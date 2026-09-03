// persistence.hpp - Atomic on-disk persistence container for broker state.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// The container is deterministic and versioned:
//   magic(8) | version(u32) | payload_len(u64) | crc32(payload)(u32) | payload | sha256(payload)(32)
// Readers reject bad magic/version/truncation/checksum/digest.
#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace resourcebroker {
namespace persistence {

// Write bytes as an atomic persistence container (temp -> flush -> rename).
void write_file_atomic(const std::string& path, const std::vector<std::uint8_t>& payload);

// Read and validate a persistence container; returns the payload.
std::vector<std::uint8_t> read_file(const std::string& path);

}  // namespace persistence
}  // namespace resourcebroker