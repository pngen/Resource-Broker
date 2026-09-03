// consumer.cpp - Independent downstream consumer of ResourceBroker.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include <resourcebroker/broker.hpp>
#include <resourcebroker/request.hpp>
#include <resourcebroker/resource.hpp>
#include <resourcebroker/reservation.hpp>
#include <resourcebroker/enums.hpp>
#include <resourcebroker/amount.hpp>
#include <resourcebroker/error.hpp>

#include <cstdio>
#include <cstdint>
#include <string>

using namespace resourcebroker;
static const std::uint64_t MiB = 1024ull * 1024ull;

int main() {
    Broker b(CoordinatorEpoch(1), BrokerId(1));
    b.set_policy(BrokerPolicy{});

    ResourcePoolDescriptor gpu;
    gpu.pool_id = ResourcePoolId(1); gpu.pool_generation = ResourcePoolGeneration(1);
    gpu.resource_class = ResourceClass::GPU_MEMORY_BYTES; gpu.scope = PoolScope::DEVICE_LOCAL;
    gpu.total_governed = ResourceAmount::bytes(Bytes(8 * MiB * 1024));
    gpu.provenance = Provenance::SYNTHETIC; gpu.health = Health::HEALTHY; gpu.freshness = Freshness::CURRENT;
    b.register_pool(gpu, CapacityGeneration(1));
    ResourceDescriptor gi;
    gi.instance_id = ResourceInstanceId(1); gi.resource_class = ResourceClass::GPU_MEMORY_BYTES; gi.pool_id = gpu.pool_id;
    gi.governed = gpu.total_governed; gi.provenance = Provenance::SYNTHETIC; gi.health = Health::HEALTHY; gi.freshness = Freshness::CURRENT;
    b.register_resource(gi);

    ResourcePoolDescriptor pin;
    pin.pool_id = ResourcePoolId(2); pin.pool_generation = ResourcePoolGeneration(1);
    pin.resource_class = ResourceClass::HOST_PINNED_BYTES; pin.scope = PoolScope::HOST_LOCAL;
    pin.total_governed = ResourceAmount::bytes(Bytes(512 * MiB));
    pin.provenance = Provenance::SYNTHETIC; pin.health = Health::HEALTHY; pin.freshness = Freshness::CURRENT;
    b.register_pool(pin, CapacityGeneration(1));

    AuthorityEnvelope auth; auth.coordinator_epoch = b.coordinator_epoch();

    // Single-resource request.
    ResourceRequest r1; r1.request_id = ResourceRequestId(1); r1.owner = OwnerId(1); r1.tenant = TenantId(1);
    ResourceRequirement rq; rq.resource_class = ResourceClass::GPU_MEMORY_BYTES;
    rq.requested = ResourceAmount::bytes(Bytes(2048 * MiB)); rq.minimum = rq.requested; rq.preferred = rq.requested; rq.semantics = CapacitySemantics::EXACT;
    r1.requirements.push_back(rq); r1.priority = Priority::HIGH;
    GrantResult g = b.submit_request(r1, auth);
    if (g.outcome != RequestOutcome::GRANT) { std::fprintf(stderr, "single request failed\n"); return 1; }
    b.activate_reservation(g.reservation_id, auth);
    const AllocationId aid = b.report_allocation(g.reservation_id, gi.instance_id, ResourceAmount::bytes(Bytes(2048 * MiB)), "consumer-alloc", auth);
    if (!aid.is_valid()) { std::fprintf(stderr, "allocation failed\n"); return 1; }

    // Contention: a second 8-GiB request must not be granted silently.
    ResourceRequest r2; r2.request_id = ResourceRequestId(2); r2.owner = OwnerId(2); r2.tenant = TenantId(2);
    ResourceRequirement rq2; rq2.resource_class = ResourceClass::GPU_MEMORY_BYTES;
    rq2.requested = ResourceAmount::bytes(Bytes(6 * MiB * 1024)); rq2.minimum = rq2.requested; rq2.preferred = rq2.requested; rq2.semantics = CapacitySemantics::EXACT;
    r2.requirements.push_back(rq2); r2.priority = Priority::NORMAL;
    const GrantResult g2 = b.submit_request(r2, auth);
    std::fprintf(stderr, "contention outcome = %s\n", to_string(g2.outcome));

    // Release + accounting closure.
    b.release_allocation(aid, auth);
    b.release_reservation(g.reservation_id, auth);
    const auto cv = b.capacity_view(gpu.pool_id);
    if (cv.free.as_bytes().value() != b.pool(gpu.pool_id).total_governed.as_bytes().value()) {
        std::fprintf(stderr, "accounting did not close\n"); return 1;
    }

    // Save/recover.
    const std::string path = "consumer_state.rbstate";
    b.save(path);
    Broker b2(CoordinatorEpoch(2), BrokerId(1));
    b2.load(path);
    if (!b2.pool_for_class(ResourceClass::GPU_MEMORY_BYTES).has_value()) { std::fprintf(stderr, "recover failed\n"); return 1; }

    // Print an explanation.
    const Explanation ex = b.explain_request(r1, auth);
    std::fprintf(stderr, "explanation: %s\n", ex.headline.c_str());

    std::printf("rb_consumer PASS\n");
    return 0;
}
