/// @file wal.cpp
/// @brief Write-ahead log implementation: record encoding, recovery scan and atomic rewrite.

#include "common/wal.h"

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>

namespace outpost {
namespace common {

namespace {

constexpr uint32_t kRecordMagic = 0x4C41574Fu;
constexpr std::size_t kHeaderSize = 12;  // magic + crc32 + payloadLength
constexpr std::size_t kMinPayloadSize = 8 + 1 + 8 + 4 + 4 + 4;
constexpr uint32_t kMaxPayloadSize = 64u * 1024u * 1024u;

/// CRC-32 lookup table for the reflected polynomial 0xEDB88320, generated at compile time.
constexpr std::array<uint32_t, 256> kCrc32Table = [] {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
        table[i] = crc;
    }
    return table;
}();

}  // namespace

void WriteAheadLog::FileCloser::operator()(std::FILE* file) const {
    std::fclose(file);
}

uint32_t WriteAheadLog::computeCrc32(const uint8_t* data, std::size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc = kCrc32Table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> WriteAheadLog::readFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    std::ifstream in(path, std::ios::binary);
    if (in.is_open()) {
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    return bytes;
}

WriteAheadLog::ScanResult WriteAheadLog::scanRecords(const std::vector<uint8_t>& bytes) {
    auto readU32 = [&bytes](std::size_t offset) {
        return static_cast<uint32_t>(bytes[offset]) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    };
    auto readU64 = [&readU32](std::size_t offset) {
        return static_cast<uint64_t>(readU32(offset)) |
               (static_cast<uint64_t>(readU32(offset + 4)) << 32);
    };

    // Parses the record starting at `offset`; yields the entry and the record's total length.
    auto tryParse = [&](std::size_t offset) {
        std::optional<std::pair<WalEntry, std::size_t>> parsed;
        bool headerFits = offset + kHeaderSize <= bytes.size();
        uint32_t payloadLength = headerFits ? readU32(offset + 8) : 0;
        bool shapeValid = headerFits && readU32(offset) == kRecordMagic &&
                          payloadLength >= kMinPayloadSize && payloadLength <= kMaxPayloadSize &&
                          offset + kHeaderSize + payloadLength <= bytes.size();
        bool crcValid = shapeValid &&
                        computeCrc32(bytes.data() + offset + 8, 4 + payloadLength) ==
                            readU32(offset + 4);
        if (crcValid) {
            std::size_t cursor = offset + kHeaderSize;
            std::size_t end = cursor + payloadLength;
            WalEntry entry;
            entry.sequenceNumber = readU64(cursor);
            uint8_t operation = bytes[cursor + 8];
            entry.version = readU64(cursor + 9);
            cursor += 17;
            bool fieldsValid = operation == static_cast<uint8_t>(WalOperation::PUT) ||
                               operation == static_cast<uint8_t>(WalOperation::DELETE);
            entry.operation = static_cast<WalOperation>(operation);
            std::string* fields[3] = {&entry.writerId, &entry.key, &entry.value};
            for (std::string* field : fields) {
                bool lengthFits = fieldsValid && cursor + 4 <= end;
                uint32_t fieldLength = lengthFits ? readU32(cursor) : 0;
                fieldsValid = lengthFits && cursor + 4 + fieldLength <= end;
                if (fieldsValid) {
                    field->assign(reinterpret_cast<const char*>(bytes.data() + cursor + 4),
                                  fieldLength);
                    cursor += 4 + fieldLength;
                }
            }
            if (fieldsValid && cursor == end) {
                parsed.emplace(std::move(entry), kHeaderSize + payloadLength);
            }
        }
        return parsed;
    };

    ScanResult result;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        auto parsed = tryParse(offset);
        if (parsed) {
            result.entries.push_back(std::move(parsed->first));
            offset += parsed->second;
            result.validEnd = offset;
        } else {
            std::size_t next = offset + 1;
            bool found = false;
            while (!found && next + kHeaderSize <= bytes.size()) {
                found = readU32(next) == kRecordMagic && tryParse(next).has_value();
                next += found ? 0 : 1;
            }
            if (found) {
                result.skippedBytes += next - offset;
            }
            offset = found ? next : bytes.size();
        }
    }
    return result;
}

std::vector<uint8_t> WriteAheadLog::encodeRecord(const WalEntry& entry) {
    std::vector<uint8_t> record;
    record.reserve(kHeaderSize + kMinPayloadSize + entry.writerId.size() + entry.key.size() +
                   entry.value.size());
    auto putU32 = [&record](uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            record.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
        }
    };
    auto putU64 = [&putU32](uint64_t value) {
        putU32(static_cast<uint32_t>(value & 0xFFFFFFFFu));
        putU32(static_cast<uint32_t>(value >> 32));
    };

    putU32(kRecordMagic);
    putU32(0);  // crc32, patched below
    putU32(0);  // payloadLength, patched below
    putU64(entry.sequenceNumber);
    record.push_back(static_cast<uint8_t>(entry.operation));
    putU64(entry.version);
    for (const std::string* field : {&entry.writerId, &entry.key, &entry.value}) {
        putU32(static_cast<uint32_t>(field->size()));
        record.insert(record.end(), field->begin(), field->end());
    }

