#pragma once

/// @file wal.h
/// @brief Write-ahead log: an append-only file of checksummed records with crash recovery.
///
/// Record layout (all integers little-endian):
///   [4 bytes] magic          0x4C41574F
///   [4 bytes] crc32          CRC-32 over the 4 payloadLength bytes followed by the payload
///   [4 bytes] payloadLength  number of payload bytes that follow
///   [payloadLength bytes] payload:
///       [8 bytes] sequenceNumber
///       [1 byte ] operation        0x01 = PUT, 0x02 = DELETE
///       [8 bytes] version          caller-defined ordering version
///       [4 bytes] writerIdLength,  then writerId bytes
///       [4 bytes] keyLength,       then key bytes
///       [4 bytes] valueLength,     then value bytes
///
/// Recovery on construction: every record whose magic, length and checksum are valid is kept.
/// A damaged record between valid records is skipped by searching forward for the next valid
/// record. Bytes after the last valid record (a torn final write) are cut off, so new appends
/// always start on a record boundary and stay replayable.
///
/// Durability: each append is written with a single unbuffered write and, when syncOnAppend is
/// true, is durable before append() returns. Concurrent appends share fsyncs (group commit): the
/// first appender that finds no fsync running fsyncs everything written so far, with the lock
/// released, while later appenders wait for the fsync that covers their record. No delay is added
/// to wait for a batch. A failed write rolls the file back to its previous size; a failed fsync
/// marks the log failed, because the kernel may already have dropped the data, so every later
/// append throws until the process restarts and recovers from what is on disk.
///
/// Thread safety: all public methods take one mutex (an fsync leader releases it while syncing).
/// replay() returns a snapshot, so callers iterate without holding the log's lock.

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace outpost
{
namespace common
{

/// @brief Operation type for a WAL entry.
enum class WalOperation : uint8_t
{
    PUT = 0x01,
    DELETE = 0x02,
};

/// @brief A single logged mutation.
struct WalEntry
{
    WalOperation operation = WalOperation::PUT;
    std::string key;
    std::string value;            ///< Empty for DELETE.
    uint64_t version = 0;         ///< Caller-defined ordering version, stored verbatim.
    std::string writerId;         ///< Caller-defined writer identity, stored verbatim.
    uint64_t sequenceNumber = 0;  ///< Assigned by append(); ignored on input.
};

/// @brief Append and fsync counters since this instance opened the log.
struct WalStats
{
    uint64_t appends = 0;  ///< Records appended.
    uint64_t fsyncs = 0;   ///< fsync calls made to make appended records durable.
};

/// @brief What recovery found when the log was opened.
struct WalRecoveryReport
{
    std::size_t validRecords = 0;    ///< Records kept.
    std::size_t skippedBytes = 0;    ///< Damaged bytes between valid records (ignored on replay).
    std::size_t truncatedBytes = 0;  ///< Invalid tail bytes removed from the end of the file.
};

/// @brief Append-only write-ahead log with per-record checksums and tail recovery.
class WriteAheadLog
{
public:
    /// @param filePath     Log file; created when missing, recovered when present.
    /// @param syncOnAppend fsync after every append (durable across power loss) when true.
    explicit WriteAheadLog(const std::string& filePath, bool syncOnAppend = true);

    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;
    WriteAheadLog(WriteAheadLog&&) = delete;
    WriteAheadLog& operator=(WriteAheadLog&&) = delete;

    /// @brief Append an entry and make it durable.
    /// @return The sequence number assigned to the entry.
    /// @throws std::runtime_error when the write fails; the file is rolled back.
    uint64_t append(const WalEntry& entry);

    /// @brief Convenience append of a PUT with version 0 and an empty writer id.
    uint64_t appendPut(const std::string& key, const std::string& value);

    /// @brief Convenience append of a DELETE with version 0 and an empty writer id.
    uint64_t appendDelete(const std::string& key);

    /// @brief Return every valid entry currently in the log, in append order.
    std::vector<WalEntry> replay() const;

    /// @brief The entries recovery found when this instance opened the file, without re-reading it.
    ///
    /// Moves the entries out: the first call returns them, later calls return an empty vector.
    /// Lets a consumer rebuild its state at start-up from the single recovery scan.
    std::vector<WalEntry> takeRecoveredEntries();

    /// @brief Atomically remove every entry with sequenceNumber <= the given value.
    ///
    /// Entries appended after a replay() snapshot keep their place, so a consumer can drain a
    /// snapshot and then discard exactly what it processed.
    /// @return Number of entries left in the log.
    std::size_t discardThrough(uint64_t sequenceNumber);

    /// @brief Remove every entry. Sequence numbers keep increasing afterwards.
    void truncate();

    /// @brief Number of valid entries currently in the log.
    std::size_t size() const;

    /// @brief Path of the log file.
    const std::string& filePath() const;

    /// @brief Number of entries appended through this instance.
    uint64_t entriesAppended() const;

    /// @brief Highest sequence number assigned or recovered so far.
    uint64_t currentSequence() const;

    /// @brief Append and fsync counters.
    WalStats stats() const;

    /// @brief Highest sequence number known to be durable (fsync'd, or written when not syncing).
    uint64_t durableSequence() const;

    /// @brief What recovery found when this instance opened the file.
    WalRecoveryReport recoveryReport() const;

    /// @brief CRC-32 (ISO 3309 polynomial, as used by zlib) of a byte buffer.
    static uint32_t computeCrc32(const uint8_t* data, std::size_t length);

private:
    /// @brief Closes a FILE handle owned by a std::unique_ptr.
    struct FileCloser
    {
        void operator()(std::FILE* file) const;
    };
    using FileHandle = std::unique_ptr<std::FILE, FileCloser>;

    /// @brief Result of parsing a log image.
    struct ScanResult
    {
        std::vector<WalEntry> entries;
        std::size_t validEnd = 0;      ///< Offset just after the last valid record.
        std::size_t skippedBytes = 0;  ///< Damaged bytes between valid records.
    };

    static std::vector<uint8_t> readFile(const std::string& path);
    static ScanResult scanRecords(const std::vector<uint8_t>& bytes);
    static std::vector<uint8_t> encodeRecord(const WalEntry& entry);
    static FileHandle openForAppend(const std::string& path);

    const std::string filePath_;
    const bool syncOnAppend_;
    mutable std::mutex mutex_;
    FileHandle file_;
    std::size_t fileSize_ = 0;
    std::size_t recordCount_ = 0;
    uint64_t sequenceCounter_ = 0;
    uint64_t entriesAppended_ = 0;
    uint64_t fsyncCount_ = 0;
    uint64_t durableSequence_ = 0;
    bool syncInProgress_ = false;       ///< A leader is running fsync with the lock released.
    bool failed_ = false;               ///< An fsync failed; appends are refused.
    std::condition_variable syncDone_;  ///< Signalled when a group fsync finishes.
    WalRecoveryReport recovery_;
    std::vector<WalEntry> recoveredEntries_;  ///< Held until takeRecoveredEntries().
};

}  // namespace common
}  // namespace outpost
