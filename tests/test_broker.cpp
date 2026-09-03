// test_broker.cpp - End-to-end Broker functional validation.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/broker.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/reservation.hpp"
#include "resourcebroker/enums.hpp"
#include "resourcebroker/amount.hpp"
#include "resourcebroker/error.hpp"
#include <cstdio>
#include <string>
#include <fstream>
#include <iterator>
#include <filesystem>

using namespace resourcebroker;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } else { std::fprintf(stderr, "pass: %s\n", msg); } } while (0)

static const std::uint64_t GiB = 1024ull * 1024ull * 1024ull;
static const std::uint64_t MiB = 1024ull * 1024ull;

static AuthorityEnvelope auth_for(Broker& b) {
    AuthorityEnvelope a;
    a.coordinator_epoch = b.coordinator_epoch();
    return a;
}

int main() {
    Broker b(CoordinatorEpoch(1), BrokerId(1));
    BrokerPolicy pol; pol.policy_id = PolicyId(1); pol.policy_generation = PolicyGeneration(1);
    b.set_policy(pol);

    // Pools.
    ResourcePoolDescriptor gpu;
    gpu.pool_id = ResourcePoolId(1); gpu.pool_generation = ResourcePoolGeneration(1);
    gpu.resource_class = ResourceClass::GPU_MEMORY_BYTES; gpu.scope = PoolScope::DEVICE_LOCAL;
    gpu.total_governed = ResourceAmount::bytes(Bytes(8 * GiB));
    gpu.provenance = Provenance::MEASURED; gpu.health = Health::HEALTHY; gpu.freshness = Freshness::CURRENT;
    b.register_pool(gpu, CapacityGeneration(1));

    ResourcePoolDescriptor pin;
    pin.pool_id = ResourcePoolId(2); pin.pool_generation = ResourcePoolGeneration(1);
    pin.resource_class = ResourceClass::HOST_PINNED_BYTES; pin.scope = PoolScope::HOST_LOCAL;
    pin.total_governed = ResourceAmount::bytes(Bytes(512 * MiB));
    pin.provenance = Provenance::MEASURED; pin.health = Health::HEALTHY; pin.freshness = Freshness::CURRENT;
    b.register_pool(pin, CapacityGeneration(1));

    ResourcePoolDescriptor ex;
    ex.pool_id = ResourcePoolId(3); ex.pool_generation = ResourcePoolGeneration(1);
    ex.resource_class = ResourceClass::EXECUTION_SLOTS; ex.scope = PoolScope::NODE_LOCAL;
    ex.total_governed = ResourceAmount::count(Count(8));
    ex.provenance = Provenance::DERIVED; ex.health = Health::HEALTHY; ex.freshness = Freshness::CURRENT;
    b.register_pool(ex, CapacityGeneration(1));

    // Instances.
    ResourceDescriptor gpuInst;
    gpuInst.instance_id = ResourceInstanceId(1); gpuInst.resource_class = ResourceClass::GPU_MEMORY_BYTES;
    gpuInst.pool_id = gpu.pool_id; gpuInst.governed = ResourceAmount::bytes(Bytes(8 * GiB));
    gpuInst.provenance = Provenance::MEASURED; gpuInst.health = Health::HEALTHY; gpuInst.freshness = Freshness::CURRENT;
    b.register_resource(gpuInst);

    ResourceDescriptor pinInst;
    pinInst.instance_id = ResourceInstanceId(2); pinInst.resource_class = ResourceClass::HOST_PINNED_BYTES;
    pinInst.pool_id = pin.pool_id; pinInst.governed = ResourceAmount::bytes(Bytes(512 * MiB));
    pinInst.provenance = Provenance::MEASURED; pinInst.health = Health::HEALTHY; pinInst.freshness = Freshness::CURRENT;
    b.register_resource(pinInst);

    ResourceDescriptor exInst;
    exInst.instance_id = ResourceInstanceId(3); exInst.resource_class = ResourceClass::EXECUTION_SLOTS;
    exInst.pool_id = ex.pool_id; exInst.governed = ResourceAmount::count(Count(8));
    exInst.provenance = Provenance::DERIVED; exInst.health = Health::HEALTHY; exInst.freshness = Freshness::CURRENT;
    b.register_resource(exInst);

    const AuthorityEnvelope auth = auth_for(b);

    // Multi-resource atomic request.
    ResourceRequest req;
    req.request_id = ResourceRequestId(1); req.request_generation = RequestGeneration(1);
    req.owner = OwnerId(1); req.tenant = TenantId(1); req.workload = WorkloadId(1);
    ResourceRequirement r1; r1.resource_class = ResourceClass::GPU_MEMORY_BYTES; r1.requested = ResourceAmount::bytes(Bytes(4 * GiB)); r1.minimum = r1.requested; r1.preferred = r1.requested; r1.semantics = CapacitySemantics::EXACT;
    ResourceRequirement r2; r2.resource_class = ResourceClass::HOST_PINNED_BYTES; r2.requested = ResourceAmount::bytes(Bytes(256 * MiB)); r2.minimum = r2.requested; r2.preferred = r2.requested; r2.semantics = CapacitySemantics::EXACT;
    ResourceRequirement r3; r3.resource_class = ResourceClass::EXECUTION_SLOTS; r3.requested = ResourceAmount::count(Count(2)); r3.minimum = r3.requested; r3.preferred = r3.requested; r3.semantics = CapacitySemantics::EXACT;
    req.requirements.push_back(r1); req.requirements.push_back(r2); req.requirements.push_back(r3);
    req.priority = Priority::HIGH; req.reclaimability = Reclaimability::COOPERATIVE;

    GrantResult g = b.submit_request(req, auth);
    CHECK(g.outcome == RequestOutcome::GRANT, "multi-resource request granted");
    CHECK(g.reservation_id.is_valid(), "reservation id assigned");
    ReservationId rid = g.reservation_id;

    ReservationDescriptor rd = b.reservation(rid);
    CHECK(rd.state == ReservationState::RESERVED, "reservation in RESERVED state");
    CHECK(rd.claims.size() == 3, "reservation has 3 claims");

    auto cvGpu = b.capacity_view(gpu.pool_id);
    CHECK(cvGpu.reserved.as_bytes().value() == 4 * GiB, "GPU pool reserved 4GiB");
    CHECK(cvGpu.free.as_bytes().value() == 4 * GiB, "GPU pool free 4GiB");

    b.activate_reservation(rid, auth);
    CHECK(b.reservation(rid).state == ReservationState::ACTIVE, "reservation active");

    // Physical allocation on GPU instance.
    AllocationId aid = b.report_allocation(rid, gpuInst.instance_id, ResourceAmount::bytes(Bytes(4 * GiB)), "cudaMalloc-ok", auth);
    CHECK(aid.is_valid(), "allocation created");
    auto cvGpu2 = b.capacity_view(gpu.pool_id);
    CHECK(cvGpu2.allocated.as_bytes().value() == 4 * GiB, "GPU pool allocated 4GiB after allocation");
    CHECK(cvGpu2.free.as_bytes().value() == 4 * GiB, "GPU pool still free 4GiB (reserved->allocated)");

    // Atomic rollback: request GPU 2GiB + pinned 1GiB (pinned too big).
    ResourceRequest reqB;
    reqB.request_id = ResourceRequestId(2); reqB.request_generation = RequestGeneration(1);
    reqB.owner = OwnerId(1); reqB.tenant = TenantId(1); reqB.workload = WorkloadId(2); reqB.priority = Priority::NORMAL;
    ResourceRequirement rqb1; rqb1.resource_class = ResourceClass::GPU_MEMORY_BYTES; rqb1.requested = ResourceAmount::bytes(Bytes(2 * GiB)); rqb1.minimum = rqb1.requested; rqb1.preferred = rqb1.requested; rqb1.semantics = CapacitySemantics::EXACT;
    ResourceRequirement rqb2; rqb2.resource_class = ResourceClass::HOST_PINNED_BYTES; rqb2.requested = ResourceAmount::bytes(Bytes(1024 * MiB)); rqb2.minimum = rqb2.requested; rqb2.preferred = rqb2.requested; rqb2.semantics = CapacitySemantics::EXACT;
    reqB.requirements.push_back(rqb1); reqB.requirements.push_back(rqb2);
    const auto gpuFreeBefore = b.capacity_view(gpu.pool_id).free.as_bytes().value();
    GrantResult gB = b.submit_request(reqB, auth);
    CHECK(gB.outcome == RequestOutcome::DEFER, "atomic request defers on pinned dimension");
    const auto gpuFreeAfter = b.capacity_view(gpu.pool_id).free.as_bytes().value();
    CHECK(gpuFreeAfter == gpuFreeBefore, "GPU provisional hold rolled back after pinned failure");

    // Release reservation returns all capacity.
    b.release_reservation(rid, auth);
    auto cvGpu3 = b.capacity_view(gpu.pool_id);
    CHECK(cvGpu3.allocated.as_bytes().value() == 0, "GPU allocated returned after release");
    CHECK(cvGpu3.reserved.as_bytes().value() == 0, "GPU reserved returned after release");
    CHECK(cvGpu3.free.as_bytes().value() == 8 * GiB, "GPU free returns to baseline");

    // Persistence round-trip.
    const std::string path = (std::filesystem::path(__FILE__).parent_path() / "test_state.rbstate").string();
    std::fprintf(stderr, "saving...\n");
    try { b.save(path); std::fprintf(stderr, "saved ok\n"); } catch (const BrokerError& e) { std::fprintf(stderr, "save threw %s: %s\n", e.code_string(), e.what()); ++failures; }
    Broker b2(CoordinatorEpoch(2), BrokerId(1));
    std::fprintf(stderr, "loading...\n");
    try { b2.load(path); std::fprintf(stderr, "loaded ok\n"); } catch (const BrokerError& e) { std::fprintf(stderr, "load threw %s: %s\n", e.code_string(), e.what()); ++failures; }
    CHECK(b2.pool_for_class(ResourceClass::GPU_MEMORY_BYTES).has_value(), "loaded GPU pool present");
    auto cvR = b2.capacity_view(gpu.pool_id);
    CHECK(cvR.free.as_bytes().value() == 8 * GiB, "loaded accounting baseline");

    // Deterministic round-trip digest stability via re-save + compare.
    const auto path1 = (std::filesystem::path(__FILE__).parent_path() / "test_state.rbstate").string();
    const auto path2 = (std::filesystem::path(__FILE__).parent_path() / "test_state2.rbstate").string();
    try { b.save(path2); } catch (const BrokerError& e) { std::fprintf(stderr, "save2 threw %s: %s\n", e.code_string(), e.what()); ++failures; }
    std::ifstream f1(path1, std::ios::binary);
    std::string s1((std::istreambuf_iterator<char>(f1)), std::istreambuf_iterator<char>());
    std::ifstream f2(path2, std::ios::binary);
    std::string s2((std::istreambuf_iterator<char>(f2)), std::istreambuf_iterator<char>());
    CHECK(s1 == s2, "deterministic persistence byte-identical");

    if (failures == 0) { std::printf("test_broker PASS\n"); return 0; }
    std::printf("test_broker FAIL (%d)\n", failures); return 1;
}
