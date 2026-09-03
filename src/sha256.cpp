// sha256.cpp - Standard SHA-256 (FIPS 180-4).
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/digest.hpp"

#include <cstring>
#include <cstdio>

namespace resourcebroker {

namespace {

constexpr std::uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

constexpr std::uint32_t rotr(std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32u - n)); }

struct Sha256Ctx {
    std::uint32_t h[8];
    std::uint64_t total_bits = 0;
    std::uint8_t buffer[64];
    std::size_t buffer_len = 0;
};

void init(Sha256Ctx& ctx) {
    ctx.h[0] = 0x6a09e667u; ctx.h[1] = 0xbb67ae85u; ctx.h[2] = 0x3c6ef372u; ctx.h[3] = 0xa54ff53au;
    ctx.h[4] = 0x510e527fu; ctx.h[5] = 0x9b05688cu; ctx.h[6] = 0x1f83d9abu; ctx.h[7] = 0x5be0cd19u;
    ctx.total_bits = 0; ctx.buffer_len = 0;
}

void transform(Sha256Ctx& ctx, const std::uint8_t* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i*4]) << 24) |
               (static_cast<std::uint32_t>(block[i*4+1]) << 16) |
               (static_cast<std::uint32_t>(block[i*4+2]) << 8) |
                static_cast<std::uint32_t>(block[i*4+3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        const std::uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    std::uint32_t a = ctx.h[0], b = ctx.h[1], c = ctx.h[2], d = ctx.h[3];
    std::uint32_t e = ctx.h[4], f = ctx.h[5], g = ctx.h[6], h = ctx.h[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + kK[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx.h[0] += a; ctx.h[1] += b; ctx.h[2] += c; ctx.h[3] += d;
    ctx.h[4] += e; ctx.h[5] += f; ctx.h[6] += g; ctx.h[7] += h;
}

void update(Sha256Ctx& ctx, const std::uint8_t* data, std::size_t len) {
    ctx.total_bits += static_cast<std::uint64_t>(len) * 8u;
    while (len > 0) {
        const std::size_t take = (64 - ctx.buffer_len) < len ? (64 - ctx.buffer_len) : len;
        std::memcpy(ctx.buffer + ctx.buffer_len, data, take);
        ctx.buffer_len += take;
        data += take;
        len -= take;
        if (ctx.buffer_len == 64) { transform(ctx, ctx.buffer); ctx.buffer_len = 0; }
    }
}

void finalize(Sha256Ctx& ctx, std::uint8_t out[32]) {
    const std::uint64_t bits = ctx.total_bits;
    const std::uint8_t pad = 0x80u;
    update(ctx, &pad, 1);
    const std::uint8_t zero = 0x00u;
    while (ctx.buffer_len != 56) { update(ctx, &zero, 1); }
    std::uint8_t len_be[8];
    for (int i = 0; i < 8; ++i) { len_be[i] = static_cast<std::uint8_t>((bits >> ((7 - i) * 8)) & 0xFFu); }
    update(ctx, len_be, 8);
    for (int i = 0; i < 8; ++i) {
        out[i*4]   = static_cast<std::uint8_t>((ctx.h[i] >> 24) & 0xFFu);
        out[i*4+1] = static_cast<std::uint8_t>((ctx.h[i] >> 16) & 0xFFu);
        out[i*4+2] = static_cast<std::uint8_t>((ctx.h[i] >> 8) & 0xFFu);
        out[i*4+3] = static_cast<std::uint8_t>(ctx.h[i] & 0xFFu);
    }
}

}  // namespace

std::uint32_t Crc32::compute(const std::uint8_t* data, std::size_t len) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (int j = 0; j < 8; ++j) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t Crc32::compute(const std::string& s) noexcept {
    return compute(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

Sha256::Digest Sha256::compute(const std::uint8_t* data, std::size_t len) noexcept {
    Sha256Ctx ctx; init(ctx); update(ctx, data, len);
    Digest out{}; finalize(ctx, out.data());
    return out;
}

Sha256::Digest Sha256::compute(const std::string& s) noexcept {
    return compute(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

std::string Sha256::hex(const Digest& d) noexcept {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::size_t i = 0; i < d.size(); ++i) {
        const std::uint8_t b = d[i];
        out.push_back(digits[(b >> 4) & 0x0Fu]);
        out.push_back(digits[b & 0x0Fu]);
    }
    return out;
}

}  // namespace resourcebroker
