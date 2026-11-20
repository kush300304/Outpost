/// @file testWal.cpp
/// @brief Unit tests for outpost::common::WriteAheadLog.
///
/// Covers: append/replay roundtrip, sequence numbering, PUT and DELETE ops,
/// CRC corruption detection, truncation, empty-file replay, large entries,
/// and concurrent appends.

#include <gtest/gtest.h>
#include <sys/resource.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "common/wal.h"

using outpost::common::WalEntry;
using outpost::common::WalOperation;
using outpost::common::WriteAheadLog;

namespace fs = std::filesystem;

/// @brief RAII helper that creates a unique temp file path and removes it on destruction.
class TempWalFile
{
public:
    TempWalFile()
    {
        path_ = (fs::temp_directory_path() /
                 ("outpost_wal_test_" + std::to_string(counter_++) + ".wal"))
                    .string();
    }
    ~TempWalFile()
    {
        std::error_code ec;
        fs::remove(path_, ec);
    }
    const std::string& path() const
    {
        return path_;
    }

private:
    std::string path_;
    static inline std::atomic<int> counter_{0};
};

// ── Basic roundtrip ─────────────────────────────────────────────────────

TEST(WalTest, AppendPutThenReplayReturnsSameEntry)
{
    TempWalFile tmp;
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut("greeting", "hello");
    }
    // Reopen and replay
    WriteAheadLog wal2(tmp.path());
    auto entries = wal2.replay();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].operation, WalOperation::PUT);
    EXPECT_EQ(entries[0].key, "greeting");
    EXPECT_EQ(entries[0].value, "hello");
    EXPECT_EQ(entries[0].sequenceNumber, 1u);
}

TEST(WalTest, AppendDeleteThenReplay)
{
    TempWalFile tmp;
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut("key1", "val1");
        wal.appendDelete("key1");
    }
    WriteAheadLog wal2(tmp.path());
    auto entries = wal2.replay();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].operation, WalOperation::PUT);
    EXPECT_EQ(entries[0].key, "key1");
    EXPECT_EQ(entries[1].operation, WalOperation::DELETE);
    EXPECT_EQ(entries[1].key, "key1");
    EXPECT_TRUE(entries[1].value.empty());
}

TEST(WalTest, MultipleEntriesPreserveOrder)
{
    TempWalFile tmp;
    {
        WriteAheadLog wal(tmp.path());
        for (int i = 0; i < 100; ++i)
        {
            wal.appendPut("k" + std::to_string(i), "v" + std::to_string(i));
        }
    }
    WriteAheadLog wal2(tmp.path());
    auto entries = wal2.replay();
    ASSERT_EQ(entries.size(), 100u);
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(entries[i].key, "k" + std::to_string(i));
        EXPECT_EQ(entries[i].value, "v" + std::to_string(i));
        EXPECT_EQ(entries[i].sequenceNumber, static_cast<uint64_t>(i + 1));
    }
}

// ── Sequence numbering ──────────────────────────────────────────────────

TEST(WalTest, SequenceNumbersAreMonotonicallyIncreasing)
{
    TempWalFile tmp;
    WriteAheadLog wal(tmp.path());

    uint64_t seq1 = wal.appendPut("a", "1");
    uint64_t seq2 = wal.appendPut("b", "2");
    uint64_t seq3 = wal.appendDelete("a");

    EXPECT_EQ(seq1, 1u);
    EXPECT_EQ(seq2, 2u);
    EXPECT_EQ(seq3, 3u);
    EXPECT_EQ(wal.currentSequence(), 3u);
    EXPECT_EQ(wal.entriesAppended(), 3u);
}

TEST(WalTest, SequenceContinuesAfterReopen)
{
    TempWalFile tmp;
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut("a", "1");
        wal.appendPut("b", "2");
    }
    // Reopen — sequence should continue from 2
    WriteAheadLog wal2(tmp.path());
    uint64_t seq = wal2.appendPut("c", "3");
    EXPECT_EQ(seq, 3u);
}

