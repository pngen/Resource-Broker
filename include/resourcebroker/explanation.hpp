// explanation.hpp - Explanation records for deterministic, explainable arbitration.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Arbitration is not one opaque master score: named factors, a binding
// constraint, and a decision reason are always produced.
#pragma once

#include <string>
#include <vector>
#include "enums.hpp"
#include "resource.hpp"

namespace resourcebroker {

// A named arbitration factor that contributed to a decision.
struct ArbitrationFactor {
    std::string name;
    std::string value;   // human-readable contribution
    int rank = 0;        // ordering weight; larger counted earlier
};

struct Explanation {
    RequestOutcome outcome = RequestOutcome::UNKNOWN;
    std::string headline;
    std::string binding_constraint;        // resource class / pool that bound
    bool rolled_back_provisional = false;  // true if a provisional hold was undone
    bool all_required_satisfied = false;
    std::string decision_reason;
    std::vector<ArbitrationFactor> factors;
    std::vector<std::string> notes;
};

}  // namespace resourcebroker
