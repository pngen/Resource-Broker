// 02_single_resource_reservation.cpp
#include "resourcebroker/broker.hpp"
#include <cstdio>
using namespace resourcebroker;
int main(){ Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
 ResourcePoolDescriptor p; p.pool_id=ResourcePoolId(1); p.resource_class=ResourceClass::HOST_MEMORY_BYTES; p.scope=PoolScope::HOST_LOCAL;
 p.total_governed=ResourceAmount::bytes(Bytes(64ull*1024*1024)); p.provenance=Provenance::MEASURED; b.register_pool(p,CapacityGeneration(1));
 AuthorityEnvelope a; a.coordinator_epoch=b.coordinator_epoch();
 ResourceRequest r; r.request_id=ResourceRequestId(1); r.owner=OwnerId(1); r.tenant=TenantId(1);
 ResourceRequirement q; q.resource_class=ResourceClass::HOST_MEMORY_BYTES; q.requested=ResourceAmount::bytes(Bytes(32ull*1024*1024)); q.minimum=q.requested; q.preferred=q.requested; q.semantics=CapacitySemantics::EXACT; r.requirements.push_back(q);
 GrantResult g=b.submit_request(r,a); std::printf("outcome=%s rid=%llu\n",to_string(g.outcome),(unsigned long long)g.reservation_id.value()); return 0; }
