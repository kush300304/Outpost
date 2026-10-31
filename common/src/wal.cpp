/// @file wal.cpp
/// @brief Write-Ahead Log implementation with binary format and CRC32 integrity.

#include "common/wal.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace outpost {
namespace common {

// ── CRC32 (ISO 3309 / ITU-T V.42, same polynomial as zlib) ──────────────

static constexpr uint32_t kCrc32Table[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u, 0x706AF48Fu, 0xE963A535u,
    0x9E6495A3u, 0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Bu, 0x97D2D988u, 0x09B64C2Bu, 0x7EB17CBDu,
    0xE7B82D09u, 0x90BF1D9Fu, 0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu, 0x1ADAD47Du,
    0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u, 0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu,
    0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u, 0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u,
    0xA2677172u, 0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu, 0x35B5A8FAu, 0x42B2986Cu,
    0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u, 0x26D930ACu,
    0x51DE003Au, 0xC8D75180u, 0xBFD06116u, 0x21B4F6B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
    0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u, 0x2F6F7C87u, 0x58684C11u, 0xC1611DABu,
    0xB6662D3Du, 0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu,
    0x9FBFE4A5u, 0xE8B8D433u, 0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u, 0x7F6A0D6Bu,
    0x086D3D2Du, 0x91646C97u, 0xE6635C01u, 0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu,
    0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u, 0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu,
    0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u, 0x4DB26158u, 0x3AB551CEu,
    0xA3BC0074u, 0xD4BB30E2u, 0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu, 0x4369E96Au,
    0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u, 0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7AC9u,
    0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u,
    0xCE61E49Fu, 0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u, 0x2EB40D81u,
    0xB7BD5C3Bu, 0xC0BA6CADu, 0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au, 0xEAD54739u,
    0x9DD277AFu, 0x04DB2615u, 0x73DC1683u, 0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u,
    0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u, 0xF00F9344u, 0x8708A3D2u, 0x1E01F268u,
    0x6906C2FEu, 0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u, 0xFED41B76u, 0x89D32BE0u,
    0x10DA7A5Au, 0x67DD4ACCu, 0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u, 0xD6D6A3E8u,
    0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
    0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu,
    0x4669BE79u, 0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u, 0xCC0C7795u, 0xBB0B4703u,
    0x220216B9u, 0x5505262Fu, 0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u,
    0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du, 0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au,
    0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u, 0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu,
    0x0CB61B38u, 0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB9u, 0x0BDBDF21u, 0x86D3D2D4u, 0xF1D4E242u,
    0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u, 0x88085AE6u,
    0xFF0F6B70u, 0x66063BCAu, 0x11010B5Cu, 0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u,
    0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u, 0xA7672661u, 0xD06016F7u, 0x4969474Du,
    0x3E6E77DBu, 0xAE9F7A4Au, 0xD9984B8Cu, 0x40913F02u, 0x37964B94u, 0x09E02637u, 0x7EE716A1u,
    0xE7E0061Bu, 0x90E7768Du, 0x04DBB496u, 0x73DCA400u, 0xEAD5D49Au, 0x9DD8A50Cu, 0x2D02EF8Du,
    0x5A05DF1Bu, 0x8D0BE8A1u, 0xFA0CBF37u, 0x6E6CB543u, 0x196CC0D5u, 0x806BCE6Fu, 0xF768BFF9u,
    0x90BF2568u, 0xE7B815FEu, 0x7EB14544u, 0x09B675D2u,
};

uint32_t WriteAheadLog::computeCrc32(const uint8_t* data, std::size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc = kCrc32Table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// ── Little-endian serialization helpers ──────────────────────────────────

static void writeU32LE(std::ostream& out, uint32_t val) {
    uint8_t buf[4];
    buf[0] = static_cast<uint8_t>(val & 0xFF);
    buf[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
    out.write(reinterpret_cast<const char*>(buf), 4);
}

static bool readU32LE(std::istream& in, uint32_t& val) {
    uint8_t buf[4];
    if (!in.read(reinterpret_cast<char*>(buf), 4)) {
        return false;
    }
    val = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
          (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
    return true;
}

// ── WriteAheadLog implementation ─────────────────────────────────────────

WriteAheadLog::WriteAheadLog(const std::string& filePath) : filePath_(filePath) {
    // Determine the current sequence number by replaying existing entries.
    std::ifstream in(filePath_, std::ios::binary);
    if (in.is_open()) {
        replayForEach([this](const WalEntry& entry) {
            if (entry.sequenceNumber >= sequenceCounter_) {
                sequenceCounter_ = entry.sequenceNumber;
            }
            return true;  // Continue
        });
    }
    spdlog::info("WAL initialized: file={}, lastSeq={}", filePath_, sequenceCounter_);
}

WriteAheadLog::~WriteAheadLog() {
    std::lock_guard lock(mutex_);
    if (outFile_.is_open()) {
        outFile_.flush();
        outFile_.close();
    }
}

void WriteAheadLog::ensureOpen() {
    if (!outFile_.is_open()) {
        outFile_.open(filePath_, std::ios::binary | std::ios::app);
        if (!outFile_.is_open()) {
            throw std::runtime_error("WAL: failed to open file for writing: " + filePath_);
        }
    }
}

uint64_t WriteAheadLog::appendPut(const std::string& key, const std::string& value) {
    WalEntry entry;
    entry.operation = WalOperation::PUT;
    entry.key = key;
    entry.value = value;
    return appendEntry(entry);
}

uint64_t WriteAheadLog::appendDelete(const std::string& key) {
    WalEntry entry;
    entry.operation = WalOperation::DELETE;
    entry.key = key;
    return appendEntry(entry);
}

uint64_t WriteAheadLog::appendEntry(const WalEntry& entry) {
    std::lock_guard lock(mutex_);
    ensureOpen();

    ++sequenceCounter_;
    uint64_t seq = sequenceCounter_;

    // Build the payload in memory first so we can compute CRC over it.
    // Payload = [1 byte op] [4 bytes keyLen] [key] [4 bytes valLen (PUT)] [val (PUT)]
    std::vector<uint8_t> payload;
    payload.reserve(1 + 4 + entry.key.size() + 4 + entry.value.size() + 8);

    // Sequence number (8 bytes LE)
    for (int i = 0; i < 8; ++i) {
        payload.push_back(static_cast<uint8_t>((seq >> (i * 8)) & 0xFF));
    }

    // Operation
    payload.push_back(static_cast<uint8_t>(entry.operation));

    // Key
    uint32_t keyLen = static_cast<uint32_t>(entry.key.size());
    payload.push_back(static_cast<uint8_t>(keyLen & 0xFF));
    payload.push_back(static_cast<uint8_t>((keyLen >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>((keyLen >> 16) & 0xFF));
    payload.push_back(static_cast<uint8_t>((keyLen >> 24) & 0xFF));
    payload.insert(payload.end(), entry.key.begin(), entry.key.end());

    // Value (only for PUT)
    if (entry.operation == WalOperation::PUT) {
        uint32_t valLen = static_cast<uint32_t>(entry.value.size());
        payload.push_back(static_cast<uint8_t>(valLen & 0xFF));
        payload.push_back(static_cast<uint8_t>((valLen >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>((valLen >> 16) & 0xFF));
        payload.push_back(static_cast<uint8_t>((valLen >> 24) & 0xFF));
        payload.insert(payload.end(), entry.value.begin(), entry.value.end());
    }

    uint32_t payloadLen = static_cast<uint32_t>(payload.size());
    uint32_t crc = computeCrc32(payload.data(), payload.size());

    // Write: [payloadLen][crc][payload]
    writeU32LE(outFile_, payloadLen);
    writeU32LE(outFile_, crc);
    outFile_.write(reinterpret_cast<const char*>(payload.data()),
                   static_cast<std::streamsize>(payload.size()));
    outFile_.flush();

    if (!outFile_.good()) {
        throw std::runtime_error("WAL: write failed for seq=" + std::to_string(seq));
    }

    ++entriesAppended_;
    spdlog::debug("WAL append: seq={} op={} key={}", seq,
                  entry.operation == WalOperation::PUT ? "PUT" : "DEL", entry.key);

    return seq;
}

std::vector<WalEntry> WriteAheadLog::replay() const {
    std::vector<WalEntry> entries;
    replayForEach([&entries](const WalEntry& entry) {
        entries.push_back(entry);
        return true;
    });
    return entries;
}

void WriteAheadLog::replayForEach(
    const std::function<bool(const WalEntry& entry)>& callback) const {
    std::lock_guard lock(mutex_);

    std::ifstream in(filePath_, std::ios::binary);
    if (!in.is_open()) {
        spdlog::debug("WAL replay: file does not exist yet: {}", filePath_);
        return;
    }

    std::size_t validCount = 0;
    std::size_t corruptCount = 0;

    while (in.peek() != EOF) {
        // Read header: [payloadLen][crc]
        uint32_t payloadLen = 0;
        uint32_t storedCrc = 0;
        if (!readU32LE(in, payloadLen) || !readU32LE(in, storedCrc)) {
            if (in.eof()) break;  // Clean EOF mid-header is just end of file
            spdlog::warn("WAL replay: truncated header at entry {}", validCount + corruptCount);
            break;
        }

        // Sanity check: reject obviously bogus lengths (>64 MB)
        if (payloadLen > 64 * 1024 * 1024) {
            spdlog::warn("WAL replay: payload length {} exceeds 64MB limit, stopping",
                         payloadLen);
            break;
        }

        // Read payload
        std::vector<uint8_t> payload(payloadLen);
        if (!in.read(reinterpret_cast<char*>(payload.data()),
                     static_cast<std::streamsize>(payloadLen))) {
            spdlog::warn("WAL replay: truncated payload at entry {}", validCount + corruptCount);
            break;
        }

        // Verify CRC
        uint32_t computedCrc = computeCrc32(payload.data(), payload.size());
        if (computedCrc != storedCrc) {
            spdlog::warn("WAL replay: CRC mismatch at entry {} (stored={:#010x}, "
                         "computed={:#010x}), skipping",
                         validCount + corruptCount, storedCrc, computedCrc);
            ++corruptCount;
            continue;
        }

        // Parse payload
        // Minimum payload: 8 (seq) + 1 (op) + 4 (keyLen) = 13 bytes
        if (payloadLen < 13) {
            spdlog::warn("WAL replay: payload too short ({} bytes), skipping", payloadLen);
            ++corruptCount;
            continue;
        }

        std::size_t offset = 0;

        // Sequence number (8 bytes LE)
        uint64_t seq = 0;
        for (int i = 0; i < 8; ++i) {
            seq |= static_cast<uint64_t>(payload[offset + i]) << (i * 8);
        }
        offset += 8;

        WalEntry entry;
        entry.sequenceNumber = seq;

        // Operation
        entry.operation = static_cast<WalOperation>(payload[offset]);
        offset += 1;

        if (entry.operation != WalOperation::PUT && entry.operation != WalOperation::DELETE) {
            spdlog::warn("WAL replay: unknown operation {:#04x} at seq={}, skipping",
                         static_cast<unsigned>(payload[offset - 1]), seq);
            ++corruptCount;
            continue;
        }

        // Key length + key
        if (offset + 4 > payloadLen) {
            ++corruptCount;
            continue;
        }
        uint32_t keyLen = static_cast<uint32_t>(payload[offset]) |
                          (static_cast<uint32_t>(payload[offset + 1]) << 8) |
                          (static_cast<uint32_t>(payload[offset + 2]) << 16) |
                          (static_cast<uint32_t>(payload[offset + 3]) << 24);
        offset += 4;

        if (offset + keyLen > payloadLen) {
            ++corruptCount;
            continue;
        }
        entry.key.assign(reinterpret_cast<const char*>(payload.data() + offset), keyLen);
        offset += keyLen;

        // Value (only for PUT)
        if (entry.operation == WalOperation::PUT) {
            if (offset + 4 > payloadLen) {
                ++corruptCount;
                continue;
            }
            uint32_t valLen = static_cast<uint32_t>(payload[offset]) |
                              (static_cast<uint32_t>(payload[offset + 1]) << 8) |
                              (static_cast<uint32_t>(payload[offset + 2]) << 16) |
                              (static_cast<uint32_t>(payload[offset + 3]) << 24);
            offset += 4;

            if (offset + valLen > payloadLen) {
                ++corruptCount;
                continue;
            }
            entry.value.assign(reinterpret_cast<const char*>(payload.data() + offset), valLen);
            offset += valLen;
        }

        ++validCount;
        if (!callback(entry)) {
            break;
        }
    }

    spdlog::info("WAL replay complete: {} valid entries, {} corrupted/skipped", validCount,
                 corruptCount);
}

void WriteAheadLog::truncate() {
    std::lock_guard lock(mutex_);
    if (outFile_.is_open()) {
        outFile_.close();
    }
    // Open with trunc to clear the file
    std::ofstream truncStream(filePath_, std::ios::binary | std::ios::trunc);
    truncStream.close();
    sequenceCounter_ = 0;
    entriesAppended_ = 0;
    spdlog::info("WAL truncated: {}", filePath_);
}

uint64_t WriteAheadLog::entriesAppended() const {
    std::lock_guard lock(mutex_);
    return entriesAppended_;
}

uint64_t WriteAheadLog::currentSequence() const {
    std::lock_guard lock(mutex_);
    return sequenceCounter_;
}

}  // namespace common
}  // namespace outpost
