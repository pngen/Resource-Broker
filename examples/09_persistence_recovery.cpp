// 09_persistence_recovery.cpp
#include "resourcebroker/broker.hpp"
#include "example_util.hpp"
#include <cstdio>
using namespace resourcebroker;
int main(){ Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
 ResourcePoolDescriptor p; p.pool_id=ResourcePoolId(1); p.resource_class=ResourceClass::HOST_MEMORY_BYTES; p.total_governed=ResourceAmount::bytes(Bytes(64ull*1024*1024)); p.provenance=Provenance::MEASURED; b.register_pool(p,CapacityGeneration(1));
 b.save("example_state.rbstate");
 Broker b2(CoordinatorEpoch(2),BrokerId(1)); b2.load("example_state.rbstate");
 std::printf("recovered pool=%d\n",b2.pool_for_class(ResourceClass::HOST_MEMORY_BYTES).has_value()); return 0; }
