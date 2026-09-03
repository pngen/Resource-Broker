// authority.hpp - Authority envelopes carried by every mutation.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Authority is incarnation-scoped. A large generation owned by a stale
// WorkerBootId or CoordinatorEpoch is rejected before any generation
// comparison, so it can never fence a fresh process incarnation.
#pragma once

#include "identities.hpp"

namespace resourcebroker {

struct AuthorityEnvelope {
    CoordinatorEpoch coordinator_epoch;
    WorkerId worker;
    WorkerBootId worker_boot;
    BrokerGeneration broker_generation;   // optional; zero when unspecified

    bool has_coordinator() const { return coordinator_epoch.is_valid(); }
    bool has_worker() const { return worker.is_valid(); }
    bool has_boot() const { return worker_boot.is_valid(); }
};

}  // namespace resourcebroker
