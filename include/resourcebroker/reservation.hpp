// reservation.hpp - Reservation, allocation, lease, recall, preemption records.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// A reservation reserves capacity; an allocation binds physical/logical
// resources. These are distinct and tracked separately.
#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include "amount.hpp"
#include "enums.hpp"
#include "identities.hpp"
#include "authority.hpp"
#include "digest.hpp"

namespace resourcebroker {

// One class/pool claim within a reservation.
struct ReservationClaim {
    ResourceClassId class_id;
    ResourceClass resource_class = ResourceClass::GENERIC_SCALAR_RESOURCE;
    ResourcePoolId pool_id;
    ResourceAmount amount;
    ResourceAmount granted_amount;   // could be less than amount for elastic
    ResourceAmount allocated_amount;    // portion physically bound so far
};

struct ReservationDescriptor {
    ReservationId reservation_id;
    ResourceRequestId request_id;
    OwnerId owner;
    TenantId tenant;
    WorkloadId workload;
    ReservationGeneration reservation_generation;
    std::vector<ReservationClaim> claims;
    std::int64_t issued_at_ms = 0;
    std::int64_t expires_at_ms = 0;      // 0 => no expiry
    Priority priority = Priority::NORMAL;
    Reclaimability reclaimability = Reclaimability::COOPERATIVE;
    bool preemption_eligible = true;
    ReservationState state = ReservationState::PLANNED;
    AuthorityEnvelope authority;
    Provenance provenance = Provenance::REPORTED;
    SemanticDigest semantic_digest{};
};

struct AllocationDescriptor {
    AllocationId allocation_id;
    ReservationId reservation_id;
    ResourceInstanceId instance_id;
    ResourceAmount amount;
    AllocationGeneration allocation_generation;
    AllocationState state = AllocationState::PENDING;
    AuthorityEnvelope authority;
    std::string physical_evidence;   // opaque token supplied by the consumer
    Freshness freshness = Freshness::CURRENT;
    Provenance provenance = Provenance::REPORTED;
};

struct LeaseDescriptor {
    LeaseId lease_id;
    ReservationId reservation_id;
    LeaseGeneration lease_generation;
    WorkerBootId worker_boot;
    std::int64_t issued_at_ms = 0;
    std::int64_t renews_at_ms = 0;   // next expected renewal
    std::int64_t expires_at_ms = 0;  // 0 => no expiry
    LeaseState state = LeaseState::ACTIVE;
    Freshness freshness = Freshness::CURRENT;
    AuthorityEnvelope authority;
    Provenance provenance = Provenance::REPORTED;
};

struct RecallDescriptor {
    RecallId recall_id;
    ReservationId reservation_id;
    AllocationId allocation_id;
    ResourceClass resource_class = ResourceClass::GENERIC_SCALAR_RESOURCE;
    ResourceAmount amount;
    std::string reason;
    RecallGeneration recall_generation;
    bool urgent = false;
    RecallState state = RecallState::ISSUED;
    AuthorityEnvelope authority;
    Provenance provenance = Provenance::DERIVED;
};

struct PreemptionDescriptor {
    PreemptionId preemption_id;
    ReservationId reservation_id;
    ResourceClass resource_class = ResourceClass::GENERIC_SCALAR_RESOURCE;
    ResourceAmount amount;
    std::string reason;
    PreemptionGeneration preemption_generation;
    PreemptionState state = PreemptionState::PLANNED;
    AuthorityEnvelope authority;
    Provenance provenance = Provenance::DERIVED;
};

}  // namespace resourcebroker
