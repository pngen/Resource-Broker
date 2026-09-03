// resource.hpp - Resource, pool, and fragmentation descriptors.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Descriptors are immutable snapshots of broker state produced for queries,
// persistence, and protocol messages. Internal mutable state lives in the
// Broker; these views are canonical records.
#pragma once

#include <vector>
#include "amount.hpp"
#include "enums.hpp"
#include "identities.hpp"
#include "digest.hpp"

namespace resourcebroker {


// Aggregate reserve/allocate state for a governed dimension.
struct CapacityView {
    ResourceAmount nominal;
    ResourceAmount reported;
    ResourceAmount measured;
    ResourceAmount governed;
    ResourceAmount reserved;
    ResourceAmount allocated;
    ResourceAmount reclaimable;
    ResourceAmount unavailable;
    ResourceAmount free;
};

// An individual governed resource instance.
struct ResourceDescriptor {
    ResourceInstanceId instance_id;
    ResourceClassId class_id;
    ResourceClass resource_class = ResourceClass::GENERIC_SCALAR_RESOURCE;
    NodeId node;
    DeviceId device;
    BackendId backend;
    ResourcePoolId pool_id;   // owning pool
    ResourceGeneration resource_generation;
    CapacityGeneration capacity_generation;
    ResourceAmount nominal;
    ResourceAmount governed;
    ResourceAmount available;
    ResourceAmount reserved;
    ResourceAmount allocated;
    ResourceAmount reclaimable;
    Health health = Health::UNKNOWN;
    Freshness freshness = Freshness::UNKNOWN;
    Provenance provenance = Provenance::UNKNOWN;
    PolicyGeneration policy_generation;
    SemanticDigest semantic_digest{};
};

// A pool groups compatible resources of a single class.
struct ResourcePoolDescriptor {
    ResourcePoolId pool_id;
    ResourcePoolGeneration pool_generation;
    ResourceClass resource_class = ResourceClass::GENERIC_SCALAR_RESOURCE;
    PoolScope scope = PoolScope::UNKNOWN;
    std::vector<ResourceInstanceId> members;
    ResourceAmount total_governed;
    ResourceAmount total_reserved;
    ResourceAmount total_allocated;
    Health health = Health::UNKNOWN;
    Freshness freshness = Freshness::UNKNOWN;
    Provenance provenance = Provenance::UNKNOWN;
    PolicyId policy;
};

// Basic fragmentation view for integer/byte resources where it is meaningful.
// Contiguity that cannot be known from available APIs stays UNKNOWN.
struct FragmentationDescriptor {
    ResourcePoolId pool_id;
    ResourceAmount total_free;
    ResourceAmount largest_allocatable_contiguous;  // UNKNOWN if not knowable
    std::uint64_t reserved_fragments = 0;
    std::uint64_t allocation_count = 0;
    bool contiguous_known = false;  // false => largest_* is UNKNOWN
    Provenance provenance = Provenance::UNKNOWN;
};

}  // namespace resourcebroker
