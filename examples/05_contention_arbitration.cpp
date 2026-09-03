// 05_contention_arbitration.cpp
#include "resourcebroker/broker.hpp"
#include "example_util.hpp"
#include <cstdio>
using namespace resourcebroker;
int main(){ Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
 ResourcePoolDescriptor p; p.pool_id=ResourcePoolId(1); p.resource_class=ResourceClass::GPU_MEMORY_BYTES; p.total_governed=ResourceAmount::bytes(Bytes(8ull*1024*1024*1024)); p.provenance=Provenance::SYNTHETIC; b.register_pool(p,CapacityGeneration(1));
 AuthorityEnvelope a; a.coordinator_epoch=b.coordinator_epoch();
 const GrantResult g1=b.submit_request(make_req(ResourceRequestId(1),4ull*1024*1024*1024,Priority::HIGH),a);
 const GrantResult g2=b.submit_request(make_req(ResourceRequestId(2),7ull*1024*1024*1024,Priority::NORMAL),a);
 std::printf("high=%s low=%s\n",to_string(g1.outcome),to_string(g2.outcome)); return 0; }