// ── Empty and missing files ─────────────────────────────────────────────

TEST(WalTest, ReplayOnNonexistentFileReturnsEmpty)
{
    TempWalFile tmp;
    // Don't write anything — file doesn't exist
    WriteAheadLog wal(tmp.path());
    auto entries = wal.replay();
    EXPECT_TRUE(entries.empty());
}

TEST(WalTest, ReplayOnEmptyFileReturnsEmpty)
{
    TempWalFile tmp;
    // Create empty file
    {
        std::ofstream(tmp.path(), std::ios::binary);
    }
    WriteAheadLog wal(tmp.path());
    auto entries = wal.replay();
    EXPECT_TRUE(entries.empty());
}

// ── CRC corruption detection ────────────────────────────────────────────

TEST(WalTest, CorruptedEntryIsSkippedDuringReplay)
{
    TempWalFile tmp;
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut("good1", "val1");
        wal.appendPut("good2", "val2");
        wal.appendPut("good3", "val3");
    }

    // Corrupt one byte in the middle of the file (targeting the second entry's payload)
    {
        std::fstream f(tmp.path(), std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(f.is_open());

        // Seek past the first entry: 4(len) + 4(crc) + payload of first entry
        // We just need to flip a byte somewhere in the second entry's payload area
        f.seekg(0, std::ios::end);
        auto fileSize = f.tellg();
        ASSERT_GT(fileSize, 30);  // File should be reasonably sized

        // Flip a byte roughly in the middle
        auto corruptPos = fileSize / 2;
        f.seekg(corruptPos);
        char byte;
        f.read(&byte, 1);
        byte = static_cast<char>(~byte);  // Flip all bits
        f.seekp(corruptPos);
        f.write(&byte, 1);
        f.flush();
    }

    // Replay should skip the corrupted entry but recover others
    WriteAheadLog wal2(tmp.path());
    auto entries = wal2.replay();
    // At least 1 entry should survive (the first one, before the corruption point)
    EXPECT_GE(entries.size(), 1u);
    // But we shouldn't get all 3 (one should be corrupted)
    EXPECT_LT(entries.size(), 3u);
    // First entry should be intact
    EXPECT_EQ(entries[0].key, "good1");
    EXPECT_EQ(entries[0].value, "val1");
}

// ── Truncation ──────────────────────────────────────────────────────────

TEST(WalTest, TruncateRemovesAllEntries)
{
    TempWalFile tmp;
    WriteAheadLog wal(tmp.path());
    wal.appendPut("a", "1");
    wal.appendPut("b", "2");
    EXPECT_EQ(wal.entriesAppended(), 2u);

    wal.truncate();

    auto entries = wal.replay();
    EXPECT_TRUE(entries.empty());
    EXPECT_EQ(wal.size(), 0u);
    EXPECT_EQ(wal.currentSequence(), 2u);
}

TEST(WalTest, CanAppendAfterTruncate)
{
    TempWalFile tmp;
    WriteAheadLog wal(tmp.path());
    wal.appendPut("old", "data");
    wal.truncate();

    uint64_t seq = wal.appendPut("new", "data");
    EXPECT_EQ(seq, 2u);  // Sequence numbers keep increasing across truncate

    auto entries = wal.replay();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].key, "new");
}

// ── Edge cases ──────────────────────────────────────────────────────────

TEST(WalTest, EmptyKeyAndValue)
{
    TempWalFile tmp;
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut("", "");
    }
    WriteAheadLog wal2(tmp.path());
    auto entries = wal2.replay();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].key, "");
    EXPECT_EQ(entries[0].value, "");
}

