// test_flow.cpp - Requests, reservations, allocations, leases, recall/preemption, reclamation, arbitration.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/broker.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/reservation.hpp"
#include "resourcebroker/amount.hpp"
#include <cstdio>
using namespace resourcebroker;
static int failures=0;
#define CHECK(c,m) do{ if(!(c)){std::fprintf(stderr,"FAIL: %s\n",m);++failures;} else std::fprintf(stderr,"PASS: %s\n",m);}while(0)
static const std::uint64_t MiB=1024ull*1024ull,GiB=1024ull*1024ull*1024ull;
static ResourceRequirement exact(ResourceClass c, std::uint64_t amt){ ResourceRequirement r; r.resource_class=c; r.requested=ResourceAmount::bytes(Bytes(amt)); r.minimum=r.requested; r.preferred=r.requested; r.semantics=CapacitySemantics::EXACT; return r; }
int main(){
    Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
    ResourcePoolDescriptor gp; gp.pool_id=ResourcePoolId(1); gp.resource_class=ResourceClass::GPU_MEMORY_BYTES; gp.scope=PoolScope::DEVICE_LOCAL;
    gp.total_governed=ResourceAmount::bytes(Bytes(8*GiB)); gp.provenance=Provenance::SYNTHETIC; gp.health=Health::HEALTHY; gp.freshness=Freshness::CURRENT;
    b.register_pool(gp,CapacityGeneration(1));
    ResourceDescriptor gi; gi.instance_id=ResourceInstanceId(1); gi.resource_class=ResourceClass::GPU_MEMORY_BYTES; gi.pool_id=gp.pool_id; gi.governed=ResourceAmount::bytes(Bytes(8*GiB)); gi.provenance=Provenance::SYNTHETIC;
    b.register_resource(gi);
    AuthorityEnvelope auth; auth.coordinator_epoch=b.coordinator_epoch();

    // Atomic multi-resource: GPU 2GiB (atomic success).
    ResourceRequest r1; r1.request_id=ResourceRequestId(1); r1.owner=OwnerId(1); r1.tenant=TenantId(1); r1.priority=Priority::HIGH;
    r1.requirements.push_back(exact(ResourceClass::GPU_MEMORY_BYTES,2*GiB)); r1.reclaimability=Reclaimability::RECLAIMABLE; r1.preemption_eligible=true;
    GrantResult g1=b.submit_request(r1,auth); CHECK(g1.outcome==RequestOutcome::GRANT,"atomic grant");
    b.activate_reservation(g1.reservation_id,auth);
    const AllocationId al=b.report_allocation(g1.reservation_id,gi.instance_id,ResourceAmount::bytes(Bytes(2*GiB)),"gpu",auth);
    CHECK(al.is_valid(),"allocation reported");
    // Lease lifecycle.
    const LeaseId lid=b.acquire_lease(g1.reservation_id,auth); CHECK(lid.is_valid(),"lease acquired");
    b.renew_lease(lid,auth); CHECK(b.lease(lid).lease_generation.value()>=2,"lease renewed generation");
    b.revoke_lease(lid,auth); CHECK(b.lease(lid).state==LeaseState::REVOKED,"lease revoked");

    // Recall then preemption on a competing low-priority reservation.
    ResourceRequest r2; r2.request_id=ResourceRequestId(2); r2.owner=OwnerId(2); r2.tenant=TenantId(2); r2.priority=Priority::LOW;
    r2.requirements.push_back(exact(ResourceClass::GPU_MEMORY_BYTES,6*GiB)); r2.reclaimability=Reclaimability::RECLAIMABLE; r2.preemption_eligible=true;
    GrantResult g2=b.submit_request(r2,auth); CHECK(g2.outcome==RequestOutcome::GRANT,"low-priority grant"); // 2+6=8
    b.activate_reservation(g2.reservation_id,auth);
    // Capacity now fully consumed; a fresh request should surface RECALL/PREEMPT_REQUIRED, not a silent grant.
    ResourceRequest r3; r3.request_id=ResourceRequestId(3); r3.owner=OwnerId(3); r3.tenant=TenantId(3); r3.priority=Priority::CRITICAL;
    r3.requirements.push_back(exact(ResourceClass::GPU_MEMORY_BYTES,4*GiB)); r3.reclaimability=Reclaimability::COOPERATIVE;
    GrantResult g3=b.submit_request(r3,auth);
    CHECK(g3.outcome==RequestOutcome::PREEMPT_REQUIRED || g3.outcome==RequestOutcome::RECALL_REQUIRED || g3.outcome==RequestOutcome::DEFER,"contention not silently granted");

    // Reclamation plan selects the lowest-priority, most-reclaimable reservation.
    const ReclamationPlan plan=b.plan_reclamation(gp.pool_id,ResourceAmount::bytes(Bytes(4*GiB)));
    CHECK(plan.covered.as_bytes().value()>=4*GiB,"reclamation plan covers deficit");
    CHECK(!plan.chosen.empty(),"reclamation plan chooses candidates");
    // Protected reservoir: a NON_RECLAIMABLE reservation must not be selected.
    // Simulate by checking candidates flag protection correctly for a protected reservation.
    ResourceRequest rp; rp.request_id=ResourceRequestId(4); rp.owner=OwnerId(4); rp.tenant=TenantId(4); rp.priority=Priority::BACKGROUND;
    rp.requirements.push_back(exact(ResourceClass::GPU_MEMORY_BYTES,1*GiB)); rp.reclaimability=Reclaimability::NON_RECLAIMABLE;
    // not submitted (would overcommit); directly test candidate flagging with a separate pool.
    ResourcePoolDescriptor mp; mp.pool_id=ResourcePoolId(2); mp.resource_class=ResourceClass::HOST_MEMORY_BYTES; mp.scope=PoolScope::HOST_LOCAL;
    mp.total_governed=ResourceAmount::bytes(Bytes(64*MiB)); mp.provenance=Provenance::SYNTHETIC; mp.health=Health::HEALTHY; mp.freshness=Freshness::CURRENT;
    b.register_pool(mp,CapacityGeneration(1));
    ResourceRequest rp2; rp2.request_id=ResourceRequestId(5); rp2.owner=OwnerId(5); rp2.tenant=TenantId(5); rp2.priority=Priority::BACKGROUND;
    rp2.requirements.push_back(exact(ResourceClass::HOST_MEMORY_BYTES,32*MiB)); rp2.reclaimability=Reclaimability::NON_RECLAIMABLE;
    GrantResult gp1=b.submit_request(rp2,auth); CHECK(gp1.outcome==RequestOutcome::GRANT,"protected reservation granted");
    const ReclamationPlan hp=b.plan_reclamation(mp.pool_id,ResourceAmount::bytes(Bytes(32*MiB)));
    CHECK(hp.unsatisfied.as_bytes().value()>=32*MiB,"protected reservation is not chosen for reclamation");

    // Release everything; accounting returns to baseline.
    b.release_reservation(g1.reservation_id,auth);
    b.release_reservation(g2.reservation_id,auth);
    b.release_reservation(gp1.reservation_id,auth);
    auto cv=b.capacity_view(gp.pool_id); CHECK(cv.free.as_bytes().value()==8*GiB,"GPU accounting returns to baseline");
    auto cv2=b.capacity_view(mp.pool_id); CHECK(cv2.free.as_bytes().value()==64*MiB,"host accounting returns to baseline");
    if(failures==0){std::printf("test_flow PASS\n");return 0;} std::printf("test_flow FAIL (%d)\n",failures);return 1;
}
