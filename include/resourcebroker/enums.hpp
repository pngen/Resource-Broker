// enums.hpp - Core enums for the resource model.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#pragma once

#include <cstdint>
#include <ostream>

namespace resourcebroker {

// The physical dimension a resource class is measured in. Mixing dimensions
// accidentally is prevented at the type level by the strong units and at the
// model level by this mapping.
enum class Dimension { Bytes, BytesPerSecond, Count, Share, Other };

inline const char* to_string(Dimension d) noexcept {
    switch (d) {
        case Dimension::Bytes: return "bytes";
        case Dimension::BytesPerSecond: return "bytes_per_second";
        case Dimension::Count: return "count";
        case Dimension::Share: return "share";
        case Dimension::Other: return "other";
    }
    return "other";
}

// Heterogeneous resource classes. Designed for extension.
enum class ResourceClass {
    GPU_MEMORY_BYTES,
    GPU_COMPUTE_SHARE,
    HOST_MEMORY_BYTES,
    HOST_PINNED_BYTES,
    CACHE_BYTES,
    MODEL_RESIDENCY_BYTES,
    EXECUTION_SLOTS,
    TRANSFER_BANDWIDTH_BPS,
    STORAGE_BANDWIDTH_BPS,
    NETWORK_BANDWIDTH_BPS,
    CONCURRENT_WORKLOADS,
    GENERIC_SCALAR_RESOURCE
};

inline const char* to_string(ResourceClass c) noexcept {
    switch (c) {
        case ResourceClass::GPU_MEMORY_BYTES: return "GPU_MEMORY_BYTES";
        case ResourceClass::GPU_COMPUTE_SHARE: return "GPU_COMPUTE_SHARE";
        case ResourceClass::HOST_MEMORY_BYTES: return "HOST_MEMORY_BYTES";
        case ResourceClass::HOST_PINNED_BYTES: return "HOST_PINNED_BYTES";
        case ResourceClass::CACHE_BYTES: return "CACHE_BYTES";
        case ResourceClass::MODEL_RESIDENCY_BYTES: return "MODEL_RESIDENCY_BYTES";
        case ResourceClass::EXECUTION_SLOTS: return "EXECUTION_SLOTS";
        case ResourceClass::TRANSFER_BANDWIDTH_BPS: return "TRANSFER_BANDWIDTH_BPS";
        case ResourceClass::STORAGE_BANDWIDTH_BPS: return "STORAGE_BANDWIDTH_BPS";
        case ResourceClass::NETWORK_BANDWIDTH_BPS: return "NETWORK_BANDWIDTH_BPS";
        case ResourceClass::CONCURRENT_WORKLOADS: return "CONCURRENT_WORKLOADS";
        case ResourceClass::GENERIC_SCALAR_RESOURCE: return "GENERIC_SCALAR_RESOURCE";
    }
    return "UNKNOWN_CLASS";
}

// The dimension a resource class is tracked in.
inline Dimension dimension_of(ResourceClass c) noexcept {
    switch (c) {
        case ResourceClass::GPU_MEMORY_BYTES:
        case ResourceClass::HOST_MEMORY_BYTES:
        case ResourceClass::HOST_PINNED_BYTES:
        case ResourceClass::CACHE_BYTES:
        case ResourceClass::MODEL_RESIDENCY_BYTES:
            return Dimension::Bytes;
        case ResourceClass::GPU_COMPUTE_SHARE:
            return Dimension::Share;
        case ResourceClass::EXECUTION_SLOTS:
        case ResourceClass::CONCURRENT_WORKLOADS:
            return Dimension::Count;
        case ResourceClass::TRANSFER_BANDWIDTH_BPS:
        case ResourceClass::STORAGE_BANDWIDTH_BPS:
        case ResourceClass::NETWORK_BANDWIDTH_BPS:
            return Dimension::BytesPerSecond;
        case ResourceClass::GENERIC_SCALAR_RESOURCE:
            return Dimension::Other;
    }
    return Dimension::Other;
}

// Provenance of a capacity, health, or accounting fact. Unknown facts must not
// be presented as measured.
enum class Provenance {
    MEASURED,      // Observed directly from a physical source.
    REPORTED,      // Reported by an external runtime/consumer.
    DERIVED,       // Computed from other facts.
    SYNTHETIC,     // Deterministic stand-in for unavailable infrastructure.
    UNKNOWN
};

inline const char* to_string(Provenance p) noexcept {
    switch (p) {
        case Provenance::MEASURED: return "MEASURED";
        case Provenance::REPORTED: return "REPORTED";
        case Provenance::DERIVED: return "DERIVED";
        case Provenance::SYNTHETIC: return "SYNTHETIC";
        case Provenance::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

enum class Health { HEALTHY, DEGRADED, UNHEALTHY, UNAVAILABLE, UNKNOWN };

inline const char* to_string(Health h) noexcept {
    switch (h) {
        case Health::HEALTHY: return "HEALTHY";
        case Health::DEGRADED: return "DEGRADED";
        case Health::UNHEALTHY: return "UNHEALTHY";
        case Health::UNAVAILABLE: return "UNAVAILABLE";
        case Health::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

enum class Freshness { CURRENT, STALE, REVALIDATION_REQUIRED, UNKNOWN };

inline const char* to_string(Freshness f) noexcept {
    switch (f) {
        case Freshness::CURRENT: return "CURRENT";
        case Freshness::STALE: return "STALE";
        case Freshness::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
        case Freshness::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

enum class PoolScope { DEVICE_LOCAL, NODE_LOCAL, HOST_LOCAL, CLUSTER_CLASS, BACKEND_LOCAL, SYNTHETIC_REMOTE, UNKNOWN };

inline const char* to_string(PoolScope s) noexcept {
    switch (s) {
        case PoolScope::DEVICE_LOCAL: return "DEVICE_LOCAL";
        case PoolScope::NODE_LOCAL: return "NODE_LOCAL";
        case PoolScope::HOST_LOCAL: return "HOST_LOCAL";
        case PoolScope::CLUSTER_CLASS: return "CLUSTER_CLASS";
        case PoolScope::BACKEND_LOCAL: return "BACKEND_LOCAL";
        case PoolScope::SYNTHETIC_REMOTE: return "SYNTHETIC_REMOTE";
        case PoolScope::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

enum class RequirementKind { REQUIRED, PREFERRED, OPTIONAL };

inline const char* to_string(RequirementKind k) noexcept {
    switch (k) {
        case RequirementKind::REQUIRED: return "REQUIRED";
        case RequirementKind::PREFERRED: return "PREFERRED";
        case RequirementKind::OPTIONAL: return "OPTIONAL";
    }
    return "OPTIONAL";
}

enum class CapacitySemantics { EXACT, MINIMUM, UP_TO, ELASTIC_RANGE };

inline const char* to_string(CapacitySemantics s) noexcept {
    switch (s) {
        case CapacitySemantics::EXACT: return "EXACT";
        case CapacitySemantics::MINIMUM: return "MINIMUM";
        case CapacitySemantics::UP_TO: return "UP_TO";
        case CapacitySemantics::ELASTIC_RANGE: return "ELASTIC_RANGE";
    }
    return "EXACT";
}

enum class RequestOutcome { GRANT, GRANT_PARTIAL, DEFER, REJECT, PREEMPT_REQUIRED, RECALL_REQUIRED, INSUFFICIENT_EVIDENCE, UNKNOWN };

inline const char* to_string(RequestOutcome o) noexcept {
    switch (o) {
        case RequestOutcome::GRANT: return "GRANT";
        case RequestOutcome::GRANT_PARTIAL: return "GRANT_PARTIAL";
        case RequestOutcome::DEFER: return "DEFER";
        case RequestOutcome::REJECT: return "REJECT";
        case RequestOutcome::PREEMPT_REQUIRED: return "PREEMPT_REQUIRED";
        case RequestOutcome::RECALL_REQUIRED: return "RECALL_REQUIRED";
        case RequestOutcome::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
        case RequestOutcome::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

enum class ReservationState { PLANNED, RESERVED, ACTIVATING, ACTIVE, RECALL_REQUESTED, PREEMPTING, RELEASING, RELEASED, EXPIRED, REVOKED, STALE, FAILED };

inline const char* to_string(ReservationState s) noexcept {
    switch (s) {
        case ReservationState::PLANNED: return "PLANNED";
        case ReservationState::RESERVED: return "RESERVED";
        case ReservationState::ACTIVATING: return "ACTIVATING";
        case ReservationState::ACTIVE: return "ACTIVE";
        case ReservationState::RECALL_REQUESTED: return "RECALL_REQUESTED";
        case ReservationState::PREEMPTING: return "PREEMPTING";
        case ReservationState::RELEASING: return "RELEASING";
        case ReservationState::RELEASED: return "RELEASED";
        case ReservationState::EXPIRED: return "EXPIRED";
        case ReservationState::REVOKED: return "REVOKED";
        case ReservationState::STALE: return "STALE";
        case ReservationState::FAILED: return "FAILED";
    }
    return "UNKNOWN_STATE";
}

enum class AllocationState { PENDING, ALLOCATED, IN_USE, RECLAIMABLE, RELEASING, RELEASED, STALE, FAILED };

inline const char* to_string(AllocationState s) noexcept {
    switch (s) {
        case AllocationState::PENDING: return "PENDING";
        case AllocationState::ALLOCATED: return "ALLOCATED";
        case AllocationState::IN_USE: return "IN_USE";
        case AllocationState::RECLAIMABLE: return "RECLAIMABLE";
        case AllocationState::RELEASING: return "RELEASING";
        case AllocationState::RELEASED: return "RELEASED";
        case AllocationState::STALE: return "STALE";
        case AllocationState::FAILED: return "FAILED";
    }
    return "UNKNOWN_STATE";
}

enum class LeaseState { ACTIVE, EXPIRED, REVOKED, REVALIDATION_REQUIRED, STALE, TERMINATED };

inline const char* to_string(LeaseState s) noexcept {
    switch (s) {
        case LeaseState::ACTIVE: return "ACTIVE";
        case LeaseState::EXPIRED: return "EXPIRED";
        case LeaseState::REVOKED: return "REVOKED";
        case LeaseState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
        case LeaseState::STALE: return "STALE";
        case LeaseState::TERMINATED: return "TERMINATED";
    }
    return "UNKNOWN_STATE";
}

enum class RecallState { ISSUED, ACKNOWLEDGED, RELEASING, SATISFIED, EXPIRED, FAILED, STALE };

inline const char* to_string(RecallState s) noexcept {
    switch (s) {
        case RecallState::ISSUED: return "ISSUED";
        case RecallState::ACKNOWLEDGED: return "ACKNOWLEDGED";
        case RecallState::RELEASING: return "RELEASING";
        case RecallState::SATISFIED: return "SATISFIED";
        case RecallState::EXPIRED: return "EXPIRED";
        case RecallState::FAILED: return "FAILED";
        case RecallState::STALE: return "STALE";
    }
    return "UNKNOWN_STATE";
}

enum class PreemptionState { PLANNED, ISSUED, ACKNOWLEDGED, RECLAIMING, COMPLETED, FAILED, STALE };

inline const char* to_string(PreemptionState s) noexcept {
    switch (s) {
        case PreemptionState::PLANNED: return "PLANNED";
        case PreemptionState::ISSUED: return "ISSUED";
        case PreemptionState::ACKNOWLEDGED: return "ACKNOWLEDGED";
        case PreemptionState::RECLAIMING: return "RECLAIMING";
        case PreemptionState::COMPLETED: return "COMPLETED";
        case PreemptionState::FAILED: return "FAILED";
        case PreemptionState::STALE: return "STALE";
    }
    return "UNKNOWN_STATE";
}

enum class Priority { CRITICAL, HIGH, NORMAL, LOW, BACKGROUND };

inline const char* to_string(Priority p) noexcept {
    switch (p) {
        case Priority::CRITICAL: return "CRITICAL";
        case Priority::HIGH: return "HIGH";
        case Priority::NORMAL: return "NORMAL";
        case Priority::LOW: return "LOW";
        case Priority::BACKGROUND: return "BACKGROUND";
    }
    return "NORMAL";
}

// Rank used for ordering; larger value = higher priority.
inline int priority_rank(Priority p) noexcept {
    switch (p) {
        case Priority::CRITICAL: return 5;
        case Priority::HIGH: return 4;
        case Priority::NORMAL: return 3;
        case Priority::LOW: return 2;
        case Priority::BACKGROUND: return 1;
    }
    return 3;
}

enum class Reclaimability { NON_RECLAIMABLE, COOPERATIVE, RECLAIMABLE, PREEMPTIBLE };

inline const char* to_string(Reclaimability r) noexcept {
    switch (r) {
        case Reclaimability::NON_RECLAIMABLE: return "NON_RECLAIMABLE";
        case Reclaimability::COOPERATIVE: return "COOPERATIVE";
        case Reclaimability::RECLAIMABLE: return "RECLAIMABLE";
        case Reclaimability::PREEMPTIBLE: return "PREEMPTIBLE";
    }
    return "NON_RECLAIMABLE";
}

// Rank used for ordering; larger value = more reclaimable/less protected.
inline int reclaimability_rank(Reclaimability r) noexcept {
    switch (r) {
        case Reclaimability::NON_RECLAIMABLE: return 0;
        case Reclaimability::COOPERATIVE: return 1;
        case Reclaimability::RECLAIMABLE: return 2;
        case Reclaimability::PREEMPTIBLE: return 3;
    }
    return 0;
}

}  // namespace resourcebroker
