// reclamation.hpp - Capacity-shrink reclamation planning.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// plan_reclamation produces a deterministic, explainable plan to cover a
// deficit; it never preempts protected workloads merely because it is
// convenient.
#pragma once

#include <string>
#include <vector>
#include "amount.hpp"
#include "identities.hpp"
#include "enums.hpp"

namespace resourcebroker {

// One candidate that a reclamation plan proposes to yield.
struct ReclamationCandidate {
    ReservationId reservation_id;
    ResourcePoolId pool_id;
    ResourceClass resource_class = ResourceClass::GENERIC_SCALAR_RESOURCE;
    ResourceAmount reclaimable_amount;
    Priority priority = Priority::NORMAL;
    Reclaimability reclaimability = Reclaimability::COOPERATIVE;
    bool is_protected = false;
    std::uint64_t waiting_age_ns = 0;
    std::string consequence;   // estimated consequence of yielding
};

struct ReclamationPlan {
    ResourcePoolId pool_id;
    ResourceClass resource_class = ResourceClass::GENERIC_SCALAR_RESOURCE;
    ResourceAmount deficit;
    std::vector<ReclamationCandidate> candidates;
    std::vector<ReservationId> chosen;         // deterministic chosen plan
    ResourceAmount covered;
    ResourceAmount unsatisfied;                // leftover deficit
    bool requires_preemption = false;
    std::string rationale;
};

}  // namespace resourcebroker