TEST(WalTest, LargeEntry)
{
    TempWalFile tmp;
    std::string bigKey(1024, 'K');
    std::string bigVal(64 * 1024, 'V');  // 64 KB value
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut(bigKey, bigVal);
    }
    WriteAheadLog wal2(tmp.path());
    auto entries = wal2.replay();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].key, bigKey);
    EXPECT_EQ(entries[0].value, bigVal);
}

// ── ReplayForEach callback ──────────────────────────────────────────────

TEST(WalTest, VersionAndWriterIdRoundTrip)
{
    TempWalFile tmp;
    {
        WriteAheadLog wal(tmp.path());
        WalEntry entry;
        entry.operation = WalOperation::PUT;
        entry.key = "k";
        entry.value = "v";
        entry.version = 1234567890123ull;
        entry.writerId = "edge-2";
        wal.append(entry);
    }
    WriteAheadLog wal(tmp.path());
    auto entries = wal.replay();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].version, 1234567890123ull);
    EXPECT_EQ(entries[0].writerId, "edge-2");
    EXPECT_EQ(entries[0].key, "k");
    EXPECT_EQ(entries[0].value, "v");
}

TEST(WalTest, DiscardThroughKeepsEntriesAppendedAfterSnapshot)
{
    TempWalFile tmp;
    WriteAheadLog wal(tmp.path());
    wal.appendPut("a", "1");
    wal.appendPut("b", "2");
    auto snapshot = wal.replay();
    wal.appendPut("late", "3");  // arrives while the snapshot is being processed

    std::size_t remaining = wal.discardThrough(snapshot.back().sequenceNumber);
    EXPECT_EQ(remaining, 1u);
    EXPECT_EQ(wal.size(), 1u);
    auto entries = wal.replay();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].key, "late");
    EXPECT_EQ(entries[0].sequenceNumber, 3u);

    EXPECT_EQ(wal.appendPut("next", "4"), 4u);
    WriteAheadLog reopened(tmp.path());
    EXPECT_EQ(reopened.replay().size(), 2u);
}

TEST(WalTest, RecoveryReportCountsTruncatedTail)
{
    TempWalFile tmp;
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut("a", "1");
        wal.appendPut("b", "2");
    }
    auto fullSize = fs::file_size(tmp.path());
    fs::resize_file(tmp.path(), fullSize - 5);
    WriteAheadLog wal(tmp.path());
    auto report = wal.recoveryReport();
    EXPECT_EQ(report.validRecords, 1u);
    EXPECT_GT(report.truncatedBytes, 0u);
    EXPECT_EQ(report.skippedBytes, 0u);
    EXPECT_EQ(wal.size(), 1u);
    EXPECT_LT(fs::file_size(tmp.path()), fullSize - 5);
}

TEST(WalTest, FailedAppendIsRolledBackAndLogStaysUsable)
{
    TempWalFile tmp;
    WriteAheadLog wal(tmp.path(), false);
    wal.appendPut("a", "1");
    auto sizeBefore = fs::file_size(tmp.path());

    // Make the next large write fail with EFBIG by capping the process file-size limit.
    std::signal(SIGXFSZ, SIG_IGN);
    rlimit original{};
    getrlimit(RLIMIT_FSIZE, &original);
    rlimit capped = original;
    capped.rlim_cur = static_cast<rlim_t>(sizeBefore + 16);
    setrlimit(RLIMIT_FSIZE, &capped);
    EXPECT_THROW(wal.appendPut("big", std::string(4096, 'x')), std::runtime_error);
    setrlimit(RLIMIT_FSIZE, &original);

    EXPECT_EQ(fs::file_size(tmp.path()), sizeBefore);
    EXPECT_EQ(wal.appendPut("b", "2"), 2u);
    auto entries = wal.replay();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[1].key, "b");
}

// ── CRC32 utility ───────────────────────────────────────────────────────

