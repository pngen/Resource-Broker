// test_cuda.cu - Real CUDA (RTX 5090) resource proof + pinned host-memory proof.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Only built when RESOURCEBROKER_ENABLE_CUDA_PROOF is ON. Uses a bounded
// governed GPU-memory pool (never the whole device) and cudaHostAlloc pinned
// memory. Proves reservation, real cudaMalloc, H2D, kernel, D2H, CPU parity,
// release, exact accounting closure, oversize rejection before allocation, and
// multi-resource atomic rollback.
#include "resourcebroker/broker.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/reservation.hpp"
#include "resourcebroker/enums.hpp"
#include "resourcebroker/amount.hpp"
#include "resourcebroker/error.hpp"

#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <string>
#include <cuda_runtime.h>

using namespace resourcebroker;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } else { std::fprintf(stderr, "PASS: %s\n", msg); } } while (0)

static const std::uint64_t GiB = 1024ull * 1024ull * 1024ull;
static const std::uint64_t MiB = 1024ull * 1024ull;

__global__ void add_scale(const float* in, float* out, int n, float scale) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = in[i] * scale + 1.0f;
}

int main() {
    // 1. Enumerate the real device.
    int dev = 0;
    cudaDeviceProp prop{};
    cudaError_t e = cudaGetDeviceProperties(&prop, dev);
    if (e != cudaSuccess) { std::fprintf(stderr, "no CUDA device: %s\n", cudaGetErrorString(e)); return 1; }
    std::fprintf(stderr, "device=%s total=%llu MiB\n", prop.name, static_cast<unsigned long long>(prop.totalGlobalMem / MiB));

    std::size_t freeB = 0, totalB = 0;
    cudaMemGetInfo(&freeB, &totalB);
    const std::uint64_t governed = std::min<std::uint64_t>(2 * GiB, (static_cast<std::uint64_t>(freeB) / 4));
    const std::uint64_t alloc_bytes = std::min<std::uint64_t>(512 * MiB, governed);
    std::fprintf(stderr, "governed GPU=%llu MiB free=%llu MiB total=%llu MiB\n",
        static_cast<unsigned long long>(governed / MiB),
        static_cast<unsigned long long>(static_cast<std::uint64_t>(freeB) / MiB),
        static_cast<unsigned long long>(static_cast<std::uint64_t>(totalB) / MiB));

    Broker b(CoordinatorEpoch(1), BrokerId(1));
    b.set_policy(BrokerPolicy{});

    // GPU memory pool.
    ResourcePoolDescriptor gp;
    gp.pool_id = ResourcePoolId(1); gp.pool_generation = ResourcePoolGeneration(1);
    gp.resource_class = ResourceClass::GPU_MEMORY_BYTES; gp.scope = PoolScope::DEVICE_LOCAL;
    gp.total_governed = ResourceAmount::bytes(Bytes(governed));
    gp.provenance = Provenance::MEASURED; gp.health = Health::HEALTHY; gp.freshness = Freshness::CURRENT;
    b.register_pool(gp, CapacityGeneration(1));
    ResourceDescriptor gi;
    gi.instance_id = ResourceInstanceId(1); gi.resource_class = ResourceClass::GPU_MEMORY_BYTES;
    gi.pool_id = gp.pool_id; gi.governed = ResourceAmount::bytes(Bytes(governed));
    gi.provenance = Provenance::MEASURED; gi.health = Health::HEALTHY; gi.freshness = Freshness::CURRENT;
    gi.node = NodeId(1); gi.device = DeviceId(1);
    b.register_resource(gi);

    // Deferred execution-slot pool (synthetic) for atomicity proof.
    ResourcePoolDescriptor ep;
    ep.pool_id = ResourcePoolId(2); ep.pool_generation = ResourcePoolGeneration(1);
    ep.resource_class = ResourceClass::EXECUTION_SLOTS; ep.scope = PoolScope::NODE_LOCAL;
    ep.total_governed = ResourceAmount::count(Count(1));
    ep.provenance = Provenance::SYNTHETIC; ep.health = Health::HEALTHY; ep.freshness = Freshness::CURRENT;
    b.register_pool(ep, CapacityGeneration(1));
    ResourceDescriptor ei;
    ei.instance_id = ResourceInstanceId(2); ei.resource_class = ResourceClass::EXECUTION_SLOTS;
    ei.pool_id = ep.pool_id; ei.governed = ResourceAmount::count(Count(1));
    ei.provenance = Provenance::SYNTHETIC; ei.health = Health::HEALTHY; ei.freshness = Freshness::CURRENT;
    b.register_resource(ei);

    AuthorityEnvelope auth; auth.coordinator_epoch = b.coordinator_epoch();

    // 2. GPU reservation + real cudaMalloc + H2D/kernel/D2H/CPU parity.
    const std::uint64_t n = static_cast<std::uint64_t>(alloc_bytes / sizeof(float));
    ResourceRequest req;
    req.request_id = ResourceRequestId(1); req.request_generation = RequestGeneration(1);
    req.owner = OwnerId(1); req.tenant = TenantId(1); req.workload = WorkloadId(1);
    ResourceRequirement r; r.resource_class = ResourceClass::GPU_MEMORY_BYTES;
    r.requested = ResourceAmount::bytes(Bytes(alloc_bytes)); r.minimum = r.requested; r.preferred = r.requested;
    r.semantics = CapacitySemantics::EXACT; req.requirements.push_back(r);
    req.priority = Priority::HIGH; req.reclaimability = Reclaimability::COOPERATIVE;
    GrantResult g = b.submit_request(req, auth);
    CHECK(g.outcome == RequestOutcome::GRANT, "GPU reservation granted");
    const ReservationId rid = g.reservation_id;
    b.activate_reservation(rid, auth);

    float* dptr = nullptr;
    CHECK(cudaMalloc(&dptr, alloc_bytes) == cudaSuccess, "real cudaMalloc succeeded");
    std::vector<float> h_in(n), h_out(n);
    for (std::uint64_t i = 0; i < n; ++i) h_in[i] = static_cast<float>(i % 1000) * 0.5f;
    CHECK(cudaMemcpy(dptr, h_in.data(), alloc_bytes, cudaMemcpyHostToDevice) == cudaSuccess, "H2D copy succeeded");
    const int block = 256; const int grid = static_cast<int>((n + block - 1) / block);
    add_scale<<<grid, block>>>(dptr, dptr, static_cast<int>(n), 2.0f);
    CHECK(cudaGetLastError() == cudaSuccess, "kernel launch succeeded");
    CHECK(cudaDeviceSynchronize() == cudaSuccess, "kernel synchronization succeeded");
    CHECK(cudaMemcpy(h_out.data(), dptr, alloc_bytes, cudaMemcpyDeviceToHost) == cudaSuccess, "D2H copy succeeded");
    bool parity = true;
    for (std::uint64_t i = 0; i < n; ++i) {
        const float expected = h_in[i] * 2.0f + 1.0f;
        if (h_out[i] != expected) { parity = false; break; }
    }
    CHECK(parity, "GPU kernel CPU-reference parity verified");

    const auto aid = b.report_allocation(rid, gi.instance_id, ResourceAmount::bytes(Bytes(alloc_bytes)), "cudaMalloc:" + std::to_string(static_cast<unsigned long long>(alloc_bytes)), auth);
    CHECK(aid.is_valid(), "GPU physical allocation reported");
    cudaFree(dptr);

    // 3. An oversize request must be rejected/deferred by broker BEFORE allocation.
    const std::uint64_t over = governed + MiB;
    ResourceRequest over_req;
    over_req.request_id = ResourceRequestId(2); over_req.request_generation = RequestGeneration(1);
    over_req.owner = OwnerId(1); over_req.tenant = TenantId(1); over_req.workload = WorkloadId(2);
    ResourceRequirement orr; orr.resource_class = ResourceClass::GPU_MEMORY_BYTES;
    orr.requested = ResourceAmount::bytes(Bytes(over)); orr.minimum = orr.requested; orr.preferred = orr.requested;
    orr.semantics = CapacitySemantics::EXACT; over_req.requirements.push_back(orr);
    over_req.reclaimability = Reclaimability::COOPERATIVE;
    GrantResult go = b.submit_request(over_req, auth);
    CHECK(go.outcome != RequestOutcome::GRANT && go.outcome != RequestOutcome::GRANT_PARTIAL, "oversize GPU request not silently granted");
    std::fprintf(stderr, "oversize outcome = %s\n", to_string(go.outcome));

    // 4. Multi-resource atomic rollback: GPU + pinned + exec slot where exec fails.
    //   Request 64 MiB GPU + 64 MiB pinned + 2 exec slots (only 1 available).
    float* pinned = nullptr;
    CHECK(cudaHostAlloc(&pinned, 64 * MiB, cudaHostAllocDefault) == cudaSuccess, "pinned cudaHostAlloc succeeded");
    cudaMemset(pinned, 0, 64 * MiB);

    ResourceRequest atomic;
    atomic.request_id = ResourceRequestId(3); atomic.request_generation = RequestGeneration(1);
    atomic.owner = OwnerId(1); atomic.tenant = TenantId(1); atomic.workload = WorkloadId(3);
    ResourceRequirement a1; a1.resource_class = ResourceClass::GPU_MEMORY_BYTES;
    a1.requested = ResourceAmount::bytes(Bytes(64 * MiB)); a1.minimum = a1.requested; a1.preferred = a1.requested; a1.semantics = CapacitySemantics::EXACT;
    ResourceRequirement a2; a2.resource_class = ResourceClass::HOST_PINNED_BYTES;
    a2.requested = ResourceAmount::bytes(Bytes(64 * MiB)); a2.minimum = a2.requested; a2.preferred = a2.requested; a2.semantics = CapacitySemantics::EXACT;
    ResourceRequirement a3; a3.resource_class = ResourceClass::EXECUTION_SLOTS;
    a3.requested = ResourceAmount::count(Count(2)); a3.minimum = a3.requested; a3.preferred = a3.requested; a3.semantics = CapacitySemantics::EXACT;
    atomic.requirements.push_back(a1); atomic.requirements.push_back(a2); atomic.requirements.push_back(a3);
    atomic.reclaimability = Reclaimability::COOPERATIVE;

    const std::uint64_t gpu_free_before = b.capacity_view(gp.pool_id).free.as_bytes().value();
    // Pinned pool must exist for a2 to resolve; register it if not.
    if (!b.pool_for_class(ResourceClass::HOST_PINNED_BYTES).has_value()) {
        ResourcePoolDescriptor pp;
        pp.pool_id = ResourcePoolId(3); pp.pool_generation = ResourcePoolGeneration(1);
        pp.resource_class = ResourceClass::HOST_PINNED_BYTES; pp.scope = PoolScope::HOST_LOCAL;
        pp.total_governed = ResourceAmount::bytes(Bytes(256 * MiB));
        pp.provenance = Provenance::MEASURED; pp.health = Health::HEALTHY; pp.freshness = Freshness::CURRENT;
        b.register_pool(pp, CapacityGeneration(1));
    }
    GrantResult ga = b.submit_request(atomic, auth);
    std::fprintf(stderr, "atomic rollback outcome = %s\n", to_string(ga.outcome));
    CHECK(ga.outcome == RequestOutcome::DEFER, "multi-resource atomic request defers on missing exec slot");
    const std::uint64_t gpu_free_after = b.capacity_view(gp.pool_id).free.as_bytes().value();
    CHECK(gpu_free_after == gpu_free_before, "GPU provisional hold rolled back after final-dimension failure");

    // 5. Release GPU reservation + allocation; accounting closes exactly.
    b.release_allocation(aid, auth);
    b.release_reservation(rid, auth);
    auto cv = b.capacity_view(gp.pool_id);
    CHECK(cv.allocated.as_bytes().value() == 0, "GPU allocated returns to zero");
    CHECK(cv.free.as_bytes().value() == governed, "GPU free returns to governed baseline");

    cudaFreeHost(pinned);
    if (failures == 0) { std::printf("test_cuda PASS\n"); return 0; }
    std::printf("test_cuda FAIL (%d)\n", failures); return 1;
}
