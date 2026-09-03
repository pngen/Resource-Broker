// 06_recall_preemption.cpp
#include "resourcebroker/broker.hpp"
#include "example_util.hpp"
#include <cstdio>
using namespace resourcebroker;
int main(){ Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
 ResourcePoolDescriptor p; p.pool_id=ResourcePoolId(1); p.resource_class=ResourceClass::GPU_MEMORY_BYTES; p.total_governed=ResourceAmount::bytes(Bytes(8ull*1024*1024*1024)); p.provenance=Provenance::SYNTHETIC; b.register_pool(p,CapacityGeneration(1));
 AuthorityEnvelope a; a.coordinator_epoch=b.coordinator_epoch();
 const GrantResult g=b.submit_request(make_req(ResourceRequestId(1),4ull*1024*1024*1024,Priority::NORMAL),a);
 b.activate_reservation(g.reservation_id,a);
 const RecallDescriptor rc=b.issue_recall(g.reservation_id,ResourceAmount::bytes(Bytes(2ull*1024*1024*1024)),"borrow",a);
 b.acknowledge_recall(rc.recall_id,a);
 std::printf("recall state=%s\n",to_string(b.all_recalls()[0].state)); return 0; }
