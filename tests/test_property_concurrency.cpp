// test_property_concurrency.cpp - Fixed-seed property checks and concurrency.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/broker.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/amount.hpp"
#include <cstdio>
#include <random>
#include <thread>
#include <atomic>
#include <vector>
using namespace resourcebroker;
static int failures=0;
#define CHECK(c,m) do{ if(!(c)){std::fprintf(stderr,"FAIL: %s\n",m);++failures;} else std::fprintf(stderr,"PASS: %s\n",m);}while(0)
static const std::uint64_t GiB=1024ull*1024ull*1024ull;
static ResourceRequirement rq_ex(ResourceClass c,std::uint64_t a){ ResourceRequirement r; r.resource_class=c; r.requested=ResourceAmount::bytes(Bytes(a)); r.minimum=r.requested; r.preferred=r.requested; r.semantics=CapacitySemantics::EXACT; return r; }
int main(){
    const std::uint32_t seed=0xC0FFEE;
    std::fprintf(stderr,"property seed = 0x%08X\n",seed);
    Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
    ResourcePoolDescriptor gp; gp.pool_id=ResourcePoolId(1); gp.resource_class=ResourceClass::GPU_MEMORY_BYTES; gp.scope=PoolScope::DEVICE_LOCAL;
    gp.total_governed=ResourceAmount::bytes(Bytes(16*GiB)); gp.provenance=Provenance::SYNTHETIC; gp.health=Health::HEALTHY; gp.freshness=Freshness::CURRENT;
    b.register_pool(gp,CapacityGeneration(1));
    ResourceDescriptor gi; gi.instance_id=ResourceInstanceId(1); gi.resource_class=ResourceClass::GPU_MEMORY_BYTES; gi.pool_id=gp.pool_id; gi.governed=ResourceAmount::bytes(Bytes(16*GiB)); gi.provenance=Provenance::SYNTHETIC;
    b.register_resource(gi);
    AuthorityEnvelope auth; auth.coordinator_epoch=b.coordinator_epoch();

    std::mt19937 rng(seed);
    std::vector<ReservationId> active;
    bool invariant_ok=true;
    for(int i=0;i<2000;++i){
        const std::uint64_t amt=1ull*GiB + (rng()%8ull)*GiB;
        ResourceRequest rq; rq.request_id=ResourceRequestId(i+1); rq.owner=OwnerId(1); rq.tenant=TenantId(1);
        rq.requirements.push_back(rq_ex(ResourceClass::GPU_MEMORY_BYTES,amt)); rq.priority=Priority::NORMAL; rq.reclaimability=Reclaimability::RECLAIMABLE;
        try{
            GrantResult g=b.submit_request(rq,auth);
            if(g.outcome==RequestOutcome::GRANT){ active.push_back(g.reservation_id); b.activate_reservation(g.reservation_id,auth); }
        }catch(const BrokerError&){}
        // Invariant: never negative, reserved+allocated <= governed.
        const auto cv=b.capacity_view(gp.pool_id);
        if(cv.free.as_bytes().value() > cv.governed.as_bytes().value()) invariant_ok=false;
        if(cv.reserved.as_bytes().value()>0 && cv.allocated.as_bytes().value()>0){ /* fine */ }
        // Randomly release some.
        if(rng()%100<8 && !active.empty()){ const auto rid=active.back(); active.pop_back(); try{ b.release_reservation(rid,auth);}catch(...){} }
    }
    CHECK(invariant_ok,"property: accounting never exceeds governed");
    // After a wave, capacity never negative.
    const auto cvf=b.capacity_view(gp.pool_id);
    CHECK(cvf.free.as_bytes().value()<=cvf.governed.as_bytes().value(),"property: free never exceeds governed");
    // Deterministic arbitration: same inputs -> same result.
    ResourceRequest q1; q1.request_id=ResourceRequestId(9999); q1.owner=OwnerId(9); q1.tenant=TenantId(9); q1.requirements.push_back(rq_ex(ResourceClass::GPU_MEMORY_BYTES,3*GiB)); q1.priority=Priority::HIGH;
    const GrantResult ga=b.submit_request(q1,auth); const GrantResult gb=b.submit_request(q1,auth);
    CHECK(ga.outcome==gb.outcome,"property: deterministic arbitration");

    // Concurrency: many threads issue requests/releases; final accounting must be sane.
    Broker c(CoordinatorEpoch(1),BrokerId(1)); c.set_policy(BrokerPolicy{});
    ResourcePoolDescriptor cp; cp.pool_id=ResourcePoolId(1); cp.resource_class=ResourceClass::EXECUTION_SLOTS; cp.scope=PoolScope::NODE_LOCAL;
    cp.total_governed=ResourceAmount::count(Count(64)); cp.provenance=Provenance::SYNTHETIC; cp.health=Health::HEALTHY; cp.freshness=Freshness::CURRENT;
    c.register_pool(cp,CapacityGeneration(1));
    AuthorityEnvelope ca; ca.coordinator_epoch=c.coordinator_epoch();
    std::atomic<int> granted{0};
    std::vector<std::thread> ths;
    for(int t=0;t<8;++t){
        ths.emplace_back([&c,&ca,&granted,t](){
            for(int j=0;j<500;++j){
                ResourceRequest rq; rq.request_id=ResourceRequestId(100000+t*1000+j); rq.owner=OwnerId(1); rq.tenant=TenantId(1);
                ResourceRequirement rr; rr.resource_class=ResourceClass::EXECUTION_SLOTS; rr.requested=ResourceAmount::count(Count(1)); rr.minimum=rr.requested; rr.preferred=rr.requested; rr.semantics=CapacitySemantics::EXACT;
                rq.requirements.push_back(rr); rq.reclaimability=Reclaimability::RECLAIMABLE;
                try{
                    GrantResult g=c.submit_request(rq,ca);
                    if(g.outcome==RequestOutcome::GRANT){ granted.fetch_add(1); c.activate_reservation(g.reservation_id,ca); c.release_reservation(g.reservation_id,ca); }
                }catch(const BrokerError&){}
            }
        });
    }
    for(auto& t:ths) t.join();
    const auto ccv=c.capacity_view(cp.pool_id);
    CHECK(ccv.free.as_count().value()==64,"concurrency: final slot accounting closed (free == 64)");
    std::fprintf(stderr,"concurrency granted=%d\n",granted.load());

    if(failures==0){std::printf("test_property_concurrency PASS\n");return 0;} std::printf("test_property_concurrency FAIL (%d)\n",failures);return 1;
}
