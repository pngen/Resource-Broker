#include "resourcebroker/identities.hpp"
#include "resourcebroker/units.hpp"
#include "resourcebroker/amount.hpp"
#include "resourcebroker/enums.hpp"
#include "resourcebroker/capacity.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/reservation.hpp"
#include "resourcebroker/authority.hpp"
#include "resourcebroker/policy.hpp"
#include "resourcebroker/explanation.hpp"
#include "resourcebroker/reclamation.hpp"
#include "resourcebroker/locality.hpp"
#include "resourcebroker/digest.hpp"
#include "resourcebroker/error.hpp"

#include <cstdio>
#include <cstdint>

static void step(const char* m) { std::fputs(m, stderr); std::fflush(stderr); }

int main() {
    using namespace resourcebroker;
    step("A\n");
    const auto g = std::int64_t(4) * 1024 * 1024 * 1024;
    Bytes gpu(static_cast<std::uint64_t>(g));
    Bytes pinned(512u * 1024u * 1024u);
    step("B\n");
    const auto gov = ResourceAmount::bytes(Bytes(static_cast<std::uint64_t>(8) * 1024 * 1024 * 1024));
    CapacityLedger ledger(ResourceClass::GPU_MEMORY_BYTES, gov, Provenance::MEASURED);
    step("C\n");
    const auto req = ResourceAmount::bytes(Bytes(static_cast<std::uint64_t>(4) * 1024 * 1024 * 1024));
    ledger.reserve(req);
    step("D\n");
    ledger.allocate(req);
    step("E\n");
    ledger.mark_reclaimable(ResourceAmount::bytes(Bytes(static_cast<std::uint64_t>(2) * 1024 * 1024 * 1024)));
    step("F\n");
    ledger.release_allocated(req);
    step("G\n");
    std::fprintf(stderr, "free=%s governed=%s\n", ledger.free().to_string().c_str(), ledger.governed().to_string().c_str());
    std::fflush(stderr);
    if (ledger.free() != gov) { step("FAIL free\n"); return 2; }
    step("OK\n");
    return 0;
}
