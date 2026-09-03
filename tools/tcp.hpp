// tcp.hpp - Minimal blocking TCP helpers for the coordinator/worker proof.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

// Undef Windows macros that collide with Resource Broker enum/identifier names.
#ifdef ERROR
#undef ERROR
#endif
#ifdef OPTIONAL
#undef OPTIONAL
#endif
#ifdef DELETE
#undef DELETE
#endif
#ifdef IN
#undef IN
#endif
#ifdef OUT
#undef OUT
#endif
#ifdef NEAR
#undef NEAR
#endif
#ifdef FAR
#undef FAR
#endif

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include "resourcebroker/error.hpp"
#include "resourcebroker/protocol.hpp"

namespace rbnet {

inline bool net_init() {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}
inline void net_cleanup() { WSACleanup(); }

inline SOCKET net_listen(std::uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw resourcebroker::BrokerError(resourcebroker::ErrorCode::Persistence, "socket() failed");
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(s); throw resourcebroker::BrokerError(resourcebroker::ErrorCode::Persistence, "bind() failed"); }
    if (listen(s, SOMAXCONN) == SOCKET_ERROR) { closesocket(s); throw resourcebroker::BrokerError(resourcebroker::ErrorCode::Persistence, "listen() failed"); }
    return s;
}

inline SOCKET net_accept(SOCKET listen) {
    sockaddr_in from{};
    int len = static_cast<int>(sizeof(from));
    return accept(listen, reinterpret_cast<sockaddr*>(&from), &len);
}

inline SOCKET net_connect(const std::string& host, std::uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw resourcebroker::BrokerError(resourcebroker::ErrorCode::Persistence, "socket() failed");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { closesocket(s); throw resourcebroker::BrokerError(resourcebroker::ErrorCode::Persistence, "connect() failed"); }
    return s;
}

inline bool net_send_all(SOCKET s, const std::uint8_t* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        const int n = send(s, reinterpret_cast<const char*>(data + sent), static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

inline bool net_recv_exact(SOCKET s, std::uint8_t* data, std::size_t len) {
    std::size_t got = 0;
    while (got < len) {
        const int n = recv(s, reinterpret_cast<char*>(data + got), static_cast<int>(len - got), 0);
        if (n <= 0) return false;
        got += static_cast<std::size_t>(n);
    }
    return true;
}

inline void net_close(SOCKET s) { if (s != INVALID_SOCKET) closesocket(s); }

inline void net_send_frame(SOCKET s, resourcebroker::MessageKind kind, const std::vector<std::uint8_t>& payload) {
    const auto frame = resourcebroker::encode_frame(kind, payload);
    net_send_all(s, frame.data(), frame.size());
}

// Reads one frame from the socket; returns false on graceful close, throws on malformed.
inline bool net_recv_frame(SOCKET s, resourcebroker::MessageKind& kind, std::vector<std::uint8_t>& payload) {
    std::uint8_t hdr[16];
    if (!net_recv_exact(s, hdr, 4)) return false;  // 4 magic
    if (!net_recv_exact(s, hdr + 4, 2)) return false;  // version
    if (!net_recv_exact(s, hdr + 6, 2)) return false;  // kind
    if (!net_recv_exact(s, hdr + 8, 4)) return false;  // length
    if (!net_recv_exact(s, hdr + 12, 4)) return false; // crc
    const std::uint32_t plen = (std::uint32_t(hdr[8]) << 24) | (std::uint32_t(hdr[9]) << 16) | (std::uint32_t(hdr[10]) << 8) | std::uint32_t(hdr[11]);
    std::vector<std::uint8_t> buf(16 + plen);
    std::memcpy(buf.data(), hdr, 16);
    if (plen > 0 && !net_recv_exact(s, buf.data() + 16, plen)) return false;
    const auto df = resourcebroker::decode_frame(buf.data(), buf.size());
    kind = df.kind; payload = std::move(df.payload);
    return true;
}

}  // namespace rbnet
