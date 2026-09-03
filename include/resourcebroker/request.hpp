// request.hpp - Resource requirements, requests, and owner descriptors.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#pragma once

#include <vector>
#include <optional>
#include "amount.hpp"
#include "enums.hpp"
#include "identities.hpp"
#include "locality.hpp"
#include "error.hpp"

namespace resourcebroker {

// A single resource requirement within a request.
struct ResourceRequirement {
    ResourceClass resource_class = ResourceClass::GENERIC_SCALAR_RESOURCE;
    ResourceClassId class_id;
    RequirementKind kind = RequirementKind::REQUIRED;
    CapacitySemantics semantics = CapacitySemantics::EXACT;
    ResourceAmount requested;          // the primary amount asked for
    ResourceAmount minimum;            // floor for ELASTIC_RANGE / MINIMUM
    ResourceAmount preferred;          // ceiling for UP_TO / ELASTIC_RANGE
    ResourcePoolId pool_hint;          // optional pool restriction
    NodeId node_hint;                  // optional locality
    DeviceId device_hint;              // optional device requirement
    BackendId backend_hint;            // optional backend requirement
    LocalityRange locality = LocalityRange::UNKNOWN;  // see locality.hpp

    // Validate internal consistency; throws on malformed requirement.
    void validate() const;
};

// An owner that submits work; references tenant/entitlement metadata without
// implementing a full quota system (that is Quota Fabric).
struct OwnerDescriptor {
    OwnerId owner_id;
    TenantId tenant;
    Priority priority_class = Priority::NORMAL;
    PolicyId policy;
    Reclaimability default_reclaimability = Reclaimability::COOPERATIVE;
    bool preemption_eligible = true;
    OwnerGeneration owner_generation;
    Provenance provenance = Provenance::REPORTED;
};

// A request for heterogeneous resources, evaluated atomically when required.
struct ResourceRequest {
    ResourceRequestId request_id;
    RequestGeneration request_generation;
    OwnerId owner;
    TenantId tenant;
    WorkloadId workload;
    WorkloadGeneration workload_generation;
    std::vector<ResourceRequirement> requirements;
    Priority priority = Priority::NORMAL;
    bool urgent = false;
    Duration deadline;                  // zero => no explicit deadline
    Duration duration_expectation;      // zero => no expectation
    bool requires_lease = false;
    Duration lease_duration;            // zero => broker default
    Reclaimability reclaimability = Reclaimability::COOPERATIVE;
    bool preemption_eligible = true;
    bool atomic = true;                 // all-or-nothing when true
    PolicyGeneration policy_generation;
    AuthorityGeneration authority;
    Provenance provenance = Provenance::REPORTED;
    std::string description;            // human-readable origin
};

// Validate a single requirement's internal consistency.
inline void ResourceRequirement::validate() const {
    if (requested.dimension() != dimension_of(resource_class)) {
        throw BrokerError(ErrorCode::InvalidArgument, "requirement dimension mismatch");
    }
    const bool is_exact = (semantics == CapacitySemantics::EXACT);
    const bool up_to = (semantics == CapacitySemantics::UP_TO);
    const bool is_elastic = (semantics == CapacitySemantics::ELASTIC_RANGE);
    const bool is_minimum = (semantics == CapacitySemantics::MINIMUM);
    if (is_exact) {
        if (!(minimum == requested) || !(preferred == requested)) {
            throw BrokerError(ErrorCode::InvalidArgument, "EXACT requires minimum=preferred=requested");
        }
    }
    if (is_elastic && !(minimum <= requested)) {
        throw BrokerError(ErrorCode::InvalidArgument, "ELASTIC_RANGE requires minimum <= requested");
    }
    if (up_to && !(preferred == requested)) {
        throw BrokerError(ErrorCode::InvalidArgument, "UP_TO requires preferred == requested");
    }
    if (is_minimum && !(minimum <= requested)) {
        throw BrokerError(ErrorCode::InvalidArgument, "MINIMUM requires minimum <= requested");
    }
    if (requested.is_zero()) {
        throw BrokerError(ErrorCode::InvalidArgument, "requirement amount must be positive");
    }
}

}  // namespace resourcebroker
