#pragma once

/// @file wal.h
/// @brief Write-Ahead Log (WAL) for crash-recoverable persistence.
///
/// Provides an append-only log that writes entries to disk before they are
/// applied to the in-memory data structure. On startup, the log can be replayed
/// to reconstruct state.
///
/// Binary format per entry:
///   [4 bytes] payload_length (uint32_t, little-endian)
///   [4 bytes] crc32          (uint32_t, little-endian, over the payload)
///   [1 byte]  operation      (0x01 = PUT, 0x02 = DELETE)
///   [4 bytes] key_length     (uint32_t, little-endian)
///   [N bytes] key_data
///   [4 bytes] value_length   (uint32_t, little-endian)  — only for PUT
///   [M bytes] value_data                                — only for PUT
///
/// Each entry is self-contained. Corrupted entries (CRC mismatch or truncation)
/// are detected and skipped during replay, with a warning logged.

#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace outpost {
namespace common {

/// @brief Operation type for a WAL entry.
enum class WalOperation : uint8_t {
    PUT = 0x01,
    DELETE = 0x02,
};

/// @brief A single WAL entry representing a mutation.
struct WalEntry {
    WalOperation operation;
    std::string key;
    std::string value;  ///< Empty for DELETE operations.
    uint64_t sequenceNumber = 0;  ///< Monotonically increasing, assigned on append.
};

/// @brief Append-only write-ahead log with binary format and CRC32 integrity checks.
///
/// Thread safety: all public methods are protected by a mutex.
/// Concurrent appends from multiple threads are serialized.
class WriteAheadLog {
public:
    /// @brief Construct a WAL that writes to the specified file path.
    /// @param filePath Path to the WAL file. Created if it does not exist.
    explicit WriteAheadLog(const std::string& filePath);

    ~WriteAheadLog();

    // Non-copyable, non-movable.
    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;
    WriteAheadLog(WriteAheadLog&&) = delete;
    WriteAheadLog& operator=(WriteAheadLog&&) = delete;

    /// @brief Append a PUT entry to the log and flush to disk.
    /// @param key   The key being written.
    /// @param value The value being written.
    /// @return The sequence number assigned to this entry.
    uint64_t appendPut(const std::string& key, const std::string& value);

    /// @brief Append a DELETE entry to the log and flush to disk.
    /// @param key The key being deleted.
    /// @return The sequence number assigned to this entry.
    uint64_t appendDelete(const std::string& key);

    /// @brief Replay all valid entries from the log file.
    ///
    /// Reads the file from the beginning. Entries that fail CRC validation
    /// or are truncated are skipped with a warning.
    ///
    /// @return A vector of valid entries in the order they were written.
    std::vector<WalEntry> replay() const;

    /// @brief Replay entries by invoking a callback for each valid entry.
    ///
    /// More memory-efficient than replay() for large logs.
    /// @param callback Called for each valid entry. Return false to stop early.
    void replayForEach(const std::function<bool(const WalEntry& entry)>& callback) const;

    /// @brief Truncate the WAL file, removing all entries.
    ///
    /// Used after a successful compaction or checkpoint.
    void truncate();

    /// @brief Return the path to the WAL file.
    const std::string& filePath() const { return filePath_; }

    /// @brief Return the number of entries appended since this instance was created.
    uint64_t entriesAppended() const;

    /// @brief Return the current sequence number (the next entry will get this + 1).
    uint64_t currentSequence() const;

    /// @brief Compute CRC32 of a byte buffer (publicly exposed for testing).
    static uint32_t computeCrc32(const uint8_t* data, std::size_t length);

private:
    /// @brief Serialize and write a single entry to the open file stream.
    uint64_t appendEntry(const WalEntry& entry);

    /// @brief Ensure the output file stream is open and ready for writing.
    void ensureOpen();

    std::string filePath_;
    mutable std::mutex mutex_;
    std::ofstream outFile_;
    uint64_t sequenceCounter_ = 0;
    uint64_t entriesAppended_ = 0;
};

}  // namespace common
}  // namespace outpost
