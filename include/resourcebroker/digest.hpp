// digest.hpp - CRC-32 and SHA-256 primitives for persistence integrity.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Implemented in-house; no external crypto dependency. Used to detect
// corruption, truncation, and tampering of persisted broker state.
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>

namespace resourcebroker {

// CRC-32 (IEEE 802.3, same polynomial as zlib).
class Crc32 {
public:
    static std::uint32_t compute(const std::uint8_t* data, std::size_t len) noexcept;
    static std::uint32_t compute(const std::string& s) noexcept;
};

// SHA-256. Produces a 32-byte semantic digest.
class Sha256 {
public:
    using Digest = std::array<std::uint8_t, 32>;
    static Digest compute(const std::uint8_t* data, std::size_t len) noexcept;
    static Digest compute(const std::string& s) noexcept;
    static std::string hex(const Digest& d) noexcept;
};

using SemanticDigest = Sha256::Digest;

}  // namespace resourcebroker