    uint32_t payloadLength = static_cast<uint32_t>(record.size() - kHeaderSize);
    for (int i = 0; i < 4; ++i) {
        record[8 + i] = static_cast<uint8_t>((payloadLength >> (8 * i)) & 0xFFu);
    }
    uint32_t crc = computeCrc32(record.data() + 8, 4 + payloadLength);
    for (int i = 0; i < 4; ++i) {
        record[4 + i] = static_cast<uint8_t>((crc >> (8 * i)) & 0xFFu);
    }
    return record;
}

WriteAheadLog::FileHandle WriteAheadLog::openForAppend(const std::string& path) {
    FileHandle file(std::fopen(path.c_str(), "ab"));
    if (!file) {
        throw std::runtime_error("WAL: cannot open for append: " + path);
    }
    std::setvbuf(file.get(), nullptr, _IONBF, 0);
    return file;
}

WriteAheadLog::WriteAheadLog(const std::string& filePath, bool syncOnAppend)
    : filePath_(filePath), syncOnAppend_(syncOnAppend) {
    std::vector<uint8_t> bytes = readFile(filePath_);
    ScanResult scan = scanRecords(bytes);
    recovery_.validRecords = scan.entries.size();
    recovery_.skippedBytes = scan.skippedBytes;
    recovery_.truncatedBytes = bytes.size() - scan.validEnd;
    if (recovery_.truncatedBytes > 0) {
        std::filesystem::resize_file(filePath_, scan.validEnd);
    }
    for (const WalEntry& entry : scan.entries) {
        sequenceCounter_ = std::max(sequenceCounter_, entry.sequenceNumber);
    }
    recordCount_ = scan.entries.size();
    fileSize_ = scan.validEnd;
    file_ = openForAppend(filePath_);
}

uint64_t WriteAheadLog::append(const WalEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    WalEntry stamped = entry;
    stamped.sequenceNumber = sequenceCounter_ + 1;
    std::vector<uint8_t> record = encodeRecord(stamped);

    bool written = std::fwrite(record.data(), 1, record.size(), file_.get()) == record.size() &&
                   (!syncOnAppend_ || ::fsync(::fileno(file_.get())) == 0);
    if (!written) {
        std::clearerr(file_.get());
        bool rolledBack = ::ftruncate(::fileno(file_.get()), static_cast<off_t>(fileSize_)) == 0;
        throw std::runtime_error("WAL: append failed for " + filePath_ +
                                 (rolledBack ? " (rolled back)" : " (rollback failed)"));
    }
    sequenceCounter_ = stamped.sequenceNumber;
    fileSize_ += record.size();
    ++recordCount_;
    ++entriesAppended_;
    return stamped.sequenceNumber;
}

uint64_t WriteAheadLog::appendPut(const std::string& key, const std::string& value) {
    WalEntry entry;
    entry.operation = WalOperation::PUT;
    entry.key = key;
    entry.value = value;
    return append(entry);
}

uint64_t WriteAheadLog::appendDelete(const std::string& key) {
    WalEntry entry;
    entry.operation = WalOperation::DELETE;
    entry.key = key;
    return append(entry);
}

std::vector<WalEntry> WriteAheadLog::replay() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<WalEntry> entries = scanRecords(readFile(filePath_)).entries;
    return entries;
}

std::size_t WriteAheadLog::discardThrough(uint64_t sequenceNumber) {
    std::lock_guard<std::mutex> lock(mutex_);
    ScanResult scan = scanRecords(readFile(filePath_));
    std::vector<uint8_t> kept;
    std::size_t keptCount = 0;
    for (const WalEntry& entry : scan.entries) {
        if (entry.sequenceNumber > sequenceNumber) {
            std::vector<uint8_t> record = encodeRecord(entry);
            kept.insert(kept.end(), record.begin(), record.end());
            ++keptCount;
        }
    }

    std::string tempPath = filePath_ + ".tmp";
    {
        FileHandle temp(std::fopen(tempPath.c_str(), "wb"));
        bool written = temp && std::fwrite(kept.data(), 1, kept.size(), temp.get()) == kept.size() &&
                       std::fflush(temp.get()) == 0 && ::fsync(::fileno(temp.get())) == 0;
        if (!written) {
            throw std::runtime_error("WAL: cannot write compacted log: " + tempPath);
        }
    }
    file_.reset();
    std::filesystem::rename(tempPath, filePath_);
    std::filesystem::path directory = std::filesystem::absolute(filePath_).parent_path();
    std::unique_ptr<DIR, int (*)(DIR*)> dir(::opendir(directory.c_str()), &::closedir);
    if (dir && syncOnAppend_) {
        ::fsync(::dirfd(dir.get()));
    }
    file_ = openForAppend(filePath_);
    fileSize_ = kept.size();
    recordCount_ = keptCount;
    return keptCount;
}

void WriteAheadLog::truncate() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::resize_file(filePath_, 0);
    fileSize_ = 0;
    recordCount_ = 0;
}

std::size_t WriteAheadLog::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return recordCount_;
}

const std::string& WriteAheadLog::filePath() const {
    return filePath_;
}

uint64_t WriteAheadLog::entriesAppended() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entriesAppended_;
}

uint64_t WriteAheadLog::currentSequence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sequenceCounter_;
}

WalRecoveryReport WriteAheadLog::recoveryReport() const {
    return recovery_;
}

}  // namespace common
}  // namespace outpost
