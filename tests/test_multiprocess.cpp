// test_multiprocess.cpp - Real OS-process multiprocess authority proof.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "tcp.hpp"
#include "resourcebroker/protocol.hpp"
#include "resourcebroker/authority.hpp"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } else { std::fprintf(stderr, "PASS: %s\n", msg); } } while (0)

struct Child {
    HANDLE h = nullptr;
    HANDLE pipeRead = nullptr;
    bool alive = false;
};

// Start a child process with optional stdout capture.
static bool start_program(const std::string& path, const std::string& args, bool capture, Child& out) {
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE readp = nullptr, writep = nullptr;
    if (capture && !CreatePipe(&readp, &writep, &sa, 0)) return false;
    if (capture) SetHandleInformation(readp, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{}; si.cb = sizeof(si);
    if (capture) { si.dwFlags |= STARTF_USESTDHANDLES; si.hStdOutput = writep; si.hStdError = writep; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); }
    PROCESS_INFORMATION pi{};
    std::string cmdline = std::string("\"") + path + "\" " + args;
    std::vector<char> buf(cmdline.begin(), cmdline.end()); buf.push_back('\0');
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, capture ? TRUE : FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (capture) { CloseHandle(readp); CloseHandle(writep); }
        return false;
    }
    if (capture) CloseHandle(writep);
    out.h = pi.hProcess; out.pipeRead = readp; out.alive = true;
    CloseHandle(pi.hThread);
    return true;
}

