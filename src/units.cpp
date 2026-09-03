// units.cpp - Physical unit formatting helpers.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/units.hpp"

#include <sstream>

namespace resourcebroker {

static std::string format_ieec(std::uint64_t bytes, const char* suffix) {
    std::ostringstream oss;
    oss << bytes << suffix;
    return oss.str();
}

std::string Bytes::to_string() const {
    if (v_ >= (1000ULL * 1000ULL * 1000ULL * 1000ULL)) {
        return format_ieec(v_ / (1000ULL * 1000ULL * 1000ULL * 1000ULL), "TiB");
    }
    if (v_ >= (1000ULL * 1000ULL * 1000ULL)) {
        return format_ieec(v_ / (1000ULL * 1000ULL * 1000ULL), "GiB");
    }
    if (v_ >= (1000ULL * 1000ULL)) {
        return format_ieec(v_ / (1000ULL * 1000ULL), "MiB");
    }
    if (v_ >= 1000ULL) {
        return format_ieec(v_ / 1000ULL, "KiB");
    }
    return std::to_string(v_) + "B";
}

std::string BytesPerSecond::to_string() const {
    if (v_ >= (1000ULL * 1000ULL * 1000ULL)) {
        return format_ieec(v_ / (1000ULL * 1000ULL * 1000ULL), "GiB/s");
    }
    if (v_ >= (1000ULL * 1000ULL)) {
        return format_ieec(v_ / (1000ULL * 1000ULL), "MiB/s");
    }
    if (v_ >= 1000ULL) {
        return format_ieec(v_ / 1000ULL, "KiB/s");
    }
    return std::to_string(v_) + "B/s";
}

std::string Duration::to_string() const {
    if (ns_ == 0) return "0ns";
    if (ns_ >= 1000000000LL) return std::to_string(ns_ / 1000000000LL) + "s";
    if (ns_ >= 1000000LL) return std::to_string(ns_ / 1000000LL) + "ms";
    if (ns_ >= 1000LL) return std::to_string(ns_ / 1000LL) + "us";
    return std::to_string(ns_) + "ns";
}

}  // namespace resourcebroker
