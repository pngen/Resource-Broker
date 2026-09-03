// units.hpp - Strong, non-interchangeable physical units.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Resource dimensions must never be mixed accidentally. Each dimension is a
// distinct strong type with checked arithmetic that throws on overflow.
#pragma once

#include <cstdint>
#include <limits>
#include <ostream>
#include <cmath>
#include <string>
#include "safe_math.hpp"

namespace resourcebroker {

// Byte count. Exact integer accounting.
class Bytes {
public:
    using value_type = std::uint64_t;
    constexpr Bytes() noexcept : v_(0) {}
    explicit constexpr Bytes(value_type v) noexcept : v_(v) {}
    static constexpr Bytes zero() noexcept { return Bytes{0}; }
    constexpr value_type value() const noexcept { return v_; }

    Bytes operator+(const Bytes& o) const { return Bytes{checked_add(v_, o.v_)}; }
    Bytes operator-(const Bytes& o) const { return Bytes{checked_sub(v_, o.v_)}; }
    Bytes& operator+=(const Bytes& o) { v_ = checked_add(v_, o.v_); return *this; }
    Bytes& operator-=(const Bytes& o) { v_ = checked_sub(v_, o.v_); return *this; }
    bool operator<(const Bytes& o) const noexcept { return v_ < o.v_; }
    bool operator<=(const Bytes& o) const noexcept { return v_ <= o.v_; }
    bool operator>(const Bytes& o) const noexcept { return v_ > o.v_; }
    bool operator>=(const Bytes& o) const noexcept { return v_ >= o.v_; }
    bool operator==(const Bytes& o) const noexcept { return v_ == o.v_; }
    bool operator!=(const Bytes& o) const noexcept { return v_ != o.v_; }

    std::string to_string() const;

private:
    value_type v_;
};

class BytesPerSecond {
public:
    using value_type = std::uint64_t;
    constexpr BytesPerSecond() noexcept : v_(0) {}
    explicit constexpr BytesPerSecond(value_type v) noexcept : v_(v) {}
    static constexpr BytesPerSecond zero() noexcept { return BytesPerSecond{0}; }
    constexpr value_type value() const noexcept { return v_; }

    BytesPerSecond operator+(const BytesPerSecond& o) const { return BytesPerSecond{checked_add(v_, o.v_)}; }
    BytesPerSecond operator-(const BytesPerSecond& o) const { return BytesPerSecond{checked_sub(v_, o.v_)}; }
    bool operator<(const BytesPerSecond& o) const noexcept { return v_ < o.v_; }
    bool operator<=(const BytesPerSecond& o) const noexcept { return v_ <= o.v_; }
    bool operator>(const BytesPerSecond& o) const noexcept { return v_ > o.v_; }
    bool operator>=(const BytesPerSecond& o) const noexcept { return v_ >= o.v_; }
    bool operator==(const BytesPerSecond& o) const noexcept { return v_ == o.v_; }
    bool operator!=(const BytesPerSecond& o) const noexcept { return v_ != o.v_; }
    std::string to_string() const;

private:
    value_type v_;
};

class Count {
public:
    using value_type = std::uint64_t;
    constexpr Count() noexcept : v_(0) {}
    explicit constexpr Count(value_type v) noexcept : v_(v) {}
    static constexpr Count zero() noexcept { return Count{0}; }
    constexpr value_type value() const noexcept { return v_; }

    Count operator+(const Count& o) const { return Count{checked_add(v_, o.v_)}; }
    Count operator-(const Count& o) const { return Count{checked_sub(v_, o.v_)}; }
    bool operator<(const Count& o) const noexcept { return v_ < o.v_; }
    bool operator<=(const Count& o) const noexcept { return v_ <= o.v_; }
    bool operator>(const Count& o) const noexcept { return v_ > o.v_; }
    bool operator>=(const Count& o) const noexcept { return v_ >= o.v_; }
    bool operator==(const Count& o) const noexcept { return v_ == o.v_; }
    bool operator!=(const Count& o) const noexcept { return v_ != o.v_; }
    std::string to_string() const { return std::to_string(v_); }

private:
    value_type v_;
};

// Normalized compute share in [0.0, 1.0], fixed-point with 1'000'000 units
// per whole so accounting is exact and deterministic.
class ComputeShare {
public:
    using value_type = std::uint64_t;
    static constexpr value_type kUnitsPerWhole = 1000000ULL;

