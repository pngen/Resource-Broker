// error.hpp - Error codes and exceptions.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#pragma once

#include <stdexcept>
#include <string>

namespace resourcebroker {

enum class ErrorCode {
    InvalidArgument,
    NotFound,
    DuplicateIdentity,
    InsufficientCapacity,
    AccountingInvariant,
    InvalidState,
    StaleAuthority,
    GenerationRegression,
    MalformedPayload,
    TruncatedPayload,
    ChecksumMismatch,
    UnsupportedVersion,
    BadMagic,
    Overflow,
    Persistence,
    UnsupportedCapacity,
    UnsupportedOperation,
    Internal,
    Unknown
};

inline const char* to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument: return "invalid_argument";
        case ErrorCode::NotFound: return "not_found";
        case ErrorCode::DuplicateIdentity: return "duplicate_identity";
        case ErrorCode::InsufficientCapacity: return "insufficient_capacity";
        case ErrorCode::AccountingInvariant: return "accounting_invariant";
        case ErrorCode::InvalidState: return "invalid_state";
        case ErrorCode::StaleAuthority: return "stale_authority";
        case ErrorCode::GenerationRegression: return "generation_regression";
        case ErrorCode::MalformedPayload: return "malformed_payload";
        case ErrorCode::TruncatedPayload: return "truncated_payload";
        case ErrorCode::ChecksumMismatch: return "checksum_mismatch";
        case ErrorCode::UnsupportedVersion: return "unsupported_version";
        case ErrorCode::BadMagic: return "bad_magic";
        case ErrorCode::Overflow: return "overflow";
        case ErrorCode::Persistence: return "persistence";
        case ErrorCode::UnsupportedCapacity: return "unsupported_capacity";
        case ErrorCode::UnsupportedOperation: return "unsupported_operation";
        case ErrorCode::Internal: return "internal";
        case ErrorCode::Unknown: return "unknown";
    }
    return "unknown";
}

// Rich exception carrying a stable error code and a human-readable message.
class BrokerError : public std::runtime_error {
public:
    BrokerError(ErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    ErrorCode code() const noexcept { return code_; }
    const char* code_string() const noexcept { return to_string(code_); }

private:
    ErrorCode code_;
};

}  // namespace resourcebroker
