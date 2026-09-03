// safe_math.hpp - Checked arithmetic that never overflows or goes negative.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#pragma once

#include <cstdint>
#include <limits>
#include "error.hpp"

namespace resourcebroker {

// Checked addition for unsigned 64-bit values. Throws on overflow.
inline std::uint64_t checked_add(std::uint64_t a, std::uint64_t b) {
    if (b > (std::numeric_limits<std::uint64_t>::max)() - a) {
        throw BrokerError(ErrorCode::Overflow, "unsigned 64-bit addition overflow");
    }
    return a + b;
}

// Checked subtraction for unsigned 64-bit values. Throws on underflow.
inline std::uint64_t checked_sub(std::uint64_t a, std::uint64_t b) {
    if (b > a) {
        throw BrokerError(ErrorCode::Overflow, "unsigned 64-bit subtraction underflow");
    }
    return a - b;
}

// Checked multiplication for unsigned 64-bit values. Throws on overflow.
inline std::uint64_t checked_mul(std::uint64_t a, std::uint64_t b) {
    if (a != 0 && b > (std::numeric_limits<std::uint64_t>::max)() / a) {
        throw BrokerError(ErrorCode::Overflow, "unsigned 64-bit multiplication overflow");
    }
    return a * b;
}

// Checked addition for signed 64-bit values. Throws on overflow.
inline std::int64_t checked_add(std::int64_t a, std::int64_t b) {
    constexpr std::int64_t kMax = (std::numeric_limits<std::int64_t>::max)();
    constexpr std::int64_t kMin = (std::numeric_limits<std::int64_t>::min)();
    if ((b > 0 && a > kMax - b) || (b < 0 && a < kMin - b)) {
        throw BrokerError(ErrorCode::Overflow, "signed 64-bit addition overflow");
    }
    return a + b;
}

}  // namespace resourcebroker
