// cli.cpp - resource-broker command-line interface.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/broker.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/reservation.hpp"
#include "resourcebroker/enums.hpp"
#include "resourcebroker/amount.hpp"
#include "resourcebroker/error.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>

using namespace resourcebroker;

static const std::uint64_t MiB = 1024ull * 1024ull;
static const std::uint64_t GiB = 1024ull * 1024ull * 1024ull;

static void print_header() {
    std::printf("resource-broker 1.0.0\n");
    std::printf("commands: register-pool, capacity, request, reservations, allocations, \n");
    std::printf("          activate, release, lease-renew, recall, preempt, reclamation-plan, \n");
    std::printf("          owner, explain, save, recover, simulate\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { print_header(); return 0; }
    const std::string cmd = argv[1];
    try {
    Broker b(CoordinatorEpoch(1), BrokerId(1));
    b.set_policy(BrokerPolicy{});
    AuthorityEnvelope auth; auth.coordinator_epoch = b.coordinator_epoch();

    if (cmd == "register-pool") {
        // args: class governed_bytes
        if (argc < 4) { std::printf("usage: resource-broker register-pool <CLASS> <governed_bytes>\n"); return 1; }
        const auto cls = argv[2];
        const std::uint64_t bytes = std::strtoull(argv[3], nullptr, 10);
        ResourcePoolDescriptor pd;
        pd.pool_id = ResourcePoolId(1); pd.pool_generation = ResourcePoolGeneration(1);
        pd.resource_class = (std::strcmp(cls, "HOST_MEMORY") == 0) ? ResourceClass::HOST_MEMORY_BYTES
                          : (std::strcmp(cls, "GPU_MEMORY") == 0) ? ResourceClass::GPU_MEMORY_BYTES
                          : ResourceClass::GENERIC_SCALAR_RESOURCE;
        pd.scope = PoolScope::HOST_LOCAL;
        pd.total_governed = ResourceAmount::bytes(Bytes(bytes));
        pd.provenance = (std::strcmp(cls, "GPU_MEMORY") == 0) ? Provenance::MEASURED : Provenance::MEASURED;
        pd.health = Health::HEALTHY; pd.freshness = Freshness::CURRENT;
        b.register_pool(pd, CapacityGeneration(1));
        ResourceDescriptor ri;
        ri.instance_id = ResourceInstanceId(1); ri.resource_class = pd.resource_class; ri.pool_id = pd.pool_id;
        ri.governed = pd.total_governed; ri.provenance = pd.provenance; ri.health = Health::HEALTHY; ri.freshness = Freshness::CURRENT;
        b.register_resource(ri);
        std::printf("registered pool %llu class=%s governed=%llu B\n", (unsigned long long)pd.pool_id.value(), to_string(pd.resource_class), (unsigned long long)bytes);
        return 0;
    } else if (cmd == "capacity") {
        for (const auto& p : b.all_pools()) {
            const auto cv = b.capacity_view(p.pool_id);
            std::printf("pool %llu class=%s governed=%s reserved=%s allocated=%s free=%s\n",
                (unsigned long long)p.pool_id.value(), to_string(p.resource_class),
                cv.governed.to_string().c_str(), cv.reserved.to_string().c_str(),
                cv.allocated.to_string().c_str(), cv.free.to_string().c_str());
        }
        return 0;
    } else if (cmd == "request") {
        // usage: request <class> <bytes>
        if (argc < 4) { std::printf("usage: resource-broker request <CLASS> <bytes>\n"); return 1; }
        const auto cls = (std::strcmp(argv[2], "HOST_MEMORY") == 0) ? ResourceClass::HOST_MEMORY_BYTES : ResourceClass::GPU_MEMORY_BYTES;
        const std::uint64_t bytes = std::strtoull(argv[3], nullptr, 10);
        ResourceRequest req;
        req.request_id = ResourceRequestId(b.coordinator_epoch().value()); req.request_generation = RequestGeneration(1);
        req.owner = OwnerId(1); req.tenant = TenantId(1); req.workload = WorkloadId(1);
        ResourceRequirement r; r.resource_class = cls; r.requested = ResourceAmount::bytes(Bytes(bytes));
        r.minimum = r.requested; r.preferred = r.requested; r.semantics = CapacitySemantics::EXACT;
        req.requirements.push_back(r); req.priority = Priority::NORMAL; req.reclaimability = Reclaimability::COOPERATIVE;
        GrantResult g = b.submit_request(req, auth);
        std::printf("outcome=%s rid=%llu (%s)\n", to_string(g.outcome), (unsigned long long)g.reservation_id.value(), g.explanation.headline.c_str());
        return 0;
    } else if (cmd == "reservations") {
        for (const auto& rsv : b.all_reservations()) {
            std::printf("reservation %llu state=%s owner=%llu priority=%s claims=%zu\n",
                (unsigned long long)rsv.reservation_id.value(), to_string(rsv.state),
                (unsigned long long)rsv.owner.value(), to_string(rsv.priority), rsv.claims.size());
        }
        return 0;
    } else if (cmd == "allocations") {
        for (const auto& a : b.all_allocations()) {
            std::printf("allocation %llu reservation=%llu instance=%llu amount=%s state=%s evidence=%s\n",
                (unsigned long long)a.allocation_id.value(), (unsigned long long)a.reservation_id.value(),
                (unsigned long long)a.instance_id.value(), a.amount.to_string().c_str(), to_string(a.state), a.physical_evidence.c_str());
        }
        return 0;
    } else if (cmd == "activate") {
        if (argc < 3) { std::printf("usage: activate <reservation_id>\n"); return 1; }
        b.activate_reservation(ReservationId(std::strtoull(argv[2], nullptr, 10)), auth);
        std::printf("activated\n"); return 0;
    } else if (cmd == "release") {
        if (argc < 3) { std::printf("usage: release <reservation_id>\n"); return 1; }
        b.release_reservation(ReservationId(std::strtoull(argv[2], nullptr, 10)), auth);
        std::printf("released\n"); return 0;
    } else if (cmd == "reclamation-plan") {
        if (argc < 4) { std::printf("usage: reclamation-plan <pool_id> <deficit_bytes>\n"); return 1; }
        const auto pid = ResourcePoolId(std::strtoull(argv[2], nullptr, 10));
        const auto deficit = ResourceAmount::bytes(Bytes(std::strtoull(argv[3], nullptr, 10)));
        const ReclamationPlan plan = b.plan_reclamation(pid, deficit);
        std::printf("pool %llu deficit=%s covered=%s unsatisfied=%s candidates=%zu chosen=%zu%s\n",
            (unsigned long long)pid.value(), plan.deficit.to_string().c_str(), plan.covered.to_string().c_str(),
            plan.unsatisfied.to_string().c_str(), plan.candidates.size(), plan.chosen.size(),
            plan.rationale.empty() ? "" : (" rationale=" + plan.rationale).c_str());
        return 0;
    } else if (cmd == "save") {
        if (argc < 3) { std::printf("usage: save <path>\n"); return 1; }
        b.save(argv[2]); std::printf("saved %s\n", argv[2]); return 0;
    } else if (cmd == "recover") {
        if (argc < 3) { std::printf("usage: recover <path>\n"); return 1; }
        b.load(argv[2]); std::printf("recovered %s\n", argv[2]); return 0;
    } else if (cmd == "simulate") {
        std::printf("simulating deterministic contention scenario\n");
        BrokerPolicy pol; b.set_policy(pol);
        ResourcePoolDescriptor pd; pd.pool_id=ResourcePoolId(1); pd.resource_class=ResourceClass::GPU_MEMORY_BYTES;
        pd.total_governed=ResourceAmount::bytes(Bytes(8*GiB)); pd.provenance=Provenance::SYNTHETIC; pd.health=Health::HEALTHY; pd.freshness=Freshness::CURRENT;
        b.register_pool(pd, CapacityGeneration(1));
        ResourceDescriptor ri; ri.instance_id=ResourceInstanceId(1); ri.resource_class=ResourceClass::GPU_MEMORY_BYTES; ri.pool_id=pd.pool_id;
        ri.governed=pd.total_governed; ri.provenance=Provenance::SYNTHETIC; ri.health=Health::HEALTHY; ri.freshness=Freshness::CURRENT;
        b.register_resource(ri);
        // Two competing requests.
        ResourceRequest r1; r1.request_id=ResourceRequestId(1); r1.owner=OwnerId(1); r1.tenant=TenantId(1);
        ResourceRequirement rq; rq.resource_class=ResourceClass::GPU_MEMORY_BYTES; rq.requested=ResourceAmount::bytes(Bytes(4*GiB));
        rq.minimum=rq.requested; rq.preferred=rq.requested; rq.semantics=CapacitySemantics::EXACT; r1.requirements.push_back(rq);
        r1.priority=Priority::HIGH; r1.reclaimability=Reclaimability::RECLAIMABLE;
        ResourceRequest r2; r2.request_id=ResourceRequestId(2); r2.owner=OwnerId(2); r2.tenant=TenantId(2);
        ResourceRequirement rq2; rq2.resource_class=ResourceClass::GPU_MEMORY_BYTES; rq2.requested=ResourceAmount::bytes(Bytes(4*GiB));
        rq2.minimum=rq2.requested; rq2.preferred=rq2.requested; rq2.semantics=CapacitySemantics::EXACT; r2.requirements.push_back(rq2);
        r2.priority=Priority::LOW; r2.reclaimability=Reclaimability::RECLAIMABLE;
        GrantResult g1 = b.submit_request(r1, auth);
        GrantResult g2 = b.submit_request(r2, auth);
        std::printf("r1=%s rid=%llu\nr2=%s rid=%llu\n", to_string(g1.outcome), (unsigned long long)g1.reservation_id.value(), to_string(g2.outcome), (unsigned long long)g2.reservation_id.value());
        if (!g1.reservation_id.is_valid()) return 0;
        b.activate_reservation(g1.reservation_id, auth);
        b.report_allocation(g1.reservation_id, ri.instance_id, ResourceAmount::bytes(Bytes(4*GiB)), "sim-alloc", auth);
        const ReclamationPlan plan = b.plan_reclamation(pd.pool_id, ResourceAmount::bytes(Bytes(2*GiB)));
        std::printf("reclamation: covers=%s unsatisfied=%s\n", plan.covered.to_string().c_str(), plan.unsatisfied.to_string().c_str());
        return 0;
    } else {
        print_header();
        return 0;
    }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
