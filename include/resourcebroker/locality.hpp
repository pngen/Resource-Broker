// locality.hpp - Locality enum. Locality is a ranking input, never authority.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#pragma once

namespace resourcebroker {

enum class LocalityRange {
    SAME_DEVICE,
    SAME_NODE,
    SAME_HOST,
    SAME_BACKEND,
    REMOTE,
    SYNTHETIC_REMOTE,
    UNKNOWN
};

inline const char* to_string(LocalityRange l) noexcept {
    switch (l) {
        case LocalityRange::SAME_DEVICE: return "SAME_DEVICE";
        case LocalityRange::SAME_NODE: return "SAME_NODE";
        case LocalityRange::SAME_HOST: return "SAME_HOST";
        case LocalityRange::SAME_BACKEND: return "SAME_BACKEND";
        case LocalityRange::REMOTE: return "REMOTE";
        case LocalityRange::SYNTHETIC_REMOTE: return "SYNTHETIC_REMOTE";
        case LocalityRange::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace resourcebroker
