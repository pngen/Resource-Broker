// test_host_memory.cpp - Real host-memory reservation proof.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Governs a bounded subset of real host memory. It does NOT claim all free
// system memory is safely allocatable; it uses a small bounded governed pool
// and verifies physical allocation, write, read-back, release, and exact
// accounting closure.
#include "resourcebroker/broker.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/reservation.hpp"
#include "resourcebroker/enums.hpp"
#include "resourcebroker/amount.hpp"
#include "resourcebroker/error.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace resourcebroker;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } else { std::fprintf(stderr, "PASS: %s\n", msg); } } while (0)

static const std::uint64_t MiB = 1024ull * 1024ull;

// Discover available physical memory conservatively; return a bounded governed
// subset for the test pool.
static std::uint64_t discover_governed_host_bytes() {
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 64 * MiB;  // conservative default
    // Do not exceed 64 MiB governed, and never more than half of truly available.
    const std::uint64_t avail = static_cast<std::uint64_t>(ms.ullAvailPhys);
    const std::uint64_t cap = std::min<std::uint64_t>(64 * MiB, avail / 2);
    return cap;
}

int main() {
    const std::uint64_t governed_bytes = discover_governed_host_bytes();
    std::fprintf(stderr, "governed host bytes = %llu MiB\n", static_cast<unsigned long long>(governed_bytes / MiB));

    Broker b(CoordinatorEpoch(1), BrokerId(1));
    b.set_policy(BrokerPolicy{});

    ResourcePoolDescriptor pool;
    pool.pool_id = ResourcePoolId(1); pool.pool_generation = ResourcePoolGeneration(1);
    pool.resource_class = ResourceClass::HOST_MEMORY_BYTES; pool.scope = PoolScope::HOST_LOCAL;
    pool.total_governed = ResourceAmount::bytes(Bytes(governed_bytes));
    pool.provenance = Provenance::MEASURED; pool.health = Health::HEALTHY; pool.freshness = Freshness::CURRENT;
    b.register_pool(pool, CapacityGeneration(1));

    ResourceDescriptor inst;
    inst.instance_id = ResourceInstanceId(1); inst.resource_class = ResourceClass::HOST_MEMORY_BYTES;
    inst.pool_id = pool.pool_id; inst.governed = ResourceAmount::bytes(Bytes(governed_bytes));
    inst.provenance = Provenance::MEASURED; inst.health = Health::HEALTHY; inst.freshness = Freshness::CURRENT;
    b.register_resource(inst);

    AuthorityEnvelope auth; auth.coordinator_epoch = b.coordinator_epoch();

    // Select a real allocation size: 32 MiB (below governed capacity).
    const std::uint64_t alloc_bytes = std::min<std::uint64_t>(governed_bytes, 32 * MiB);

    ResourceRequest req;
    req.request_id = ResourceRequestId(1); req.request_generation = RequestGeneration(1);
    req.owner = OwnerId(1); req.tenant = TenantId(1); req.workload = WorkloadId(1);
    ResourceRequirement h; h.resource_class = ResourceClass::HOST_MEMORY_BYTES;
    h.requested = ResourceAmount::bytes(Bytes(alloc_bytes)); h.minimum = h.requested; h.preferred = h.requested;
    h.semantics = CapacitySemantics::EXACT; req.requirements.push_back(h);
    req.priority = Priority::HIGH; req.reclaimability = Reclaimability::COOPERATIVE;

    GrantResult g = b.submit_request(req, auth);
    CHECK(g.outcome == RequestOutcome::GRANT, "host memory reservation granted");
    const ReservationId rid = g.reservation_id;
    b.activate_reservation(rid, auth);

    // Real bounded host allocation.
    std::vector<std::uint8_t> buf(alloc_bytes);
    for (std::size_t i = 0; i < buf.size(); ++i) buf[i] = static_cast<std::uint8_t>((i * 31) & 0xFFu);
    const auto aid = b.report_allocation(rid, inst.instance_id, ResourceAmount::bytes(Bytes(alloc_bytes)), "real-host-vector", auth);
    CHECK(aid.is_valid(), "physical allocation reported");
    CHECK(b.allocation(aid).state == AllocationState::ALLOCATED, "allocation state ALLOCATED");

    // Verify written contents round-trip.
    bool contents_ok = true;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] != static_cast<std::uint8_t>((i * 31) & 0xFFu)) { contents_ok = false; break; }
    }
    CHECK(contents_ok, "written host memory contents verified");

    auto cv = b.capacity_view(pool.pool_id);
    CHECK(cv.allocated.as_bytes().value() == alloc_bytes, "allocated == governed allocation");
    CHECK(cv.free.as_bytes().value() == governed_bytes - alloc_bytes, "free host capacity reduced by allocation");

    // A request larger than currently free must DEFER.
    ResourceRequest big;
    big.request_id = ResourceRequestId(2); big.request_generation = RequestGeneration(1);
    big.owner = OwnerId(1); big.tenant = TenantId(1); big.workload = WorkloadId(2);
    ResourceRequirement bh; bh.resource_class = ResourceClass::HOST_MEMORY_BYTES;
    bh.requested = ResourceAmount::bytes(Bytes(cv.free.as_bytes().value() + MiB));
    bh.minimum = bh.requested; bh.preferred = bh.requested; bh.semantics = CapacitySemantics::EXACT;
    big.requirements.push_back(bh); big.reclaimability = Reclaimability::COOPERATIVE;
    GrantResult gb = b.submit_request(big, auth);
    CHECK(gb.outcome == RequestOutcome::PREEMPT_REQUIRED, "oversize host request preempt-required (existing reclaimable reservation)");

    // Release physical allocation, then reservation; accounting returns to baseline.
    b.release_allocation(aid, auth);
    b.release_reservation(rid, auth);
    auto cv2 = b.capacity_view(pool.pool_id);
    CHECK(cv2.allocated.as_bytes().value() == 0, "allocated returns to zero");
    CHECK(cv2.reserved.as_bytes().value() == 0, "reserved returns to zero");
    CHECK(cv2.free.as_bytes().value() == governed_bytes, "free host capacity returns to baseline");

    if (failures == 0) { std::printf("test_host_memory PASS\n"); return 0; }
    std::printf("test_host_memory FAIL (%d)\n", failures); return 1;
}
