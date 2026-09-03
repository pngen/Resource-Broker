// example_util.hpp - shared example helpers.
#pragma once
#include "resourcebroker/request.hpp"
#include "resourcebroker/amount.hpp"
inline resourcebroker::ResourceRequest make_req(resourcebroker::ResourceRequestId id, std::uint64_t bytes, resourcebroker::Priority p){
  resourcebroker::ResourceRequest r; r.request_id=id; r.owner=resourcebroker::OwnerId(1); r.tenant=resourcebroker::TenantId(1); r.priority=p;
  resourcebroker::ResourceRequirement q; q.resource_class=resourcebroker::ResourceClass::GPU_MEMORY_BYTES; q.requested=resourcebroker::ResourceAmount::bytes(resourcebroker::Bytes(bytes)); q.minimum=q.requested; q.preferred=q.requested; q.semantics=resourcebroker::CapacitySemantics::EXACT;
  r.requirements.push_back(q); r.reclaimability=resourcebroker::Reclaimability::RECLAIMABLE; return r;
}
