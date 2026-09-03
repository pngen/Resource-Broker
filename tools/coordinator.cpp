// coordinator.cpp - Resource Broker coordinator process (framed TCP).
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "tcp.hpp"
#include "resourcebroker/broker.hpp"
#include "resourcebroker/protocol.hpp"
#include "resourcebroker/enums.hpp"
#include "resourcebroker/amount.hpp"
#include "resourcebroker/error.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <string>
#include <vector>

using namespace resourcebroker;

static std::string state_path;
static bool g_run = true;

static std::string capacity_summary(Broker& b, ResourcePoolId pool) {
    try {
        const auto cv = b.capacity_view(pool);
        const auto cls = b.pool(pool).resource_class;
        std::ostringstream oss;
        oss << "capacity " << pool.value() << " class=" << to_string(cls)
            << " governed=" << cv.governed.to_string()
            << " reserved=" << cv.reserved.to_string()
            << " allocated=" << cv.allocated.to_string()
            << " free=" << cv.free.to_string();
        return oss.str();
    } catch (const BrokerError& e) {
        return std::string("error: ") + e.what();
    }
}

static void handle_client(Broker& b, SOCKET sock) {
    WorkerBootId current_boot;
    while (g_run) {
        MessageKind kind;
        std::vector<std::uint8_t> payload;
        try {
            if (!rbnet::net_recv_frame(sock, kind, payload)) break;
        } catch (const BrokerError& e) {
            (void)e;
            break;
        }
        std::string ack;
        try {
            switch (kind) {
                case MessageKind::HELLO: {
                    const auto h = decode_hello(payload);
                    current_boot = b.register_worker(h.worker);
                    rbnet::net_send_frame(sock, MessageKind::ACK, encode_hello_result(current_boot, b.coordinator_epoch()));
                    continue;
                }
                case MessageKind::REGISTER_RESOURCE: {
                    const auto r = decode_register_resource(payload);
                    // Ensure a pool exists for this resource class/pool id.
                    if (!b.pool_for_class(r.res.resource_class).has_value()) {
                        ResourcePoolDescriptor pd;
                        pd.pool_id = r.res.pool_id;
                        if (!pd.pool_id.is_valid()) pd.pool_id = ResourcePoolId(r.res.pool_id.value() ? r.res.pool_id.value() : 1);
                        pd.pool_generation = ResourcePoolGeneration(1);
                        pd.resource_class = r.res.resource_class;
                        pd.scope = (r.res.resource_class == ResourceClass::GPU_MEMORY_BYTES) ? PoolScope::DEVICE_LOCAL
                                : (r.res.resource_class == ResourceClass::EXECUTION_SLOTS) ? PoolScope::NODE_LOCAL
                                : PoolScope::HOST_LOCAL;
                        pd.total_governed = r.res.governed;
                        pd.provenance = r.res.provenance;
                        pd.health = r.res.health;
                        pd.freshness = r.res.freshness;
                        b.register_pool(pd, CapacityGeneration(1));
                    }
                    b.register_resource(r.res);
                    rbnet::net_send_frame(sock, MessageKind::ACK, encode_ack("registered"));
                    continue;
                }
                case MessageKind::REQUEST_RESOURCES: {
                    const auto rd = decode_request(payload);
                    const GrantResult gr = b.submit_request(rd.req, rd.auth);
                    WireResult wr; wr.outcome = gr.outcome; wr.reservation_id = gr.reservation_id;
                    wr.message = gr.explanation.headline;
                    rbnet::net_send_frame(sock, MessageKind::GRANT_RESULT, encode_grant(wr));
                    continue;
                }
                case MessageKind::ACTIVATE_RESERVATION: {
                    const auto d = decode_activate(payload);
                    b.activate_reservation(d.res, d.auth);
                    rbnet::net_send_frame(sock, MessageKind::ACK, encode_ack("activated"));
                    continue;
                }
                case MessageKind::REPORT_ALLOCATION: {
                    const auto d = decode_report_allocation(payload);
                    const AllocationId aid = b.report_allocation(d.res, d.instance, d.amount, d.evidence, d.auth);
                    rbnet::net_send_frame(sock, MessageKind::ACK, encode_ack(std::to_string(aid.value())));
                    continue;
                }
                case MessageKind::RELEASE: {
                    const auto d = decode_release(payload);
                    b.release_reservation(d.res, d.auth);
                    rbnet::net_send_frame(sock, MessageKind::ACK, encode_ack("released"));
                    continue;
                }
                case MessageKind::RENEW_LEASE: {
                    const auto d = decode_renew_lease(payload);
                    b.renew_lease(d.lease, d.auth);
                    rbnet::net_send_frame(sock, MessageKind::ACK, encode_ack("renewed"));
                    continue;
                }
                case MessageKind::QUERY: {
                    const auto q = decode_query(payload);
                    std::string result;
                    if (q.what.rfind("capacity:", 0) == 0) {
                        const auto pool = ResourcePoolId(std::strtoull(q.what.c_str() + 9, nullptr, 10));
                        result = capacity_summary(b, pool);
                    } else if (q.what == "reservations") {
                        std::ostringstream oss;
                        for (const auto& rsv : b.all_reservations()) {
                            oss << "res " << rsv.reservation_id.value() << " state=" << to_string(rsv.state) << " owner=" << rsv.owner.value() << "\n";
                        }
                        result = oss.str();
                    } else if (q.what == "workers") {
                        std::ostringstream oss;
                        for (const auto& boot : b.live_workers()) { oss << "boot=" << boot.value() << "\n"; }
                        result = oss.str();
                    } else { result = "unknown query"; }
                    rbnet::net_send_frame(sock, MessageKind::QUERY_RESULT, encode_query_result(result));
                    continue;
                }
                case MessageKind::SAVE: {
                    if (!state_path.empty()) { b.save(state_path); ack = "saved"; }
                    else ack = "no-state-path";
                    rbnet::net_send_frame(sock, MessageKind::ACK, encode_ack(ack));
                    continue;
                }
                case MessageKind::RECOVER: {
                    if (!state_path.empty()) { b.load(state_path); ack = "loaded"; }
                    else ack = "no-state-path";
                    rbnet::net_send_frame(sock, MessageKind::ACK, encode_ack(ack));
                    continue;
                }
                default: {
                    rbnet::net_send_frame(sock, MessageKind::ERROR, encode_ack("unsupported message"));
                    continue;
                }
            }
        } catch (const BrokerError& e) {
            std::string err = std::string(e.code_string()) + ":" + e.what();
            rbnet::net_send_frame(sock, MessageKind::ERROR, encode_ack(err));
        }
    }
    // Connection closed: mark the worker dead and reclaim its reservations.
    if (current_boot.is_valid()) {
        try { b.mark_worker_dead(current_boot); } catch (...) {}
        try {
            for (const auto& rsv : b.all_reservations()) {
                if (rsv.authority.worker_boot == current_boot) {
                    b.release_reservation(rsv.reservation_id, rsv.authority);
                }
            }
        } catch (...) {}
    }
    rbnet::net_close(sock);
}

