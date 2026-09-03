// worker.cpp - Resource Broker worker process (framed TCP client).
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "tcp.hpp"
#include "resourcebroker/protocol.hpp"
#include "resourcebroker/enums.hpp"
#include "resourcebroker/amount.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/error.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <string>
#include <vector>

using namespace resourcebroker;

static const std::uint64_t MiB = 1024ull * 1024ull;

struct Conn {
    SOCKET s;
    AuthorityEnvelope auth;
    WorkerBootId boot;
};

static bool send_and_recv(SOCKET s, MessageKind kind, const std::vector<std::uint8_t>& payload, MessageKind& out_kind, std::vector<std::uint8_t>& out_payload) {
    rbnet::net_send_frame(s, kind, payload);
    return rbnet::net_recv_frame(s, out_kind, out_payload);
}

static AuthorityEnvelope make_auth(const Conn& c) {
    AuthorityEnvelope a; a.coordinator_epoch = c.auth.coordinator_epoch; a.worker = c.auth.worker; a.worker_boot = c.boot;
    return a;
}

static ResourceDescriptor host_res(std::uint64_t bytes, ResourceInstanceId id, ResourcePoolId pool) {
    ResourceDescriptor r; r.instance_id = id; r.pool_id = pool; r.resource_class = ResourceClass::HOST_MEMORY_BYTES;
    r.governed = ResourceAmount::bytes(Bytes(bytes)); r.nominal = r.governed;
    r.available = ResourceAmount::bytes(Bytes(0)); r.reserved = ResourceAmount::bytes(Bytes(0)); r.allocated = ResourceAmount::bytes(Bytes(0)); r.reclaimable = ResourceAmount::bytes(Bytes(0));
    r.provenance = Provenance::MEASURED; r.health = Health::HEALTHY; r.freshness = Freshness::CURRENT;
    r.resource_class = ResourceClass::HOST_MEMORY_BYTES; r.resource_generation = ResourceGeneration(1); r.capacity_generation = CapacityGeneration(1);
    return r;
}
static ResourceDescriptor exec_res(Count slots, ResourceInstanceId id, ResourcePoolId pool) {
    ResourceDescriptor r; r.instance_id = id; r.pool_id = pool; r.resource_class = ResourceClass::EXECUTION_SLOTS;
    r.governed = ResourceAmount::count(slots); r.nominal = r.governed;
    r.available = ResourceAmount::count(Count(0)); r.reserved = ResourceAmount::count(Count(0)); r.allocated = ResourceAmount::count(Count(0)); r.reclaimable = ResourceAmount::count(Count(0));
    r.provenance = Provenance::DERIVED; r.health = Health::HEALTHY; r.freshness = Freshness::CURRENT;
    r.resource_generation = ResourceGeneration(1); r.capacity_generation = CapacityGeneration(1);
    return r;
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 4299;
    std::string scenario = "A";
    std::uint64_t old_boot = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) scenario = argv[++i];
        else if (std::strcmp(argv[i], "--old-boot") == 0 && i + 1 < argc) old_boot = std::strtoull(argv[++i], nullptr, 10);
    }
    if (!rbnet::net_init()) { std::fprintf(stderr, "WSAStartup failed\n"); return 1; }
    SOCKET s;
    try { s = rbnet::net_connect(host, port); }
    catch (const BrokerError& e) { std::fprintf(stderr, "connect failed: %s\n", e.what()); rbnet::net_cleanup(); return 1; }

    MessageKind kind; std::vector<std::uint8_t> payload;
    // HELLO
    const WorkerId wid(std::strtoull(scenario.c_str(), nullptr, 10) + 1000);
    if (!send_and_recv(s, MessageKind::HELLO, encode_hello(wid), kind, payload)) { std::fprintf(stderr, "hello failed\n"); return 1; }
    const HelloResult hello = decode_hello_result(payload);
    Conn c; c.s = s; c.boot = hello.boot; c.auth.coordinator_epoch = hello.epoch; c.auth.worker = wid;

    if (scenario == "A") {
        // register host + exec resources
        std::fprintf(stdout, "WORKER_A_BOOT=%llu\n", static_cast<unsigned long long>(c.boot.value())); std::fflush(stdout);
        auto hr = host_res(32 * MiB, ResourceInstanceId(100), ResourcePoolId(100));
        auto er = exec_res(Count(4), ResourceInstanceId(200), ResourcePoolId(200));
        send_and_recv(s, MessageKind::REGISTER_RESOURCE, encode_register_resource(hr, c.auth), kind, payload);
        send_and_recv(s, MessageKind::REGISTER_RESOURCE, encode_register_resource(er, c.auth), kind, payload);
        // request 16MiB host + 1 slot
        ResourceRequest req; req.request_id = ResourceRequestId(1001); req.request_generation = RequestGeneration(1);
        req.owner = OwnerId(7); req.tenant = TenantId(7); req.workload = WorkloadId(700); req.priority = Priority::HIGH;
        ResourceRequirement h1; h1.resource_class = ResourceClass::HOST_MEMORY_BYTES; h1.requested = ResourceAmount::bytes(Bytes(16 * MiB)); h1.minimum = h1.requested; h1.preferred = h1.requested; h1.semantics = CapacitySemantics::EXACT;
        ResourceRequirement e1; e1.resource_class = ResourceClass::EXECUTION_SLOTS; e1.requested = ResourceAmount::count(Count(1)); e1.minimum = e1.requested; e1.preferred = e1.requested; e1.semantics = CapacitySemantics::EXACT;
        req.requirements.push_back(h1); req.requirements.push_back(e1); req.reclaimability = Reclaimability::COOPERATIVE;
        if (!send_and_recv(s, MessageKind::REQUEST_RESOURCES, encode_request(req, make_auth(c)), kind, payload)) return 1;
        const WireResult gr = decode_grant(payload);
        std::fprintf(stdout, "WORKER_A_GRANT=%d rid=%llu\n", static_cast<int>(gr.outcome), static_cast<unsigned long long>(gr.reservation_id.value())); std::fflush(stdout);
        // activate + allocate host
        if (gr.outcome == RequestOutcome::GRANT) {
            send_and_recv(s, MessageKind::ACTIVATE_RESERVATION, encode_activate(gr.reservation_id, make_auth(c)), kind, payload);
            send_and_recv(s, MessageKind::REPORT_ALLOCATION, encode_report_allocation(gr.reservation_id, ResourceInstanceId(100), ResourceClass::HOST_MEMORY_BYTES, ResourceAmount::bytes(Bytes(16 * MiB)), "hostbuf", make_auth(c)), kind, payload);
            std::fprintf(stdout, "WORKER_A_READY\n"); std::fflush(stdout);
            // stay alive so the coordinator can observe loss on kill
            std::this_thread::sleep_for(std::chrono::seconds(20));
        }
    } else if (scenario == "B") {
        std::fprintf(stdout, "WORKER_B_BOOT=%llu\n", static_cast<unsigned long long>(c.boot.value())); std::fflush(stdout);
        ResourceRequest req; req.request_id = ResourceRequestId(2001); req.request_generation = RequestGeneration(1);
        req.owner = OwnerId(8); req.tenant = TenantId(8); req.workload = WorkloadId(800); req.priority = Priority::NORMAL;
        ResourceRequirement h1; h1.resource_class = ResourceClass::HOST_MEMORY_BYTES; h1.requested = ResourceAmount::bytes(Bytes(24 * MiB)); h1.minimum = h1.requested; h1.preferred = h1.requested; h1.semantics = CapacitySemantics::EXACT;
        ResourceRequirement e1; e1.resource_class = ResourceClass::EXECUTION_SLOTS; e1.requested = ResourceAmount::count(Count(1)); e1.minimum = e1.requested; e1.preferred = e1.requested; e1.semantics = CapacitySemantics::EXACT;
        req.requirements.push_back(h1); req.requirements.push_back(e1); req.reclaimability = Reclaimability::COOPERATIVE;
        if (!send_and_recv(s, MessageKind::REQUEST_RESOURCES, encode_request(req, make_auth(c)), kind, payload)) return 1;
        const WireResult gr = decode_grant(payload);
        std::fprintf(stdout, "WORKER_B_RESULT=%d\n", static_cast<int>(gr.outcome)); std::fflush(stdout);
    } else if (scenario == "A_STALE") {
        std::fprintf(stdout, "WORKER_A2_BOOT=%llu\n", static_cast<unsigned long long>(c.boot.value())); std::fflush(stdout);
        // Stale release: use the OLD boot that the coordinator already marked dead.
        AuthorityEnvelope stale = make_auth(c); stale.worker_boot = WorkerBootId(old_boot);
        if (!send_and_recv(s, MessageKind::RELEASE, encode_release(ReservationId(1), stale), kind, payload)) return 1;
        const int stale_rel = (kind == MessageKind::ERROR) ? 1 : 0;
        std::fprintf(stdout, "WORKER_STALE_RELEASE_REJECTED=%d\n", stale_rel); std::fflush(stdout);
        // Fresh request under current authority.
        ResourceRequest req; req.request_id = ResourceRequestId(3001); req.request_generation = RequestGeneration(1);
        req.owner = OwnerId(7); req.tenant = TenantId(7); req.workload = WorkloadId(900); req.priority = Priority::HIGH;
        ResourceRequirement h1; h1.resource_class = ResourceClass::HOST_MEMORY_BYTES; h1.requested = ResourceAmount::bytes(Bytes(8 * MiB)); h1.minimum = h1.requested; h1.preferred = h1.requested; h1.semantics = CapacitySemantics::EXACT;
        req.requirements.push_back(h1); req.reclaimability = Reclaimability::RECLAIMABLE;
        if (!send_and_recv(s, MessageKind::REQUEST_RESOURCES, encode_request(req, make_auth(c)), kind, payload)) return 1;
        const WireResult gr = decode_grant(payload);
        std::fprintf(stdout, "WORKER_A2_FRESH_GRANT=%d\n", static_cast<int>(gr.outcome)); std::fflush(stdout);
    } else {
        std::fprintf(stderr, "unknown scenario %s\n", scenario.c_str());
    }
    rbnet::net_close(s);
    rbnet::net_cleanup();
    return 0;
}
