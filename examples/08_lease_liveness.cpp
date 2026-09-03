// 08_lease_liveness.cpp
#include "resourcebroker/broker.hpp"
#include "example_util.hpp"
#include <cstdio>
using namespace resourcebroker;
int main(){ Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
 ResourcePoolDescriptor p; p.pool_id=ResourcePoolId(1); p.resource_class=ResourceClass::GPU_MEMORY_BYTES; p.total_governed=ResourceAmount::bytes(Bytes(8ull*1024*1024*1024)); p.provenance=Provenance::SYNTHETIC; b.register_pool(p,CapacityGeneration(1));
 AuthorityEnvelope a; a.coordinator_epoch=b.coordinator_epoch(); const auto boot=b.register_worker(WorkerId(5)); a.worker_boot=boot;
 const GrantResult g=b.submit_request(make_req(ResourceRequestId(1),2ull*1024*1024*1024,Priority::HIGH),a);
 const LeaseId lid=b.acquire_lease(g.reservation_id,a);
 b.renew_lease(lid,a);
 std::printf("lease generation=%llu state=%s\n",(unsigned long long)b.lease(lid).lease_generation.value(),to_string(b.lease(lid).state)); return 0; }