int main(int argc, char** argv) {
    std::uint16_t port = 4299;
    CoordinatorEpoch epoch(1);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--epoch") == 0 && i + 1 < argc) epoch = CoordinatorEpoch(std::strtoull(argv[++i], nullptr, 10));
        else if (std::strcmp(argv[i], "--state") == 0 && i + 1 < argc) state_path = argv[++i];
    }
    if (!rbnet::net_init()) { std::fprintf(stderr, "WSAStartup failed\n"); return 1; }
    SOCKET listen = INVALID_SOCKET;
    try { listen = rbnet::net_listen(port); }
    catch (const BrokerError& e) { std::fprintf(stderr, "listen failed: %s\n", e.what()); rbnet::net_cleanup(); return 1; }
    std::fprintf(stderr, "coordinator listening on 127.0.0.1:%u epoch=%llu\n", static_cast<unsigned>(port), static_cast<unsigned long long>(epoch.value()));
    std::fflush(stderr);

    Broker b(epoch, BrokerId(1));
    BrokerPolicy pol; pol.policy_id = PolicyId(1); pol.policy_generation = PolicyGeneration(1);
    pol.auto_reclaim_dead_worker_reservations = true;
    b.set_policy(pol);

    // Auto-recover persisted state if present (a fresh coordinator never
    // trusts prior incarnation liveness; recovery handles that).
    if (!state_path.empty()) { try { b.load(state_path); } catch (...) {} }

    std::vector<std::thread> threads;
    std::vector<SOCKET> socks;
    bool accepted = true;
    while (accepted) {
        SOCKET c = rbnet::net_accept(listen);
        if (c == INVALID_SOCKET) break;
        socks.push_back(c);
        threads.emplace_back([&b, c]() { handle_client(b, c); });
    }
    for (auto& t : threads) if (t.joinable()) t.join();
    rbnet::net_close(listen);
    rbnet::net_cleanup();
    return 0;
}