TEST(WalTest, Crc32ProducesConsistentResults)
{
    const std::string data = "hello world";
    auto crc1 =
        WriteAheadLog::computeCrc32(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    auto crc2 =
        WriteAheadLog::computeCrc32(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    EXPECT_EQ(crc1, crc2);
    // Known CRC32 for "hello world" (zlib compatible)
    EXPECT_EQ(crc1, 0x0D4A1185u);
}

TEST(WalTest, Crc32DiffersForDifferentData)
{
    const std::string d1 = "abc";
    const std::string d2 = "abd";
    auto c1 = WriteAheadLog::computeCrc32(reinterpret_cast<const uint8_t*>(d1.data()), d1.size());
    auto c2 = WriteAheadLog::computeCrc32(reinterpret_cast<const uint8_t*>(d2.data()), d2.size());
    EXPECT_NE(c1, c2);
}

// ── Concurrent appends ──────────────────────────────────────────────────

TEST(WalTest, ConcurrentAppendsDoNotCorruptLog)
{
    TempWalFile tmp;
    constexpr int kThreads = 4;
    constexpr int kEntriesPerThread = 100;

    {
        WriteAheadLog wal(tmp.path());

        std::vector<std::thread> threads;
        threads.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&wal, t]() {
                for (int i = 0; i < kEntriesPerThread; ++i)
                {
                    std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                    wal.appendPut(key, "val_" + std::to_string(i));
                }
            });
        }
        for (auto& th : threads)
        {
            th.join();
        }
    }

    // All entries should be readable
    WriteAheadLog wal2(tmp.path());
    auto entries = wal2.replay();
    EXPECT_EQ(entries.size(), static_cast<std::size_t>(kThreads * kEntriesPerThread));

    // All sequence numbers should be unique
    std::vector<uint64_t> seqs;
    seqs.reserve(entries.size());
    for (const auto& e : entries)
    {
        seqs.push_back(e.sequenceNumber);
    }
    std::sort(seqs.begin(), seqs.end());
    auto uniqueEnd = std::unique(seqs.begin(), seqs.end());
    EXPECT_EQ(std::distance(seqs.begin(), uniqueEnd),
              static_cast<ptrdiff_t>(kThreads * kEntriesPerThread));
}

// ── Crash recovery ──────────────────────────────────────────────────────

TEST(WalTest, TornTailThenAppendKeepsAllAcknowledgedWrites)
{
    TempWalFile tmp;
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut("a", "1");
        wal.appendPut("b", "2");
        wal.appendPut("c", "3");
    }
    fs::resize_file(tmp.path(), fs::file_size(tmp.path()) - 3);
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut("d", "4");
        wal.appendPut("e", "5");
    }
    WriteAheadLog wal(tmp.path());
    auto entries = wal.replay();
    ASSERT_EQ(entries.size(), 4u);
    EXPECT_EQ(entries[2].key, "d");
    EXPECT_EQ(entries[3].key, "e");
}

TEST(WalTest, DamagedHeaderInMiddleKeepsLaterRecords)
{
    TempWalFile tmp;
    std::uintmax_t secondRecordOffset = 0;
    {
        WriteAheadLog wal(tmp.path());
        wal.appendPut("first", "1");
        secondRecordOffset = fs::file_size(tmp.path());
        wal.appendPut("second", "2");
        wal.appendPut("third", "3");
    }
    {
        std::fstream f(tmp.path(), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(static_cast<std::streamoff>(secondRecordOffset));
        const char damage[8] = {'\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF'};
        f.write(damage, sizeof(damage));
    }
    WriteAheadLog wal(tmp.path());
    auto entries = wal.replay();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].key, "first");
    EXPECT_EQ(entries[1].key, "third");
}

TEST(WalTest, SequenceKeepsIncreasingAfterTruncate)
{
    TempWalFile tmp;
    WriteAheadLog wal(tmp.path());
    wal.appendPut("a", "1");
    wal.appendPut("b", "2");
    wal.truncate();
    EXPECT_EQ(wal.appendPut("c", "3"), 3u);
}