    constexpr ComputeShare() noexcept : units_(0) {}
    explicit constexpr ComputeShare(value_type micro_share) noexcept : units_(micro_share) {}

    static ComputeShare from_fraction(double fraction) {
        if (fraction <= 0.0 || !std::isfinite(fraction)) return ComputeShare{0};
        if (fraction >= 1.0) return ComputeShare{kUnitsPerWhole};
        const double scaled = fraction * static_cast<double>(kUnitsPerWhole);
        const auto u = static_cast<value_type>(scaled);
        return ComputeShare{u > kUnitsPerWhole ? kUnitsPerWhole : u};
    }
    static constexpr ComputeShare whole() noexcept { return ComputeShare{kUnitsPerWhole}; }
    static constexpr ComputeShare zero() noexcept { return ComputeShare{0}; }

    constexpr value_type value() const noexcept { return units_; }
    double fraction() const noexcept {
        return static_cast<double>(units_) / static_cast<double>(kUnitsPerWhole);
    }

    ComputeShare operator+(const ComputeShare& o) const { return ComputeShare{checked_add(units_, o.units_)}; }
    ComputeShare operator-(const ComputeShare& o) const { return ComputeShare{checked_sub(units_, o.units_)}; }
    ComputeShare& operator+=(const ComputeShare& o) { units_ = checked_add(units_, o.units_); return *this; }
    ComputeShare& operator-=(const ComputeShare& o) { units_ = checked_sub(units_, o.units_); return *this; }
    bool operator<(const ComputeShare& o) const noexcept { return units_ < o.units_; }
    bool operator<=(const ComputeShare& o) const noexcept { return units_ <= o.units_; }
    bool operator>(const ComputeShare& o) const noexcept { return units_ > o.units_; }
    bool operator>=(const ComputeShare& o) const noexcept { return units_ >= o.units_; }
    bool operator==(const ComputeShare& o) const noexcept { return units_ == o.units_; }
    bool operator!=(const ComputeShare& o) const noexcept { return units_ != o.units_; }
    std::string to_string() const { return std::to_string(fraction()); }

private:
    value_type units_;
};

class Duration {
public:
    using value_type = std::int64_t;
    constexpr Duration() noexcept : ns_(0) {}
    explicit constexpr Duration(value_type nanoseconds) noexcept : ns_(nanoseconds) {}
    static constexpr Duration zero() noexcept { return Duration{0}; }
    constexpr value_type nanoseconds() const noexcept { return ns_; }
    bool is_zero() const noexcept { return ns_ == 0; }
    bool is_positive() const noexcept { return ns_ > 0; }
    Duration operator+(const Duration& o) const { return Duration{checked_add(ns_, o.ns_)}; }
    bool operator<(const Duration& o) const noexcept { return ns_ < o.ns_; }
    bool operator<=(const Duration& o) const noexcept { return ns_ <= o.ns_; }
    bool operator>(const Duration& o) const noexcept { return ns_ > o.ns_; }
    bool operator>=(const Duration& o) const noexcept { return ns_ >= o.ns_; }
    bool operator==(const Duration& o) const noexcept { return ns_ == o.ns_; }
    bool operator!=(const Duration& o) const noexcept { return ns_ != o.ns_; }
    std::string to_string() const;

private:
    value_type ns_;
};

class Watts {
public:
    using value_type = double;
    constexpr Watts() noexcept : v_(0.0) {}
    explicit constexpr Watts(double v) noexcept : v_(v) {}
    double value() const noexcept { return v_; }
    bool operator==(const Watts& o) const noexcept { return v_ == o.v_; }
    bool operator!=(const Watts& o) const noexcept { return v_ != o.v_; }
private:
    value_type v_;
};

inline std::ostream& operator<<(std::ostream& os, const Bytes& b) { os << b.to_string(); return os; }
inline std::ostream& operator<<(std::ostream& os, const BytesPerSecond& b) { os << b.to_string(); return os; }
inline std::ostream& operator<<(std::ostream& os, const Count& c) { os << c.to_string(); return os; }
inline std::ostream& operator<<(std::ostream& os, const ComputeShare& c) { os << c.to_string(); return os; }
inline std::ostream& operator<<(std::ostream& os, const Duration& d) { os << d.to_string(); return os; }
inline std::ostream& operator<<(std::ostream& os, const Watts& w) { os << w.value() << "W"; return os; }

}  // namespace resourcebroker
