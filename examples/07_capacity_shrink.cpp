// 07_capacity_shrink.cpp
#include "resourcebroker/broker.hpp"
#include "example_util.hpp"
#include <cstdio>
using namespace resourcebroker;
int main(){ Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
 ResourcePoolDescriptor p; p.pool_id=ResourcePoolId(1); p.resource_class=ResourceClass::GPU_MEMORY_BYTES; p.total_governed=ResourceAmount::bytes(Bytes(8ull*1024*1024*1024)); p.provenance=Provenance::SYNTHETIC; b.register_pool(p,CapacityGeneration(1));
 AuthorityEnvelope a; a.coordinator_epoch=b.coordinator_epoch();
 const GrantResult g=b.submit_request(make_req(ResourceRequestId(1),6ull*1024*1024*1024,Priority::HIGH),a);
 ReclamationPlan plan=b.plan_reclamation(p.pool_id,ResourceAmount::bytes(Bytes(2ull*1024*1024*1024)));
 std::printf("shrink covered=%s unsatisfied=%s selected=%zu\n",plan.covered.to_string().c_str(),plan.unsatisfied.to_string().c_str(),plan.chosen.size()); return 0; }
