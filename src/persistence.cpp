// persistence.cpp - Atomic container read/write.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/persistence.hpp"
#include "resourcebroker/digest.hpp"
#include "resourcebroker/error.hpp"
#include "resourcebroker/serdes.hpp"

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iterator>

namespace resourcebroker {
namespace {

const char kMagic[8] = {'R','B','S','T','A','T','E', 0x01};
constexpr std::uint32_t kVersion = 1;

}  // namespace

namespace persistence {

void write_file_atomic(const std::string& path, const std::vector<std::uint8_t>& payload) {
    // Build the container.
    std::vector<std::uint8_t> out;
    out.reserve(8 + 4 + 8 + 4 + payload.size() + 32);
    out.insert(out.end(), kMagic, kMagic + 8);
    ByteWriter w(out);
    w.u32(kVersion);
    if (payload.size() > static_cast<std::size_t>((std::numeric_limits<std::uint64_t>::max)())) {
        throw BrokerError(ErrorCode::Overflow, "payload too large");
    }
    w.u64(static_cast<std::uint64_t>(payload.size()));
    w.u32(Crc32::compute(payload.data(), payload.size()));
    if (!payload.empty()) out.insert(out.end(), payload.begin(), payload.end());
    const Sha256::Digest dig = Sha256::compute(payload.data(), payload.size());
    out.insert(out.end(), dig.begin(), dig.end());

    // temp -> flush -> close -> rename.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw BrokerError(ErrorCode::Persistence, "cannot open temp file");
        f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        f.flush();
        f.close();
    }
    // Overwrite any existing destination atomically.
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(path, ec2);
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) throw BrokerError(ErrorCode::Persistence, "cannot rename temp to destination");
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw BrokerError(ErrorCode::Persistence, "cannot open state file");
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (data.size() < 8 + 4 + 8 + 4 + 32) throw BrokerError(ErrorCode::TruncatedPayload, "file too short");
    // Check magic.
    for (int i = 0; i < 8; ++i) {
        if (data[static_cast<std::size_t>(i)] != static_cast<std::uint8_t>(kMagic[i])) {
            throw BrokerError(ErrorCode::BadMagic, "bad magic");
        }
    }
    ByteReader r(std::span<const std::uint8_t>(data.data(), data.size()));
    (void)r.u8(); (void)r.u8(); (void)r.u8(); (void)r.u8(); (void)r.u8(); (void)r.u8(); (void)r.u8(); (void)r.u8();
    const std::uint32_t version = r.u32();
    if (version != kVersion) throw BrokerError(ErrorCode::UnsupportedVersion, "unsupported persistence version");
    const std::uint64_t payload_len = r.u64();
    const std::uint32_t crc = r.u32();
    if (payload_len != r.remaining() - 32) throw BrokerError(ErrorCode::TruncatedPayload, "payload length mismatch");
    auto payload = r.raw(static_cast<std::size_t>(payload_len));
    const auto dig_expected = r.raw(32);
    const auto crc_actual = Crc32::compute(payload.data(), payload.size());
    if (crc_actual != crc) throw BrokerError(ErrorCode::ChecksumMismatch, "checksum mismatch");
    const Sha256::Digest dig_actual = Sha256::compute(payload.data(), payload.size());
    for (int i = 0; i < 32; ++i) {
        if (dig_actual[static_cast<std::size_t>(i)] != dig_expected[static_cast<std::size_t>(i)]) {
            throw BrokerError(ErrorCode::ChecksumMismatch, "semantic digest mismatch");
        }
    }
    return std::vector<std::uint8_t>(payload.begin(), payload.end());
}

}  // namespace persistence
}  // namespace resourcebroker