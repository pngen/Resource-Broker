// serdes.hpp - Deterministic big-endian binary serialization primitives.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Both persistence and the framed protocol use these primitives. Writers
// throw on overflow; readers throw BrokerError(TruncatedPayload/BadMagic)
// instead of reading out of bounds.
#pragma once

#include <vector>
#include <span>
#include <cstdint>
#include <cstring>
#include <string>
#include <limits>
#include "identities.hpp"
#include "error.hpp"

namespace resourcebroker {

class ByteWriter {
public:
    explicit ByteWriter(std::vector<std::uint8_t>& out) : out_(out) {}

    void u8(std::uint8_t v) { out_.push_back(v); }
    void u16(std::uint16_t v) { raw_u16(v); }
    void u32(std::uint32_t v) { raw_u32(v); }
    void u64(std::uint64_t v) { raw_u64(v); }
    void i64(std::int64_t v) { raw_u64(static_cast<std::uint64_t>(v)); }
    void bool_(bool v) { out_.push_back(v ? 1u : 0u); }
    void raw(const std::uint8_t* data, std::size_t len) { out_.insert(out_.end(), data, data + len); }

    void string(const std::string& s) {
        if (s.size() > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
            throw BrokerError(ErrorCode::Overflow, "string too long to serialize");
        }
        u32(static_cast<std::uint32_t>(s.size()));
        if (!s.empty()) out_.insert(out_.end(), s.begin(), s.end());
    }

    template <typename Tag> void id(const StrongId<Tag>& id) { u64(id.value()); }
    template <typename Tag> void gen(const StrongGeneration<Tag>& g) { u64(g.value()); }

private:
    void raw_u16(std::uint16_t v) { out_.push_back(static_cast<std::uint8_t>(v >> 8)); out_.push_back(static_cast<std::uint8_t>(v & 0xFFu)); }
    void raw_u32(std::uint32_t v) {
        out_.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    }
    void raw_u64(std::uint64_t v) {
        out_.push_back(static_cast<std::uint8_t>((v >> 56) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>((v >> 48) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>((v >> 40) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>((v >> 32) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        out_.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    }
    std::vector<std::uint8_t>& out_;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint8_t u8() { if (remaining() < 1) throw BrokerError(ErrorCode::TruncatedPayload, "u8 read past end"); return data_[pos_++]; }
    std::uint16_t u16() { return static_cast<std::uint16_t>((static_cast<std::uint16_t>(u8()) << 8) | u8()); }
    std::uint32_t u32() {
        const std::uint32_t a = u8(), b = u8(), c = u8(), d = u8();
        return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(c) << 8) | static_cast<std::uint32_t>(d);
    }
    std::uint64_t u64() {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | u8();
        return v;
    }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    bool bool_() { return u8() != 0; }

    std::string string() {
        const std::uint32_t n = u32();
        if (n > 16u * 1024u * 1024u) throw BrokerError(ErrorCode::MalformedPayload, "string length unreasonable");
        std::string s;
        if (n > 0) { s.resize(n); for (std::uint32_t i = 0; i < n; ++i) s[i] = static_cast<char>(u8()); }
        return s;
    }

    std::span<const std::uint8_t> raw(std::size_t len) {
        if (len > remaining()) throw BrokerError(ErrorCode::TruncatedPayload, "raw read past end");
        const auto out = data_.subspan(pos_, len); pos_ += len;
        return out;
    }
    std::size_t remaining() const { return data_.size() - pos_; }
    std::size_t position() const { return pos_; }

    template <typename Tag> StrongId<Tag> id() { return StrongId<Tag>(u64()); }
    template <typename Tag> StrongGeneration<Tag> gen() { return StrongGeneration<Tag>(u64()); }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
};

}  // namespace resourcebroker