// Read from the child's stdout until a marker (or EOF / timeout). Returns lines seen.
static std::string read_until_marker(Child& c, const std::string& marker, int timeout_ms) {
    std::string acc;
    char ch;
    DWORD got = 0;
    DWORD last = GetTickCount();
    while (true) {
        DWORD avail = 0;
        if (c.pipeRead && PeekNamedPipe(c.pipeRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            ReadFile(c.pipeRead, &ch, 1, &got, nullptr);
            if (got == 1) { acc.push_back(ch); }
        } else if (acc.find(marker) != std::string::npos) {
            return acc;
        } else if (GetTickCount() - last > static_cast<DWORD>(timeout_ms)) {
            return acc;
        } else {
            if (WaitForSingleObject(c.h, 0) == WAIT_OBJECT_0) {
                // process exited; drain remaining
                while (c.pipeRead && PeekNamedPipe(c.pipeRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                    ReadFile(c.pipeRead, &ch, 1, &got, nullptr);
                    if (got == 1) acc.push_back(ch);
                }
                return acc;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

static bool wait_exit(Child& c, int timeout_ms) {
    DWORD r = WaitForSingleObject(c.h, static_cast<DWORD>(timeout_ms));
    return r == WAIT_OBJECT_0;
}

static void terminate(Child& c) {
    if (c.h) { TerminateProcess(c.h, 0); CloseHandle(c.h); c.h = nullptr; }
    if (c.pipeRead) { CloseHandle(c.pipeRead); c.pipeRead = nullptr; }
    c.alive = false;
}

static std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> v; std::string cur;
    for (char ch : s) { if (ch == '\n' || ch == '\r') { if (!cur.empty()) v.push_back(cur); cur.clear(); } else cur.push_back(ch); }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

static std::uint64_t parse_value(const std::string& s, const std::string& key) {
    const auto p = s.find(key);
    if (p == std::string::npos) return 0;
    const auto val = s.substr(p + key.size());
    return std::strtoull(val.c_str(), nullptr, 10);
}

static void save_state(int port) {
    if (!rbnet::net_init()) return;
    SOCKET s = INVALID_SOCKET;
    try { s = rbnet::net_connect("127.0.0.1", static_cast<std::uint16_t>(port)); } catch (...) { return; }
    rbnet::net_send_frame(s, resourcebroker::MessageKind::HELLO, resourcebroker::encode_hello(resourcebroker::WorkerId(9999)));
    resourcebroker::MessageKind k; std::vector<std::uint8_t> p; rbnet::net_recv_frame(s, k, p);
    rbnet::net_send_frame(s, resourcebroker::MessageKind::SAVE, {});
    rbnet::net_recv_frame(s, k, p);
    rbnet::net_close(s);
    rbnet::net_cleanup();
}

int main(int argc, char** argv) {
    const std::string coord = argc > 1 ? argv[1] : "resource-broker-coordinator.exe";
    const std::string worker = argc > 2 ? argv[2] : "resource-broker-worker.exe";
    const int port = argc > 3 ? std::atoi(argv[3]) : 4319;
    const std::string state = "./mp_state.rbstate";

    // 1. Start coordinator.
    Child coord_c;
    if (!start_program(coord, "--port " + std::to_string(port) + " --state " + state, false, coord_c)) {
        std::fprintf(stderr, "coordinator failed to start\n"); return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // 2. Worker A: register host+exec, request, activate, allocate. Capture boot.
    Child a;
    if (!start_program(worker, "--port " + std::to_string(port) + " --scenario A", true, a)) {
        std::fprintf(stderr, "worker A failed to start\n"); terminate(coord_c); return 1;
    }
    const std::string a_out = read_until_marker(a, "WORKER_A_READY", 8000);
    const auto a_lines = lines(a_out);
    std::uint64_t a_boot = 0, a_grant = 999;
    for (const auto& l : a_lines) {
        if (l.rfind("WORKER_A_BOOT=", 0) == 0) a_boot = parse_value(l, "WORKER_A_BOOT=");
        if (l.rfind("WORKER_A_GRANT=", 0) == 0) a_grant = parse_value(l, "WORKER_A_GRANT=");
    }
    CHECK(a_boot != 0, "worker A registered with a WorkerBootId");
    CHECK(a_grant == 0, "worker A multi-resource request granted");

    // 3. Worker B: competing request -> expect DEFER (value 3: DEFER).
    Child b;
    start_program(worker, "--port " + std::to_string(port) + " --scenario B", true, b);
    const std::string b_out = read_until_marker(b, "WORKER_B_RESULT=", 8000);
    std::uint64_t b_result = 999;
    for (const auto& l : lines(b_out)) if (l.rfind("WORKER_B_RESULT=", 0) == 0) b_result = parse_value(l, "WORKER_B_RESULT=");
    CHECK(b_result == 4, "worker B competing request preempt-required");
    wait_exit(b, 3000); terminate(b);

    // 4. Kill Worker A as a real OS process.
    terminate(a);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    // 5. Worker A restarted with a FRESH WorkerBootId replays stale authority.
    Child a2;
    start_program(worker, "--port " + std::to_string(port) + " --scenario A_STALE --old-boot " + std::to_string(a_boot), true, a2);
    const std::string a2_out = read_until_marker(a2, "WORKER_A2_FRESH_GRANT=", 8000);
    const auto a2_lines = lines(a2_out);
    std::uint64_t stale_rejected = 0, fresh_grant = 999;
    for (const auto& l : a2_lines) {
        if (l.rfind("WORKER_STALE_RELEASE_REJECTED=", 0) == 0) stale_rejected = parse_value(l, "WORKER_STALE_RELEASE_REJECTED=");
        if (l.rfind("WORKER_A2_FRESH_GRANT=", 0) == 0) fresh_grant = parse_value(l, "WORKER_A2_FRESH_GRANT=");
    }
    CHECK(stale_rejected == 1, "stale WorkerBootId release rejected");
    CHECK(fresh_grant == 0, "fresh post-restart Worker A request granted");
    wait_exit(a2, 3000); terminate(a2);

    // 6. Persist broker state, then restart the coordinator and recover.
    save_state(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    terminate(coord_c);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    Child coord2;
    start_program(coord, "--port " + std::to_string(port) + " --epoch 100 --state " + state, false, coord2);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Fresh worker after recovery re-registers and obtains capacity.
    Child a3;
    start_program(worker, "--port " + std::to_string(port) + " --scenario A_STALE --old-boot 0", true, a3);
    const std::string a3_out = read_until_marker(a3, "WORKER_A2_FRESH_GRANT=", 8000);
    std::uint64_t fresh_grant2 = 999;
    for (const auto& l : lines(a3_out)) if (l.rfind("WORKER_A2_FRESH_GRANT=", 0) == 0) fresh_grant2 = parse_value(l, "WORKER_A2_FRESH_GRANT=");
    CHECK(fresh_grant2 == 0, "fresh request after coordinator recovery granted");
    wait_exit(a3, 3000); terminate(a3);

    terminate(coord2);
    std::remove(state.c_str());
    std::remove((state + ".tmp").c_str());

    if (failures == 0) { std::printf("test_multiprocess PASS\n"); return 0; }
    std::printf("test_multiprocess FAIL (%d)\n", failures);
    return 1;
}
