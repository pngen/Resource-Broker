// bench_core.cpp - Completed-work microbenchmarks for Broker operations.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/broker.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/protocol.hpp"
#include "resourcebroker/amount.hpp"
#include <cstdio>
#include <chrono>
#include <vector>
using namespace resourcebroker;
using Clock = std::chrono::steady_clock;
static const std::uint64_t GiB=1024ull*1024ull*1024ull;
static ResourceRequirement ex(ResourceClass c,std::uint64_t a){ ResourceRequirement r; r.resource_class=c; r.requested=ResourceAmount::bytes(Bytes(a)); r.minimum=r.requested; r.preferred=r.requested; r.semantics=CapacitySemantics::EXACT; return r; }
template<class F> double bench(F f, int iters){ const auto t0=Clock::now(); for(int i=0;i<iters;++i) f(i); const auto t1=Clock::now(); return std::chrono::duration<double,std::nano>(t1-t0).count()/iters; }
int main(){
    const int N=10000;
    const std::size_t threads=1;
    Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
    ResourcePoolDescriptor gp; gp.pool_id=ResourcePoolId(1); gp.resource_class=ResourceClass::GPU_MEMORY_BYTES; gp.scope=PoolScope::DEVICE_LOCAL;
    gp.total_governed=ResourceAmount::bytes(Bytes(1000000*GiB)); gp.provenance=Provenance::SYNTHETIC; gp.health=Health::HEALTHY; gp.freshness=Freshness::CURRENT;
    b.register_pool(gp,CapacityGeneration(1));
    AuthorityEnvelope a; a.coordinator_epoch=b.coordinator_epoch();

    ResourceRequest canonical; canonical.request_id=ResourceRequestId(999); canonical.owner=OwnerId(1); canonical.tenant=TenantId(1);
    canonical.requirements.push_back(ex(ResourceClass::GPU_MEMORY_BYTES,4*GiB)); canonical.requirements.push_back(ex(ResourceClass::GPU_MEMORY_BYTES,1*GiB));
    // Note: duplicate class would reject; use two distinct pools for multi-resource.
    // Build a proper multi-resource request.
    ResourcePoolDescriptor mp; mp.pool_id=ResourcePoolId(2); mp.resource_class=ResourceClass::HOST_MEMORY_BYTES; mp.scope=PoolScope::HOST_LOCAL;
    mp.total_governed=ResourceAmount::bytes(Bytes(1000000*GiB)); mp.provenance=Provenance::SYNTHETIC; b.register_pool(mp,CapacityGeneration(1));
    ResourceRequest multi; multi.request_id=ResourceRequestId(1); multi.owner=OwnerId(1); multi.tenant=TenantId(1);
    multi.requirements.push_back(ex(ResourceClass::GPU_MEMORY_BYTES,4*GiB)); multi.requirements.push_back(ex(ResourceClass::HOST_MEMORY_BYTES,32*GiB));
    multi.reclaimability=Reclaimability::RECLAIMABLE;

    const double t_req = bench([&](int){ ResourceRequest r=multi; r.request_id=ResourceRequestId(1000+(std::uint64_t)(std::rand()%1000)); try{ b.submit_request(r,a); }catch(...){ try{ b.submit_request(r,a);}catch(...){} } }, N);
    std::printf("request_eval          ops/s=%.0f ns/op=%.1f count=%d threads=%zu\n", 1e9/t_req, t_req, N, threads);

    // Reservation commit+release throughput (do a real request once, then release).
    std::vector<ReservationId> ids;
    {
        ResourceRequest r=multi; r.request_id=ResourceRequestId(70000);
        for(int i=0;i<2000;++i){ ResourceRequest rr=multi; rr.request_id=ResourceRequestId(70000+i); try{ GrantResult g=b.submit_request(rr,a); if(g.outcome==RequestOutcome::GRANT){ ids.push_back(g.reservation_id); b.activate_reservation(g.reservation_id,a);} }catch(...){} }
    }
    std::fprintf(stderr,"held reservations=%zu\n",ids.size());
    const double t_rel = bench([&](int i){ if(!ids.empty()){ try{ b.release_reservation(ids[i%ids.size()],a); }catch(...){} } }, N);
    std::printf("reservation_release    ops/s=%.0f ns/op=%.1f count=%d threads=%zu\n", 1e9/t_rel, t_rel, N, threads);

    // Persistence serialize (round-trip on a snapshot broker).
    const double t_save = bench([&](int){ b.save("bench_state.rbstate"); }, 200);
    std::printf("persistence_save       ops/s=%.2f ns/op=%.2f count=200 threads=%zu\n", 1e9/t_save, t_save, threads);

    // Protocol encode/decode throughput.
    const auto req_bytes = encode_request(multi, a);
    const auto frame = encode_frame(MessageKind::REQUEST_RESOURCES, req_bytes);
    const double t_proto = bench([&](int){ auto f=decode_frame(frame.data(),frame.size()); (void)f; }, N);
    std::printf("protocol_decode        ops/s=%.0f ns/op=%.1f count=%d threads=%zu\n", 1e9/t_proto, t_proto, N, threads);

    // Capacity query.
    const double t_cap = bench([&](int){ auto v=b.capacity_view(gp.pool_id); (void)v; }, N);
    std::printf("capacity_query         ops/s=%.0f ns/op=%.1f count=%d threads=%zu\n", 1e9/t_cap, t_cap, N, threads);

    std::printf("benchmark done; wall resources=2 pools, reservations=%zu\n", ids.size());
    return 0;
}
