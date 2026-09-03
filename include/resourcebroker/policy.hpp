// policy.hpp - Versioned broker policy governing arbitration.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Policy changes carry a PolicyGeneration; a stale policy decision cannot
// mutate current reservation authority.
#pragma once

#include "identities.hpp"
#include "units.hpp"

namespace resourcebroker {

struct BrokerPolicy {
    PolicyId policy_id;
    PolicyGeneration policy_generation;

    // Priority does not automatically override hard guarantees.
    bool priority_overrides_hard_guarantee = false;
    // A lower-priority reservation protected by explicit non-preemptible
    // policy remains protected.
    bool honor_non_preemptible_protection = true;

    // Bounded soft overcommit in fraction of governed capacity [0.0, 1.0].
    double max_soft_overcommit = 0.0;  // 0 => overcommit not permitted

    // Lease defaults (nanoseconds). Zero uses broker defaults.
    std::int64_t default_lease_ns = 30LL * 1000000000LL;         // 30s
    std::int64_t lease_renewal_grace_ns = 5LL * 1000000000LL;    // 5s
    bool leases_required_for_live = true;

    // Preemption is a broker-level authority decision; it never implies
    // arbitrary process termination by itself.
    bool preemption_enabled = true;
    bool preemption_requires_policy_authority = true;

    // Starvation policy: after this many nanoseconds waiting, an eligible
    // reservation receives a deterministic starvation boost.
    std::int64_t starvation_threshold_ns = 10LL * 1000000000LL;  // 10s
    int starvation_boost_priority_ranks = 0;   // 0..2

    // Reservation limits.
    std::uint64_t max_reservations = 100000;
    std::uint64_t max_active_reservations = 50000;
    std::uint64_t max_allocations = 500000;

    // Recovery policy.
    bool preserve_logical_reservations_on_recovery = true;
    bool require_revalidation_on_recovery = true;
    bool auto_reclaim_dead_worker_reservations = true;

    // Elastic-grant policy: GRANT_PARTIAL is only valid for elastic requests.
    bool allow_partial_grant = true;
    bool allow_unknown_capacity_grant = false;  // never silently grant on UNKNOWN
};

}  // namespace resourcebroker
