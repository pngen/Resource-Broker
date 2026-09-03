// broker.cpp - Resource Broker coordinator implementation.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/broker.hpp"
#include "resourcebroker/persistence.hpp"
#include "resourcebroker/serdes.hpp"
#include "resourcebroker/digest.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <unordered_set>

namespace resourcebroker {
namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

ResourceAmount amt_min(const ResourceAmount& a, const ResourceAmount& b) { return a < b ? a : b; }
ResourceAmount amt_max(const ResourceAmount& a, const ResourceAmount& b) { return a > b ? a : b; }

}  // namespace

struct Broker::Impl {
    BrokerId broker_id;
    CoordinatorEpoch epoch;
    BrokerPolicy policy;

    std::uint64_t next_pool = 1, next_resource = 1, next_request = 1;
    std::uint64_t next_reservation = 1, next_allocation = 1, next_lease = 1;
    std::uint64_t next_recall = 1, next_preemption = 1, next_owner = 1;
    std::uint64_t next_worker = 1, next_worker_boot = 1, next_class = 1;
    std::uint64_t next_policy = 1, next_node = 1, next_device = 1;
    std::uint64_t next_backend = 1, next_attempt = 1, next_dispatch = 1;
    std::uint64_t next_observation = 1, next_reclamation = 1;
    std::uint64_t broker_generation = 1;

    struct PoolState { ResourcePoolDescriptor desc; CapacityLedger ledger; };
    struct ResourceState { ResourceDescriptor desc; CapacityLedger ledger; };
    struct ReservationRec { ReservationDescriptor desc; };
    struct AllocationRec { AllocationDescriptor desc; };
    struct LeaseRec { LeaseDescriptor desc; };
    struct RecallRec { RecallDescriptor desc; };
    struct PreemptionRec { PreemptionDescriptor desc; };

    std::unordered_map<ResourcePoolId, PoolState> pools;
    std::unordered_map<ResourceInstanceId, ResourceState> resources;
    std::unordered_map<ReservationId, ReservationRec> reservations;
    std::unordered_map<AllocationId, AllocationRec> allocations;
    std::unordered_map<LeaseId, LeaseRec> leases;
    std::unordered_map<RecallId, RecallRec> recalls;
    std::unordered_map<PreemptionId, PreemptionRec> preemptions;
    std::unordered_map<OwnerId, OwnerDescriptor> owners;
    std::unordered_map<WorkerBootId, WorkerId> live_workers;
    std::unordered_map<ResourceClass, ResourcePoolId> default_pool_by_class;

    Impl(CoordinatorEpoch epoch_, BrokerId id)
        : broker_id(id), epoch(epoch_) {
        policy.policy_id = PolicyId(1);
        policy.policy_generation = PolicyGeneration(1);
    }

    // ---- validation / helpers ----
    void validate_authority(const AuthorityEnvelope& auth, bool allow_no_boot = true) const {
        if (auth.coordinator_epoch.is_valid() && auth.coordinator_epoch != epoch) {
            throw BrokerError(ErrorCode::StaleAuthority, "stale coordinator epoch");
        }
        if (auth.worker_boot.is_valid()) {
            if (!live_workers.count(auth.worker_boot)) {
                throw BrokerError(ErrorCode::StaleAuthority, "stale worker boot (not live)");
            }
        }
        (void)allow_no_boot;
    }

    // Reject stale generation against a current object generation.
    template <typename Gen>
    static void check_generation(Gen current, Gen incoming, const char* what) {
        if (incoming.is_valid() && current.is_valid() && incoming < current) {
            throw BrokerError(ErrorCode::GenerationRegression, what);
        }
    }

    static ResourceAmount class_zero(ResourceClass cls) { return ResourceAmount::of_class(cls, 0); }

    ResourcePoolId resolve_pool(ResourceClass cls, const ResourceRequirement& req) const {
        if (req.pool_hint.is_valid()) {
            auto it = pools.find(req.pool_hint);
            if (it == pools.end()) throw BrokerError(ErrorCode::NotFound, "pool_hint not found");
            if (it->second.desc.resource_class != cls)
                throw BrokerError(ErrorCode::InvalidArgument, "pool_hint class mismatch");
            return req.pool_hint;
        }
        auto it = default_pool_by_class.find(cls);
        if (it == default_pool_by_class.end()) return ResourcePoolId{};
        return it->second;
    }

    // Determine the amount to grant for one requirement given a ledger's free
    // capacity. Sets hard_blocked for REQUIRED hard constraints that cannot be
    // met. Returns the grant amount (may be less than requested only when
    // semantics allow).
    ResourceAmount grant_for_requirement(const ResourceRequirement& req, const CapacityLedger& ledger,
                                       bool& hard_blocked, bool& partial) const {
        hard_blocked = false; partial = false;
        const ResourceAmount free = ledger.free();
        const bool is_hard = (req.kind == RequirementKind::REQUIRED);
        switch (req.semantics) {
            case CapacitySemantics::EXACT:
                if (free >= req.requested) return req.requested;
                if (is_hard) { hard_blocked = true; }
                return free;
            case CapacitySemantics::MINIMUM:
                if (free >= req.minimum) {
                    ResourceAmount g = amt_min(free, req.requested);
                    if (g < req.minimum) g = req.minimum;
                    if (g < req.requested) partial = true;
                    return g;
                }
                if (is_hard) { hard_blocked = true; }
                return free;
            case CapacitySemantics::UP_TO:
                {   ResourceAmount g = amt_min(free, req.preferred);
                    if (g < req.preferred) partial = true;
                    return g; }
            case CapacitySemantics::ELASTIC_RANGE:
                if (free >= req.minimum) {
                    ResourceAmount g = amt_max(req.minimum, amt_min(free, req.preferred));
                    if (g < req.preferred) partial = true;
                    return g;
                }
                if (is_hard) { hard_blocked = true; }
                return free;
        }
        return class_zero(req.resource_class);
    }

    // The hard floor an unsatisfiable requirement demands (used for shortfall).
    ResourceAmount hard_floor(const ResourceRequirement& req) const {
        switch (req.semantics) {
            case CapacitySemantics::EXACT: return req.requested;
            case CapacitySemantics::MINIMUM: return req.minimum;
            case CapacitySemantics::UP_TO: return class_zero(req.resource_class);
            case CapacitySemantics::ELASTIC_RANGE: return req.minimum;
        }
        return class_zero(req.resource_class);
    }

    void validate_request(const ResourceRequest& req) const {
        if (req.requirements.empty()) throw BrokerError(ErrorCode::InvalidArgument, "request has no requirements");
        std::unordered_set<ResourceClass> seen;
        for (const auto& r : req.requirements) {
            r.validate();
            if (!seen.insert(r.resource_class).second) {
                throw BrokerError(ErrorCode::InvalidArgument, "duplicate resource class dimension");
            }
        }
    }

    void rollback_holds(std::vector<ProvisionalHold>& holds) {
        for (auto it = holds.rbegin(); it != holds.rend(); ++it) {
            auto pit = pools.find(it->pool_id);
            if (pit != pools.end()) pit->second.ledger.release_reserved(it->amount);
        }
        holds.clear();
    }

    // Choose deterministic reclamation targets to cover `shortfall` on a pool.
    // Never selects protected (non-reclaimable) reservations unless no alternative
    // and policy explicitly permits preemption of protected work.
    bool select_reclamation_targets(ResourcePoolId pool_id, const ResourceAmount& shortfall,
                                   ReservationId exclude, bool allow_protected,
                                   std::vector<ReservationId>& chosen, ResourceAmount& covered) const {
        chosen.clear();
        covered = class_zero(pool_by_id(pool_id).desc.resource_class);
        const ResourceClass cls = pool_by_id(pool_id).desc.resource_class;

        struct Cand { ReservationId id; ResourceAmount amount; int priority; int reclaim; std::uint64_t age; bool prot; bool preempt; };
        std::vector<Cand> cands;
        for (const auto& [rid, rsv] : reservations) {
            if (rid == exclude) continue;
            if (rsv.desc.state == ReservationState::RELEASED || rsv.desc.state == ReservationState::EXPIRED ||
                rsv.desc.state == ReservationState::REVOKED || rsv.desc.state == ReservationState::STALE) continue;
            for (const auto& claim : rsv.desc.claims) {
                if (claim.pool_id != pool_id) continue;
                if (claim.allocated_amount.is_zero() && claim.granted_amount.is_zero()) continue;
                Cand c; c.id = rid;
                c.amount = claim.allocated_amount.is_zero() ? claim.granted_amount : claim.allocated_amount;
                c.priority = priority_rank(rsv.desc.priority);
                c.reclaim = reclaimability_rank(rsv.desc.reclaimability);
                c.age = 0;  // starvation age recorded elsewhere for determinism
                c.prot = (rsv.desc.reclaimability == Reclaimability::NON_RECLAIMABLE);
                c.preempt = rsv.desc.preemption_eligible;
                cands.push_back(c);
            }
        }

        // Deterministic ordering: lowest priority first; most reclaimable first;
        // then reservation id ascending for a stable tie-break.
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            if (a.priority != b.priority) return a.priority < b.priority;
            if (a.reclaim != b.reclaim) return a.reclaim > b.reclaim;
            return a.id < b.id;
        });

        ResourceAmount need = shortfall;
        for (const auto& c : cands) {
            if (!(need > covered)) break;
            if (c.prot && !allow_protected) continue;
            chosen.push_back(c.id);
            covered = covered + c.amount;
        }
        (void)cls;
        return covered >= shortfall;
    }

    const PoolState& pool_by_id(ResourcePoolId id) const {
        auto it = pools.find(id);
        if (it == pools.end()) throw BrokerError(ErrorCode::NotFound, "pool not found");
        return it->second;
    }
    PoolState& pool_by_id(ResourcePoolId id) {
        auto it = pools.find(id);
        if (it == pools.end()) throw BrokerError(ErrorCode::NotFound, "pool not found");
        return it->second;
    }

    ResourcePoolDescriptor build_pool_descriptor(ResourcePoolId id) const {
        const PoolState& ps = pool_by_id(id);
        ResourcePoolDescriptor d = ps.desc;
        d.total_governed = ps.ledger.governed();
        d.total_reserved = ps.ledger.reserved();
        d.total_allocated = ps.ledger.allocated();
        return d;
    }

    ResourceDescriptor build_resource_descriptor(ResourceInstanceId id) const {
        const ResourceState& rs = resources.at(id);
        ResourceDescriptor d = rs.desc;
        d.nominal = rs.ledger.nominal();
        d.governed = rs.ledger.governed();
        d.available = rs.ledger.free();
        d.reserved = rs.ledger.reserved();
        d.allocated = rs.ledger.allocated();
        d.reclaimable = rs.ledger.reclaimable();
        return d;
    }

    GrantResult do_submit_request(const ResourceRequest& request, const AuthorityEnvelope& auth);
    Explanation do_explain_request(const ResourceRequest& request, const AuthorityEnvelope& auth);
    void do_activate(ReservationId res, const AuthorityEnvelope& auth);
    void do_release_reservation(ReservationId res, const AuthorityEnvelope& auth);

    void serialize(std::vector<std::uint8_t>& out) const;
    void deserialize(const std::vector<std::uint8_t>& in);

};  // struct Broker::Impl

// ---- Broker::Impl core operations ----
GrantResult Broker::Impl::do_submit_request(const ResourceRequest& request, const AuthorityEnvelope& auth) {
    GrantResult result;
    result.outcome = RequestOutcome::UNKNOWN;
    validate_authority(auth, true);
    validate_request(request);

    std::vector<ProvisionalHold> holds;
    std::vector<ResourceAmount> grants;
    bool any_partial = false;

    // Reserve provisionally per requirement, in deterministic order. If any
    // REQUIRED hard constraint cannot be met, roll back every earlier hold.
    for (const auto& req : request.requirements) {
        const ResourceClass cls = req.resource_class;
        const ResourcePoolId pid = resolve_pool(cls, req);
        if (!pid.is_valid()) {
            rollback_holds(holds);
            result.outcome = RequestOutcome::INSUFFICIENT_EVIDENCE;
            result.explanation.outcome = result.outcome;
            result.explanation.headline = "No pool registered for " + std::string(to_string(cls)) + "; capacity unknown";
            result.explanation.binding_constraint = to_string(cls);
            result.explanation.notes.push_back("Unknown capacity never produces a silent grant.");
            return result;
        }
        PoolState& ps = pools.at(pid);
        bool hard_blocked = false;
        bool partial = false;
        const ResourceAmount grant = grant_for_requirement(req, ps.ledger, hard_blocked, partial);
        if (hard_blocked) {
            const ResourceAmount floor = hard_floor(req);
            const ResourceAmount shortfall = floor - grant;
            std::vector<ReservationId> targets;
            ResourceAmount covered;
            const bool can = select_reclamation_targets(pid, shortfall, ReservationId{}, policy.preemption_enabled, targets, covered);
            rollback_holds(holds);
            result.explanation.binding_constraint = std::string(to_string(cls));
            result.explanation.factors.push_back({"priority", to_string(request.priority), priority_rank(request.priority)});
            if (can && !targets.empty()) {
                bool any_preempt = false;
                for (const auto& t : targets) {
                    auto it = reservations.find(t);
                    if (it != reservations.end() && it->second.desc.preemption_eligible) any_preempt = true;
                }
                result.outcome = any_preempt ? RequestOutcome::PREEMPT_REQUIRED : RequestOutcome::RECALL_REQUIRED;
                result.explanation.outcome = result.outcome;
                result.explanation.headline = std::string(to_string(result.outcome)) + ": " + std::to_string(targets.size()) + " reservation(s) must yield for " + to_string(cls);
                for (const auto& t : targets) {
                    result.explanation.notes.push_back("target reservation " + std::to_string(t.value()));
                }
            } else {
                result.outcome = RequestOutcome::DEFER;
                result.explanation.outcome = result.outcome;
                result.explanation.headline = "Insufficient " + std::string(to_string(cls)) + "; provisional holds rolled back";
                result.explanation.rolled_back_provisional = !holds.empty();
            }
            result.explanation.decision_reason = "Binding constraint: " + std::string(to_string(cls));
            return result;
        }
        if (grant > class_zero(cls)) {
            ps.ledger.reserve(grant);
            holds.push_back(ProvisionalHold{pid, grant});
        }
        grants.push_back(grant);
        if (partial) any_partial = true;
    }

    // Commit the reservation atomically.
    ReservationDescriptor desc;
    desc.reservation_id = ReservationId(next_reservation++);
    desc.request_id = request.request_id;
    desc.owner = request.owner;
    desc.tenant = request.tenant;
    desc.workload = request.workload;
    desc.reservation_generation = ReservationGeneration(1);
    desc.issued_at_ms = now_ms();
    if (request.duration_expectation.nanoseconds() > 0) {
        desc.expires_at_ms = desc.issued_at_ms + request.duration_expectation.nanoseconds() / 1000000LL;
    }
    desc.priority = request.priority;
    desc.reclaimability = request.reclaimability;
    desc.preemption_eligible = request.preemption_eligible;
    desc.state = ReservationState::RESERVED;
    desc.authority = auth;
    desc.provenance = request.provenance;
    for (std::size_t i = 0; i < request.requirements.size(); ++i) {
        const auto& req = request.requirements[i];
        const ResourcePoolId pid = resolve_pool(req.resource_class, req);
        ReservationClaim claim;
        claim.class_id = req.class_id;
        claim.resource_class = req.resource_class;
        claim.pool_id = pid;
        claim.amount = req.requested;
        claim.granted_amount = grants[i];
        claim.allocated_amount = ResourceAmount::of_class(req.resource_class, 0);
        desc.claims.push_back(claim);
    }
    reservations[desc.reservation_id] = ReservationRec{desc};
    result.reservation_id = desc.reservation_id;
    result.outcome = any_partial ? RequestOutcome::GRANT_PARTIAL : RequestOutcome::GRANT;
    result.explanation.outcome = result.outcome;
    result.explanation.headline = result.outcome == RequestOutcome::GRANT_PARTIAL
        ? "Granted elastically (partial)"
        : "Granted reservation " + std::to_string(desc.reservation_id.value());
    result.explanation.all_required_satisfied = true;
    result.explanation.binding_constraint = "none";
    result.explanation.notes.push_back("All required dimensions reserved atomically; accounting invariant preserved.");
    return result;
}

Explanation Broker::Impl::do_explain_request(const ResourceRequest& request, const AuthorityEnvelope& auth) {
    Explanation ex;
    ex.outcome = RequestOutcome::UNKNOWN;
    validate_authority(auth, true);
    validate_request(request);
    bool any_partial = false;
    std::string binding = "none";
    const ResourceAmount zero_gpu = ResourceAmount::of_class(ResourceClass::GPU_MEMORY_BYTES, 0);
    (void)zero_gpu;
    for (const auto& req : request.requirements) {
        const ResourcePoolId pid = resolve_pool(req.resource_class, req);
        if (!pid.is_valid()) {
            ex.outcome = RequestOutcome::INSUFFICIENT_EVIDENCE;
            ex.headline = "No pool for " + std::string(to_string(req.resource_class)) + "; capacity unknown";
            ex.binding_constraint = to_string(req.resource_class);
            ex.decision_reason = "Unknown capacity never yields a grant.";
            return ex;
        }
        const PoolState& ps = pools.at(pid);
        bool hard_blocked = false;
        bool partial = false;
        ResourceAmount g = grant_for_requirement(req, ps.ledger, hard_blocked, partial);
        if (hard_blocked) {
            ex.outcome = RequestOutcome::DEFER;
            ex.headline = "Deferred: " + std::string(to_string(req.resource_class)) + " insufficient";
            ex.binding_constraint = to_string(req.resource_class);
            ex.decision_reason = "Hard constraint cannot be met under current capacity.";
            return ex;
        }
        ex.factors.push_back({std::string(to_string(req.resource_class)) + "_available", g.to_string(), 1});
        if (partial) any_partial = true;
        (void)g;
    }
    if (any_partial) { ex.outcome = RequestOutcome::GRANT_PARTIAL; ex.headline = "Would grant partially (elastic)"; }
    else { ex.outcome = RequestOutcome::GRANT; ex.headline = "Would grant"; }
    ex.all_required_satisfied = true;
    ex.binding_constraint = binding;
    return ex;
}

void Broker::Impl::do_activate(ReservationId res, const AuthorityEnvelope& auth) {
    validate_authority(auth, true);
    auto it = reservations.find(res);
    if (it == reservations.end()) throw BrokerError(ErrorCode::NotFound, "reservation not found");
    auto& rsv = it->second;
    if (rsv.desc.state != ReservationState::RESERVED && rsv.desc.state != ReservationState::PLANNED) {
        throw BrokerError(ErrorCode::InvalidState, "reservation cannot be activated from current state");
    }
    rsv.desc.state = ReservationState::ACTIVE;
}

void Broker::Impl::do_release_reservation(ReservationId res, const AuthorityEnvelope& auth) {
    validate_authority(auth, true);
    auto it = reservations.find(res);
    if (it == reservations.end()) throw BrokerError(ErrorCode::NotFound, "reservation not found");
    auto& rsv = it->second;
    if (rsv.desc.state == ReservationState::RELEASED || rsv.desc.state == ReservationState::STALE || rsv.desc.state == ReservationState::REVOKED) {
        return;  // idempotent release is safe; duplicate release never double-credits.
    }
    for (auto& claim : rsv.desc.claims) {
        auto pit = pools.find(claim.pool_id);
        if (pit != pools.end()) {
            const ResourceAmount rem_alloc = claim.allocated_amount;
            const ResourceAmount rem_reserved = claim.granted_amount - rem_alloc;
            if (rem_alloc > ResourceAmount::of_class(claim.resource_class, 0)) pit->second.ledger.release_allocated(rem_alloc);
            if (rem_reserved > ResourceAmount::of_class(claim.resource_class, 0)) pit->second.ledger.release_reserved(rem_reserved);
        }
        claim.granted_amount = ResourceAmount::of_class(claim.resource_class, 0);
        claim.allocated_amount = ResourceAmount::of_class(claim.resource_class, 0);
    }
    // Release any remaining allocations that reference this reservation.
    for (auto& [aid, a] : allocations) {
        if (a.desc.reservation_id == res && a.desc.state == AllocationState::ALLOCATED) {
            auto rit = resources.find(a.desc.instance_id);
            if (rit != resources.end()) rit->second.ledger.release_allocated(a.desc.amount);
            a.desc.state = AllocationState::RELEASED;
        }
    }
    rsv.desc.state = ReservationState::RELEASED;
}


// ---- Broker public methods ----
Broker::Broker(CoordinatorEpoch epoch, BrokerId id) : impl_(std::make_unique<Impl>(epoch, id)) {}
Broker::~Broker() = default;
BrokerId Broker::id() const { std::lock_guard<std::mutex> lk(mu_); return impl_->broker_id; }
CoordinatorEpoch Broker::coordinator_epoch() const { std::lock_guard<std::mutex> lk(mu_); return impl_->epoch; }
BrokerGeneration Broker::broker_generation() const { std::lock_guard<std::mutex> lk(mu_); return BrokerGeneration(impl_->broker_generation); }

void Broker::set_policy(const BrokerPolicy& policy) { std::lock_guard<std::mutex> lk(mu_); impl_->policy = policy; }
BrokerPolicy Broker::policy() const { std::lock_guard<std::mutex> lk(mu_); return impl_->policy; }

WorkerBootId Broker::register_worker(WorkerId worker) {
    std::lock_guard<std::mutex> lk(mu_);
    const WorkerBootId boot(impl_->next_worker_boot++);
    impl_->live_workers[boot] = worker;
    ++impl_->broker_generation;
    return boot;
}
bool Broker::is_worker_live(WorkerBootId boot) const { std::lock_guard<std::mutex> lk(mu_); return impl_->live_workers.count(boot) != 0; }
std::vector<WorkerBootId> Broker::live_workers() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<WorkerBootId> v;
    v.reserve(impl_->live_workers.size());
    for (const auto& kv : impl_->live_workers) v.push_back(kv.first);
    std::sort(v.begin(), v.end());
    return v;
}
void Broker::mark_worker_dead(WorkerBootId boot) { std::lock_guard<std::mutex> lk(mu_); impl_->live_workers.erase(boot); ++impl_->broker_generation; }

void Broker::register_owner(const OwnerDescriptor& owner) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!owner.owner_id.is_valid()) throw BrokerError(ErrorCode::InvalidArgument, "owner must have an id");
    impl_->owners[owner.owner_id] = owner;
}
const OwnerDescriptor* Broker::find_owner(OwnerId owner) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = impl_->owners.find(owner);
    return it == impl_->owners.end() ? nullptr : &it->second;
}

ResourcePoolId Broker::register_pool(const ResourcePoolDescriptor& pool, CapacityGeneration gen) {
    (void)gen;
    std::lock_guard<std::mutex> lk(mu_);
    if (!pool.pool_id.is_valid()) throw BrokerError(ErrorCode::InvalidArgument, "pool must have an id");
    if (impl_->pools.count(pool.pool_id)) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate pool");
    CapacityLedger ledger(pool.resource_class, pool.total_governed, pool.provenance);
    auto ps = Broker::Impl::PoolState{pool, std::move(ledger)};
    impl_->pools.emplace(pool.pool_id, std::move(ps));
    impl_->default_pool_by_class[pool.resource_class] = pool.pool_id;
    return pool.pool_id;
}
ResourceInstanceId Broker::register_resource(const ResourceDescriptor& res) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!res.instance_id.is_valid()) throw BrokerError(ErrorCode::InvalidArgument, "resource must have an id");
    if (impl_->resources.count(res.instance_id)) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate resource instance");
    CapacityLedger ledger(res.resource_class, res.governed, res.provenance);
    auto rs = Broker::Impl::ResourceState{res, std::move(ledger)};
    impl_->resources.emplace(res.instance_id, std::move(rs));
    return res.instance_id;
}

void Broker::update_capacity(ResourcePoolId pool, ResourceAmount new_governed, CapacityGeneration gen, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto it = impl_->pools.find(pool);
    if (it == impl_->pools.end()) throw BrokerError(ErrorCode::NotFound, "pool not found");
    impl_->check_generation<CapacityGeneration>(CapacityGeneration(1), gen, "stale capacity generation");
    it->second.ledger.set_governed(new_governed);
    (void)it;
}

void Broker::update_health(ResourceInstanceId instance, Health health, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto it = impl_->resources.find(instance);
    if (it == impl_->resources.end()) throw BrokerError(ErrorCode::NotFound, "resource not found");
    it->second.desc.health = health;
}

GrantResult Broker::submit_request(const ResourceRequest& request, const AuthorityEnvelope& auth) { std::lock_guard<std::mutex> lk(mu_); return impl_->do_submit_request(request, auth); }
Explanation Broker::explain_request(const ResourceRequest& request, const AuthorityEnvelope& auth) const { std::lock_guard<std::mutex> lk(mu_); return impl_->do_explain_request(request, auth); }
void Broker::activate_reservation(ReservationId res, const AuthorityEnvelope& auth) { std::lock_guard<std::mutex> lk(mu_); impl_->do_activate(res, auth); }
void Broker::release_reservation(ReservationId res, const AuthorityEnvelope& auth) { std::lock_guard<std::mutex> lk(mu_); impl_->do_release_reservation(res, auth); }
void Broker::expire_reservation(ReservationId res, const AuthorityEnvelope& auth) { std::lock_guard<std::mutex> lk(mu_); impl_->validate_authority(auth, true); auto& rsv = impl_->reservations.at(res); if (rsv.desc.state == ReservationState::ACTIVE || rsv.desc.state == ReservationState::RESERVED) rsv.desc.state = ReservationState::EXPIRED; }
void Broker::revoke_reservation(ReservationId res, const AuthorityEnvelope& auth) { std::lock_guard<std::mutex> lk(mu_); impl_->validate_authority(auth, true); auto& rsv = impl_->reservations.at(res); if (rsv.desc.state == ReservationState::ACTIVE || rsv.desc.state == ReservationState::RESERVED) rsv.desc.state = ReservationState::REVOKED; }

LeaseId Broker::acquire_lease(ReservationId res, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto it = impl_->reservations.find(res);
    if (it == impl_->reservations.end()) throw BrokerError(ErrorCode::NotFound, "reservation not found");
    LeaseDescriptor d;
    d.lease_id = LeaseId(impl_->next_lease++);
    d.reservation_id = res;
    d.lease_generation = LeaseGeneration(1);
    d.worker_boot = auth.worker_boot;
    d.issued_at_ms = now_ms();
    d.renews_at_ms = d.issued_at_ms + impl_->policy.default_lease_ns / 1000000LL;
    if (impl_->policy.default_lease_ns > 0) d.expires_at_ms = d.issued_at_ms + impl_->policy.default_lease_ns / 1000000LL;
    d.state = LeaseState::ACTIVE;
    d.authority = auth;
    d.provenance = Provenance::REPORTED;
    impl_->leases[d.lease_id] = Broker::Impl::LeaseRec{d};
    return d.lease_id;
}
void Broker::renew_lease(LeaseId lease, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto it = impl_->leases.find(lease);
    if (it == impl_->leases.end()) throw BrokerError(ErrorCode::NotFound, "lease not found");
    auto& ld = it->second.desc;
    if (ld.state == LeaseState::EXPIRED || ld.state == LeaseState::TERMINATED || ld.state == LeaseState::STALE) throw BrokerError(ErrorCode::InvalidState, "lease no longer renewable");
    ld.lease_generation = LeaseGeneration(ld.lease_generation.value() + 1);
    ld.renews_at_ms = now_ms() + impl_->policy.default_lease_ns / 1000000LL;
    if (impl_->policy.default_lease_ns > 0) ld.expires_at_ms = now_ms() + impl_->policy.default_lease_ns / 1000000LL;
}
void Broker::revoke_lease(LeaseId lease, const AuthorityEnvelope& auth) { std::lock_guard<std::mutex> lk(mu_); impl_->validate_authority(auth, true); auto& ld = impl_->leases.at(lease).desc; ld.state = LeaseState::REVOKED; }
void Broker::expire_lease(LeaseId lease, const AuthorityEnvelope& auth) { std::lock_guard<std::mutex> lk(mu_); impl_->validate_authority(auth, true); auto& ld = impl_->leases.at(lease).desc; if (ld.state == LeaseState::ACTIVE) ld.state = LeaseState::EXPIRED; }
LeaseDescriptor Broker::lease(LeaseId lease) const { std::lock_guard<std::mutex> lk(mu_); return impl_->leases.at(lease).desc; }

AllocationId Broker::report_allocation(ReservationId res, ResourceInstanceId instance, ResourceAmount amount, std::string evidence, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto rit = impl_->reservations.find(res);
    if (rit == impl_->reservations.end()) throw BrokerError(ErrorCode::NotFound, "reservation not found");
    auto& rsv = rit->second;
    if (rsv.desc.state != ReservationState::ACTIVE) throw BrokerError(ErrorCode::InvalidState, "allocation requires an active reservation");
    auto iit = impl_->resources.find(instance);
    if (iit == impl_->resources.end()) throw BrokerError(ErrorCode::NotFound, "resource instance not found");
    auto& inst = iit->second;
    const ResourceClass cls = inst.desc.resource_class;
    if (amount.dimension() != dimension_of(cls)) throw BrokerError(ErrorCode::InvalidArgument, "allocation dimension mismatch");
    ReservationClaim* claim = nullptr;
    for (auto& c : rsv.desc.claims) {
        if (c.resource_class == cls && c.pool_id == inst.desc.pool_id) { claim = &c; break; }
    }
    if (claim == nullptr) throw BrokerError(ErrorCode::InvalidState, "no reservation claim matches pool/class");
    if (claim->allocated_amount + amount > claim->granted_amount) throw BrokerError(ErrorCode::InvalidState, "allocation exceeds reservation grant");
    auto pit = impl_->pools.find(claim->pool_id);
    if (pit == impl_->pools.end()) throw BrokerError(ErrorCode::NotFound, "pool not found");
    if (!(inst.ledger.free() >= amount)) throw BrokerError(ErrorCode::InsufficientCapacity, "instance lacks free capacity");
    pit->second.ledger.allocate(amount);
    inst.ledger.bind_allocated(amount);
    claim->allocated_amount = claim->allocated_amount + amount;
    AllocationDescriptor a;
    a.allocation_id = AllocationId(impl_->next_allocation++);
    a.reservation_id = res;
    a.instance_id = instance;
    a.amount = amount;
    a.allocation_generation = AllocationGeneration(1);
    a.state = AllocationState::ALLOCATED;
    a.authority = auth;
    a.physical_evidence = std::move(evidence);
    a.provenance = Provenance::REPORTED;
    impl_->allocations[a.allocation_id] = Broker::Impl::AllocationRec{a};
    return a.allocation_id;
}
void Broker::release_allocation(AllocationId alloc, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto ait = impl_->allocations.find(alloc);
    if (ait == impl_->allocations.end()) throw BrokerError(ErrorCode::NotFound, "allocation not found");
    auto& a = ait->second.desc;
    if (a.state == AllocationState::RELEASED) return;
    auto iit = impl_->resources.find(a.instance_id);
    if (iit != impl_->resources.end()) iit->second.ledger.release_allocated(a.amount);
    auto rit = impl_->reservations.find(a.reservation_id);
    if (rit != impl_->reservations.end()) {
        for (auto& c : rit->second.desc.claims) {
            if (a.instance_id.is_valid()) {
                auto iinst = impl_->resources.find(a.instance_id);
                if (iinst != impl_->resources.end() && c.pool_id == iinst->second.desc.pool_id) {
                    auto p2 = impl_->pools.find(c.pool_id);
                    if (p2 != impl_->pools.end()) p2->second.ledger.release_allocation_to_reserved(a.amount);
                    c.allocated_amount = c.allocated_amount - a.amount;
                    break;
                }
            }
        }
    }
    a.state = AllocationState::RELEASED;
}


RecallDescriptor Broker::issue_recall(ReservationId res, ResourceAmount amount, std::string reason, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto rit = impl_->reservations.find(res);
    if (rit == impl_->reservations.end()) throw BrokerError(ErrorCode::NotFound, "reservation not found");
    RecallDescriptor d;
    d.recall_id = RecallId(impl_->next_recall++);
    d.reservation_id = res;
    d.amount = amount;
    d.reason = std::move(reason);
    d.recall_generation = RecallGeneration(1);
    d.state = RecallState::ISSUED;
    d.authority = auth;
    d.provenance = Provenance::DERIVED;
    impl_->recalls[d.recall_id] = Broker::Impl::RecallRec{d};
    if (rit->second.desc.state == ReservationState::ACTIVE || rit->second.desc.state == ReservationState::RESERVED) {
        rit->second.desc.state = ReservationState::RECALL_REQUESTED;
    }
    return d;
}
void Broker::acknowledge_recall(RecallId recall, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto it = impl_->recalls.find(recall);
    if (it == impl_->recalls.end()) throw BrokerError(ErrorCode::NotFound, "recall not found");
    if (it->second.desc.state == RecallState::ISSUED) it->second.desc.state = RecallState::ACKNOWLEDGED;
}

PreemptionDescriptor Broker::issue_preemption(ReservationId res, ResourceAmount amount, std::string reason, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto rit = impl_->reservations.find(res);
    if (rit == impl_->reservations.end()) throw BrokerError(ErrorCode::NotFound, "reservation not found");
    PreemptionDescriptor d;
    d.preemption_id = PreemptionId(impl_->next_preemption++);
    d.reservation_id = res;
    d.amount = amount;
    d.reason = std::move(reason);
    d.preemption_generation = PreemptionGeneration(1);
    d.state = PreemptionState::PLANNED;
    d.authority = auth;
    d.provenance = Provenance::DERIVED;
    impl_->preemptions[d.preemption_id] = Broker::Impl::PreemptionRec{d};
    if (rit->second.desc.state == ReservationState::ACTIVE || rit->second.desc.state == ReservationState::RESERVED) {
        rit->second.desc.state = ReservationState::PREEMPTING;
    }
    return d;
}
void Broker::acknowledge_preemption(PreemptionId preemption, const AuthorityEnvelope& auth) {
    std::lock_guard<std::mutex> lk(mu_);
    impl_->validate_authority(auth, true);
    auto it = impl_->preemptions.find(preemption);
    if (it == impl_->preemptions.end()) throw BrokerError(ErrorCode::NotFound, "preemption not found");
    if (it->second.desc.state == PreemptionState::PLANNED || it->second.desc.state == PreemptionState::ISSUED) it->second.desc.state = PreemptionState::ACKNOWLEDGED;
}

std::vector<ReclamationCandidate> Broker::reclaimable_candidates(ResourcePoolId pool) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ReclamationCandidate> out;
    const auto pit = impl_->pools.find(pool);
    if (pit == impl_->pools.end()) return out;
    for (const auto& kv : impl_->reservations) {
        const auto& rsv = kv.second.desc;
        if (rsv.state == ReservationState::RELEASED || rsv.state == ReservationState::EXPIRED ||
            rsv.state == ReservationState::REVOKED || rsv.state == ReservationState::STALE) continue;
        for (const auto& claim : rsv.claims) {
            if (claim.pool_id != pool) continue;
            if (claim.granted_amount.is_zero() && claim.allocated_amount.is_zero()) continue;
            ReclamationCandidate c;
            c.reservation_id = rsv.reservation_id;
            c.pool_id = pool;
            c.resource_class = claim.resource_class;
            c.reclaimable_amount = claim.granted_amount.is_zero() ? claim.allocated_amount : claim.granted_amount;
            c.priority = rsv.priority;
            c.reclaimability = rsv.reclaimability;
            c.is_protected = (rsv.reclaimability == Reclaimability::NON_RECLAIMABLE);
            c.waiting_age_ns = 0;
            c.consequence = "reclaims " + claim.granted_amount.to_string() + " for pool " + std::to_string(pool.value());
            out.push_back(c);
        }
    }
    return out;
}

ReclamationPlan Broker::plan_reclamation(ResourcePoolId pool, ResourceAmount deficit) const {
    std::lock_guard<std::mutex> lk(mu_);
    ReclamationPlan plan;
    const auto pit = impl_->pools.find(pool);
    if (pit == impl_->pools.end()) throw BrokerError(ErrorCode::NotFound, "pool not found");
    plan.pool_id = pool;
    plan.resource_class = pit->second.desc.resource_class;
    plan.deficit = deficit;
    plan.candidates = reclaimable_candidates(pool);
    std::sort(plan.candidates.begin(), plan.candidates.end(), [](const ReclamationCandidate& a, const ReclamationCandidate& b) {
        if (a.priority != b.priority) return priority_rank(a.priority) < priority_rank(b.priority);
        if (a.reclaimability != b.reclaimability) return reclaimability_rank(a.reclaimability) > reclaimability_rank(b.reclaimability);
        return a.reservation_id < b.reservation_id;
    });
    const ResourceClass cls = pit->second.desc.resource_class;
    const ResourceAmount zero = ResourceAmount::of_class(cls, 0);
    ResourceAmount need = deficit;
    for (const auto& c : plan.candidates) {
        if (!(need > plan.covered)) break;
        if (c.is_protected) continue;
        plan.chosen.push_back(c.reservation_id);
        plan.covered = plan.covered + c.reclaimable_amount;
    }
    plan.unsatisfied = need > plan.covered ? (need - plan.covered) : zero;
    for (const auto& c : plan.candidates) { if (c.is_protected && c.reclaimable_amount > zero) { plan.requires_preemption = true; } }
    plan.rationale = "Selected lowest-priority, most-reclaimable reservations first; protected reservations preserved.";
    return plan;
}

ResourcePoolDescriptor Broker::pool(ResourcePoolId pool) const { std::lock_guard<std::mutex> lk(mu_); return impl_->build_pool_descriptor(pool); }
ResourceDescriptor Broker::resource(ResourceInstanceId instance) const { std::lock_guard<std::mutex> lk(mu_); return impl_->build_resource_descriptor(instance); }
ReservationDescriptor Broker::reservation(ReservationId res) const { std::lock_guard<std::mutex> lk(mu_); return impl_->reservations.at(res).desc; }
AllocationDescriptor Broker::allocation(AllocationId alloc) const { std::lock_guard<std::mutex> lk(mu_); return impl_->allocations.at(alloc).desc; }
std::vector<ResourcePoolDescriptor> Broker::all_pools() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ResourcePoolDescriptor> v;
    for (const auto& kv : impl_->pools) v.push_back(impl_->build_pool_descriptor(kv.first));
    std::sort(v.begin(), v.end(), [](const ResourcePoolDescriptor& a, const ResourcePoolDescriptor& b) { return a.pool_id < b.pool_id; });
    return v;
}
std::vector<ResourceDescriptor> Broker::all_resources() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ResourceDescriptor> v;
    for (const auto& kv : impl_->resources) v.push_back(impl_->build_resource_descriptor(kv.first));
    std::sort(v.begin(), v.end(), [](const ResourceDescriptor& a, const ResourceDescriptor& b) { return a.instance_id < b.instance_id; });
    return v;
}
std::vector<ReservationDescriptor> Broker::all_reservations() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ReservationDescriptor> v;
    for (const auto& kv : impl_->reservations) v.push_back(kv.second.desc);
    std::sort(v.begin(), v.end(), [](const ReservationDescriptor& a, const ReservationDescriptor& b) { return a.reservation_id < b.reservation_id; });
    return v;
}
std::vector<AllocationDescriptor> Broker::all_allocations() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<AllocationDescriptor> v;
    for (const auto& kv : impl_->allocations) v.push_back(kv.second.desc);
    std::sort(v.begin(), v.end(), [](const AllocationDescriptor& a, const AllocationDescriptor& b) { return a.allocation_id < b.allocation_id; });
    return v;
}
std::vector<RecallDescriptor> Broker::all_recalls() const { std::lock_guard<std::mutex> lk(mu_); std::vector<RecallDescriptor> v; for (const auto& kv : impl_->recalls) v.push_back(kv.second.desc); std::sort(v.begin(), v.end(), [](const RecallDescriptor& a, const RecallDescriptor& b) { return a.recall_id < b.recall_id; }); return v; }
std::vector<PreemptionDescriptor> Broker::all_preemptions() const { std::lock_guard<std::mutex> lk(mu_); std::vector<PreemptionDescriptor> v; for (const auto& kv : impl_->preemptions) v.push_back(kv.second.desc); std::sort(v.begin(), v.end(), [](const PreemptionDescriptor& a, const PreemptionDescriptor& b) { return a.preemption_id < b.preemption_id; }); return v; }
CapacityView Broker::capacity_view(ResourcePoolId pool) const {
    std::lock_guard<std::mutex> lk(mu_);
    const auto& ps = impl_->pool_by_id(pool);
    CapacityView v;
    v.nominal = ps.ledger.nominal();
    v.reported = ps.ledger.reported();
    v.measured = ps.ledger.measured();
    v.governed = ps.ledger.governed();
    v.reserved = ps.ledger.reserved();
    v.allocated = ps.ledger.allocated();
    v.reclaimable = ps.ledger.reclaimable();
    v.unavailable = ps.ledger.unavailable();
    v.free = ps.ledger.free();
    return v;
}
std::optional<ResourcePoolId> Broker::pool_for_class(ResourceClass cls) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = impl_->default_pool_by_class.find(cls);
    return it == impl_->default_pool_by_class.end() ? std::nullopt : std::optional<ResourcePoolId>(it->second);
}

void Broker::save(const std::string& path) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::uint8_t> buf;
    impl_->serialize(buf);
    persistence::write_file_atomic(path, buf);
}
void Broker::load(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    const std::vector<std::uint8_t> buf = persistence::read_file(path);
    impl_->deserialize(buf);
}

// ---- persistence serialization helpers ----
static void write_amount(ByteWriter& w, const ResourceAmount& a) {
    switch (a.dimension()) {
        case Dimension::Bytes: w.u8(0); w.u64(a.as_bytes().value()); break;
        case Dimension::BytesPerSecond: w.u8(1); w.u64(a.as_bytes_per_second().value()); break;
        case Dimension::Count: w.u8(2); w.u64(a.as_count().value()); break;
        case Dimension::Share: w.u8(3); w.u64(a.as_share().value()); break;
        case Dimension::Other: { w.u8(4); std::uint64_t bits; std::memcpy(&bits, &(const double&)a.as_other(), 8); w.u64(bits); break; }
    }
}
static ResourceAmount read_amount(ByteReader& r, ResourceClass cls) {
    const std::uint8_t k = r.u8();
    const std::uint64_t v = r.u64();
    if (dimension_of(cls) == Dimension::Bytes) { if (k != 0) throw BrokerError(ErrorCode::MalformedPayload, "amount kind mismatch"); return ResourceAmount::bytes(Bytes{v}); }
    if (dimension_of(cls) == Dimension::BytesPerSecond) { if (k != 1) throw BrokerError(ErrorCode::MalformedPayload, "amount kind mismatch"); return ResourceAmount::bytes_per_second(BytesPerSecond{v}); }
    if (dimension_of(cls) == Dimension::Count) { if (k != 2) throw BrokerError(ErrorCode::MalformedPayload, "amount kind mismatch"); return ResourceAmount::count(Count{v}); }
    if (dimension_of(cls) == Dimension::Share) { if (k != 3) throw BrokerError(ErrorCode::MalformedPayload, "amount kind mismatch"); return ResourceAmount::share(ComputeShare{v}); }
    if (dimension_of(cls) == Dimension::Other) { if (k != 4) throw BrokerError(ErrorCode::MalformedPayload, "amount kind mismatch"); double d; std::memcpy(&d, &v, 8); return ResourceAmount::other(d); }
    throw BrokerError(ErrorCode::MalformedPayload, "unknown amount dimension");
}
static void write_authority(ByteWriter& w, const AuthorityEnvelope& a) { w.gen(a.coordinator_epoch); w.id(a.worker); w.id(a.worker_boot); w.gen(a.broker_generation); }
static AuthorityEnvelope read_authority(ByteReader& r) {
    AuthorityEnvelope a; a.coordinator_epoch = r.gen<CoordinatorEpochTag>(); a.worker = r.id<WorkerIdTag>(); a.worker_boot = r.id<WorkerBootIdTag>(); a.broker_generation = r.gen<BrokerGenerationTag>(); return a;
}
static void validate_res_class(ResourceClass c) {
    const auto v = static_cast<std::uint32_t>(c);
    if (v > static_cast<std::uint32_t>(ResourceClass::GENERIC_SCALAR_RESOURCE)) {
        throw BrokerError(ErrorCode::MalformedPayload, "invalid resource class");
    }
}
static void write_u64(ByteWriter& w, std::uint64_t v) { w.u64(v); }
static void write_double(ByteWriter& w, double d) { std::uint64_t bits; std::memcpy(&bits, &d, 8); w.u64(bits); }
static double read_double(ByteReader& r) { std::uint64_t bits = r.u64(); double d; std::memcpy(&d, &bits, 8); return d; }

void Broker::Impl::serialize(std::vector<std::uint8_t>& out) const {
    ByteWriter w(out);
    w.u64(broker_generation);
    w.gen(epoch);
    // counters
    write_u64(w, next_pool); write_u64(w, next_resource); write_u64(w, next_request);
    write_u64(w, next_reservation); write_u64(w, next_allocation); write_u64(w, next_lease);
    write_u64(w, next_recall); write_u64(w, next_preemption); write_u64(w, next_owner);
    write_u64(w, next_worker); write_u64(w, next_worker_boot); write_u64(w, next_class);
    write_u64(w, next_policy); write_u64(w, next_node); write_u64(w, next_device);
    write_u64(w, next_backend); write_u64(w, next_attempt); write_u64(w, next_dispatch);
    write_u64(w, next_observation); write_u64(w, next_reclamation);
    // policy
    w.id(policy.policy_id); w.gen(policy.policy_generation);
    w.bool_(policy.priority_overrides_hard_guarantee); w.bool_(policy.honor_non_preemptible_protection);
    write_double(w, policy.max_soft_overcommit);
    w.u64(static_cast<std::uint64_t>(policy.default_lease_ns)); w.u64(static_cast<std::uint64_t>(policy.lease_renewal_grace_ns));
    w.bool_(policy.leases_required_for_live); w.bool_(policy.preemption_enabled); w.bool_(policy.preemption_requires_policy_authority);
    w.u64(static_cast<std::uint64_t>(policy.starvation_threshold_ns)); w.u32(static_cast<std::uint32_t>(policy.starvation_boost_priority_ranks));
    w.u64(policy.max_reservations); w.u64(policy.max_active_reservations); w.u64(policy.max_allocations);
    w.bool_(policy.preserve_logical_reservations_on_recovery); w.bool_(policy.require_revalidation_on_recovery); w.bool_(policy.auto_reclaim_dead_worker_reservations);
    w.bool_(policy.allow_partial_grant); w.bool_(policy.allow_unknown_capacity_grant);

    // pools
    write_u64(w, pools.size());
    for (const auto& kv : pools) {
        const auto& p = kv.second;
        w.id(p.desc.pool_id); w.gen(p.desc.pool_generation);
        w.u32(static_cast<std::uint32_t>(p.desc.resource_class)); w.u32(static_cast<std::uint32_t>(p.desc.scope));
        write_u64(w, p.desc.members.size()); for (const auto& m : p.desc.members) w.id(m);
        w.u32(static_cast<std::uint32_t>(p.desc.health)); w.u32(static_cast<std::uint32_t>(p.desc.freshness)); w.u32(static_cast<std::uint32_t>(p.desc.provenance));
        w.id(p.desc.policy);
        write_amount(w, p.ledger.nominal()); write_amount(w, p.ledger.reported()); write_amount(w, p.ledger.measured());
        write_amount(w, p.ledger.governed()); write_amount(w, p.ledger.reserved()); write_amount(w, p.ledger.allocated());
        write_amount(w, p.ledger.reclaimable()); write_amount(w, p.ledger.unavailable());
    }

    // resources
    write_u64(w, resources.size());
    for (const auto& kv : resources) {
        const auto& r = kv.second;
        w.id(r.desc.instance_id); w.id(r.desc.class_id); w.u32(static_cast<std::uint32_t>(r.desc.resource_class));
        w.id(r.desc.node); w.id(r.desc.device); w.id(r.desc.backend); w.id(r.desc.pool_id);
        w.gen(r.desc.resource_generation); w.gen(r.desc.capacity_generation);
        w.u32(static_cast<std::uint32_t>(r.desc.health)); w.u32(static_cast<std::uint32_t>(r.desc.freshness)); w.u32(static_cast<std::uint32_t>(r.desc.provenance));
        w.gen(r.desc.policy_generation);
        write_amount(w, r.ledger.nominal()); write_amount(w, r.ledger.reported()); write_amount(w, r.ledger.measured());
        write_amount(w, r.ledger.governed()); write_amount(w, r.ledger.reserved()); write_amount(w, r.ledger.allocated());
        write_amount(w, r.ledger.reclaimable()); write_amount(w, r.ledger.unavailable());
    }

    // reservations
    write_u64(w, reservations.size());
    for (const auto& kv : reservations) { const ReservationDescriptor& d = kv.second.desc;
        w.id(d.reservation_id); w.id(d.request_id); w.id(d.owner); w.id(d.tenant); w.id(d.workload); w.gen(d.reservation_generation);
        w.u64(static_cast<std::uint64_t>(d.issued_at_ms)); w.u64(static_cast<std::uint64_t>(d.expires_at_ms));
        w.u32(static_cast<std::uint32_t>(d.priority)); w.u32(static_cast<std::uint32_t>(d.reclaimability)); w.bool_(d.preemption_eligible);
        w.u32(static_cast<std::uint32_t>(d.state)); w.u32(static_cast<std::uint32_t>(d.provenance));
        write_authority(w, d.authority);
        write_u64(w, d.claims.size());
        for (const auto& c : d.claims) { w.id(c.class_id); w.u32(static_cast<std::uint32_t>(c.resource_class)); w.id(c.pool_id); write_amount(w, c.amount); write_amount(w, c.granted_amount); write_amount(w, c.allocated_amount); }
    }

    // allocations
    write_u64(w, allocations.size());
    for (const auto& kv : allocations) { const AllocationDescriptor& d = kv.second.desc;
        w.id(d.allocation_id); w.id(d.reservation_id); w.id(d.instance_id); write_amount(w, d.amount); w.gen(d.allocation_generation);
        w.u32(static_cast<std::uint32_t>(d.state)); w.u32(static_cast<std::uint32_t>(d.freshness)); w.u32(static_cast<std::uint32_t>(d.provenance));
        write_authority(w, d.authority); w.string(d.physical_evidence);
    }

    // leases
    write_u64(w, leases.size());
    for (const auto& kv : leases) { const LeaseDescriptor& d = kv.second.desc;
        w.id(d.lease_id); w.id(d.reservation_id); w.gen(d.lease_generation); w.id(d.worker_boot);
        w.u64(static_cast<std::uint64_t>(d.issued_at_ms)); w.u64(static_cast<std::uint64_t>(d.renews_at_ms)); w.u64(static_cast<std::uint64_t>(d.expires_at_ms));
        w.u32(static_cast<std::uint32_t>(d.state)); w.u32(static_cast<std::uint32_t>(d.freshness)); w.u32(static_cast<std::uint32_t>(d.provenance)); write_authority(w, d.authority);
    }

    // recalls
    write_u64(w, recalls.size());
    for (const auto& kv : recalls) { const RecallDescriptor& d = kv.second.desc;
        w.id(d.recall_id); w.id(d.reservation_id); w.id(d.allocation_id); w.u32(static_cast<std::uint32_t>(d.resource_class)); write_amount(w, d.amount); w.string(d.reason); w.gen(d.recall_generation);
        w.bool_(d.urgent); w.u32(static_cast<std::uint32_t>(d.state)); w.u32(static_cast<std::uint32_t>(d.provenance)); write_authority(w, d.authority);
    }

    // preemptions
    write_u64(w, preemptions.size());
    for (const auto& kv : preemptions) { const PreemptionDescriptor& d = kv.second.desc;
        w.id(d.preemption_id); w.id(d.reservation_id); w.u32(static_cast<std::uint32_t>(d.resource_class)); write_amount(w, d.amount); w.string(d.reason); w.gen(d.preemption_generation);
        w.u32(static_cast<std::uint32_t>(d.state)); w.u32(static_cast<std::uint32_t>(d.provenance)); write_authority(w, d.authority);
    }

    // owners
    write_u64(w, owners.size());
    for (const auto& kv : owners) { const OwnerDescriptor& d = kv.second;
        w.id(d.owner_id); w.id(d.tenant); w.u32(static_cast<std::uint32_t>(d.priority_class)); w.id(d.policy);
        w.u32(static_cast<std::uint32_t>(d.default_reclaimability)); w.bool_(d.preemption_eligible); w.gen(d.owner_generation); w.u32(static_cast<std::uint32_t>(d.provenance));
    }

    // live workers -> incarnation registry is ephemeral; at recovery it is cleared.
    // We serialize an entry count for determinism, but the recovery pass clears it.
    write_u64(w, 0);
    // end sentinel
    w.u64(0);
}


void Broker::Impl::deserialize(const std::vector<std::uint8_t>& in) {
    ByteReader r(std::span<const std::uint8_t>(in.data(), in.size()));
    constexpr std::uint64_t kMaxCount = 5000000;
    broker_generation = r.u64();
    (void)r.gen<CoordinatorEpochTag>();  // persisted epoch; a fresh coordinator supplies its own

    // counters
    next_pool = r.u64(); next_resource = r.u64(); next_request = r.u64();
    next_reservation = r.u64(); next_allocation = r.u64(); next_lease = r.u64();
    next_recall = r.u64(); next_preemption = r.u64(); next_owner = r.u64();
    next_worker = r.u64(); next_worker_boot = r.u64(); next_class = r.u64();
    next_policy = r.u64(); next_node = r.u64(); next_device = r.u64();
    next_backend = r.u64(); next_attempt = r.u64(); next_dispatch = r.u64();
    next_observation = r.u64(); next_reclamation = r.u64();

    // policy
    policy.policy_id = r.id<PolicyIdTag>(); policy.policy_generation = r.gen<PolicyGenerationTag>();
    policy.priority_overrides_hard_guarantee = r.bool_(); policy.honor_non_preemptible_protection = r.bool_();
    policy.max_soft_overcommit = read_double(r);
    policy.default_lease_ns = static_cast<std::int64_t>(r.u64()); policy.lease_renewal_grace_ns = static_cast<std::int64_t>(r.u64());
    policy.leases_required_for_live = r.bool_(); policy.preemption_enabled = r.bool_(); policy.preemption_requires_policy_authority = r.bool_();
    policy.starvation_threshold_ns = static_cast<std::int64_t>(r.u64()); policy.starvation_boost_priority_ranks = static_cast<int>(r.u32());
    policy.max_reservations = r.u64(); policy.max_active_reservations = r.u64(); policy.max_allocations = r.u64();
    policy.preserve_logical_reservations_on_recovery = r.bool_(); policy.require_revalidation_on_recovery = r.bool_(); policy.auto_reclaim_dead_worker_reservations = r.bool_();
    policy.allow_partial_grant = r.bool_(); policy.allow_unknown_capacity_grant = r.bool_();

    pools.clear(); resources.clear(); reservations.clear(); allocations.clear();
    leases.clear(); recalls.clear(); preemptions.clear(); owners.clear(); default_pool_by_class.clear();

    // pools
    const auto pool_count = r.u64();
    if (pool_count > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible pool count");
    for (std::uint64_t i = 0; i < pool_count; ++i) {
        ResourcePoolDescriptor pd;
        pd.pool_id = r.id<ResourcePoolIdTag>(); pd.pool_generation = r.gen<ResourcePoolGenerationTag>();
        pd.resource_class = static_cast<ResourceClass>(r.u32()); validate_res_class(pd.resource_class);
        pd.scope = static_cast<PoolScope>(r.u32());
        const auto members = r.u64(); if (members > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible member count");
        for (std::uint64_t m = 0; m < members; ++m) pd.members.push_back(r.id<ResourceInstanceIdTag>());
        pd.health = static_cast<Health>(r.u32()); pd.freshness = static_cast<Freshness>(r.u32()); pd.provenance = static_cast<Provenance>(r.u32());
        pd.policy = r.id<PolicyIdTag>();
        const ResourceAmount nominal = read_amount(r, pd.resource_class);
        const ResourceAmount reported = read_amount(r, pd.resource_class);
        const ResourceAmount measured = read_amount(r, pd.resource_class);
        const ResourceAmount governed = read_amount(r, pd.resource_class);
        const ResourceAmount reserved = read_amount(r, pd.resource_class);
        const ResourceAmount allocated = read_amount(r, pd.resource_class);
        const ResourceAmount reclaimable = read_amount(r, pd.resource_class);
        const ResourceAmount unavailable = read_amount(r, pd.resource_class);
        CapacityLedger ledger(pd.resource_class, governed, pd.provenance);
        ledger.restore_state(nominal, reported, measured, governed, reserved, allocated, reclaimable, unavailable);
        if (!pools.emplace(pd.pool_id, PoolState{pd, std::move(ledger)}).second) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate pool id");
        default_pool_by_class[pd.resource_class] = pd.pool_id;
    }

    // resources
    const auto res_count = r.u64(); if (res_count > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible resource count");
    for (std::uint64_t i = 0; i < res_count; ++i) {
        ResourceDescriptor rd;
        rd.instance_id = r.id<ResourceInstanceIdTag>(); rd.class_id = r.id<ResourceClassIdTag>();
        rd.resource_class = static_cast<ResourceClass>(r.u32()); validate_res_class(rd.resource_class);
        rd.node = r.id<NodeIdTag>(); rd.device = r.id<DeviceIdTag>(); rd.backend = r.id<BackendIdTag>(); rd.pool_id = r.id<ResourcePoolIdTag>();
        rd.resource_generation = r.gen<ResourceGenerationTag>(); rd.capacity_generation = r.gen<CapacityGenerationTag>();
        rd.health = static_cast<Health>(r.u32()); rd.freshness = static_cast<Freshness>(r.u32()); rd.provenance = static_cast<Provenance>(r.u32());
        rd.policy_generation = r.gen<PolicyGenerationTag>();
        const ResourceAmount nominal = read_amount(r, rd.resource_class);
        const ResourceAmount reported = read_amount(r, rd.resource_class);
        const ResourceAmount measured = read_amount(r, rd.resource_class);
        const ResourceAmount governed = read_amount(r, rd.resource_class);
        const ResourceAmount reserved = read_amount(r, rd.resource_class);
        const ResourceAmount allocated = read_amount(r, rd.resource_class);
        const ResourceAmount reclaimable = read_amount(r, rd.resource_class);
        const ResourceAmount unavailable = read_amount(r, rd.resource_class);
        CapacityLedger ledger(rd.resource_class, governed, rd.provenance);
        ledger.restore_state(nominal, reported, measured, governed, reserved, allocated, reclaimable, unavailable);
        if (!resources.emplace(rd.instance_id, ResourceState{rd, std::move(ledger)}).second) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate resource id");
    }

    // reservations
    const auto rsv_count = r.u64(); if (rsv_count > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible reservation count");
    for (std::uint64_t i = 0; i < rsv_count; ++i) {
        ReservationDescriptor d;
        d.reservation_id = r.id<ReservationIdTag>(); d.request_id = r.id<ResourceRequestIdTag>();
        d.owner = r.id<OwnerIdTag>(); d.tenant = r.id<TenantIdTag>(); d.workload = r.id<WorkloadIdTag>();
        d.reservation_generation = r.gen<ReservationGenerationTag>();
        d.issued_at_ms = static_cast<std::int64_t>(r.u64()); d.expires_at_ms = static_cast<std::int64_t>(r.u64());
        d.priority = static_cast<Priority>(r.u32()); d.reclaimability = static_cast<Reclaimability>(r.u32()); d.preemption_eligible = r.bool_();
        d.state = static_cast<ReservationState>(r.u32()); d.provenance = static_cast<Provenance>(r.u32());
        d.authority = read_authority(r);
        const auto claims = r.u64(); if (claims > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible claim count");
        for (std::uint64_t c = 0; c < claims; ++c) {
            ReservationClaim rc; rc.class_id = r.id<ResourceClassIdTag>();
            rc.resource_class = static_cast<ResourceClass>(r.u32()); validate_res_class(rc.resource_class);
            rc.pool_id = r.id<ResourcePoolIdTag>(); rc.amount = read_amount(r, rc.resource_class);
            rc.granted_amount = read_amount(r, rc.resource_class); rc.allocated_amount = read_amount(r, rc.resource_class);
            d.claims.push_back(rc);
        }
        if (!reservations.emplace(d.reservation_id, ReservationRec{d}).second) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate reservation id");
    }

    // allocations
    const auto alloc_count = r.u64(); if (alloc_count > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible allocation count");
    for (std::uint64_t i = 0; i < alloc_count; ++i) {
        AllocationDescriptor d;
        d.allocation_id = r.id<AllocationIdTag>(); d.reservation_id = r.id<ReservationIdTag>(); d.instance_id = r.id<ResourceInstanceIdTag>();
        d.amount = read_amount(r, resources.at(d.instance_id).desc.resource_class);
        d.allocation_generation = r.gen<AllocationGenerationTag>();
        d.state = static_cast<AllocationState>(r.u32()); d.freshness = static_cast<Freshness>(r.u32()); d.provenance = static_cast<Provenance>(r.u32());
        d.authority = read_authority(r); d.physical_evidence = r.string();
        if (!allocations.emplace(d.allocation_id, AllocationRec{d}).second) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate allocation id");
    }

    // leases
    const auto lease_count = r.u64(); if (lease_count > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible lease count");
    for (std::uint64_t i = 0; i < lease_count; ++i) {
        LeaseDescriptor d;
        d.lease_id = r.id<LeaseIdTag>(); d.reservation_id = r.id<ReservationIdTag>(); d.lease_generation = r.gen<LeaseGenerationTag>(); d.worker_boot = r.id<WorkerBootIdTag>();
        d.issued_at_ms = static_cast<std::int64_t>(r.u64()); d.renews_at_ms = static_cast<std::int64_t>(r.u64()); d.expires_at_ms = static_cast<std::int64_t>(r.u64());
        d.state = static_cast<LeaseState>(r.u32()); d.freshness = static_cast<Freshness>(r.u32()); d.provenance = static_cast<Provenance>(r.u32()); d.authority = read_authority(r);
        if (!leases.emplace(d.lease_id, LeaseRec{d}).second) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate lease id");
    }

    // recalls
    const auto recall_count = r.u64(); if (recall_count > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible recall count");
    for (std::uint64_t i = 0; i < recall_count; ++i) {
        RecallDescriptor d;
        d.recall_id = r.id<RecallIdTag>(); d.reservation_id = r.id<ReservationIdTag>(); d.allocation_id = r.id<AllocationIdTag>();
        d.resource_class = static_cast<ResourceClass>(r.u32()); validate_res_class(d.resource_class);
        d.amount = read_amount(r, d.resource_class);
        d.reason = r.string(); d.recall_generation = r.gen<RecallGenerationTag>(); d.urgent = r.bool_();
        d.state = static_cast<RecallState>(r.u32()); d.provenance = static_cast<Provenance>(r.u32()); d.authority = read_authority(r);
        if (!recalls.emplace(d.recall_id, RecallRec{d}).second) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate recall id");
    }

    // preemptions
    const auto pre_count = r.u64(); if (pre_count > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible preemption count");
    for (std::uint64_t i = 0; i < pre_count; ++i) {
        PreemptionDescriptor d;
        d.preemption_id = r.id<PreemptionIdTag>(); d.reservation_id = r.id<ReservationIdTag>(); d.resource_class = static_cast<ResourceClass>(r.u32()); validate_res_class(d.resource_class);
        d.amount = read_amount(r, d.resource_class);
        d.reason = r.string(); d.preemption_generation = r.gen<PreemptionGenerationTag>();
        d.state = static_cast<PreemptionState>(r.u32()); d.provenance = static_cast<Provenance>(r.u32()); d.authority = read_authority(r);
        if (!preemptions.emplace(d.preemption_id, PreemptionRec{d}).second) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate preemption id");
    }

    // owners
    const auto owner_count = r.u64(); if (owner_count > kMaxCount) throw BrokerError(ErrorCode::MalformedPayload, "impossible owner count");
    for (std::uint64_t i = 0; i < owner_count; ++i) {
        OwnerDescriptor d;
        d.owner_id = r.id<OwnerIdTag>(); d.tenant = r.id<TenantIdTag>(); d.priority_class = static_cast<Priority>(r.u32()); d.policy = r.id<PolicyIdTag>();
        d.default_reclaimability = static_cast<Reclaimability>(r.u32()); d.preemption_eligible = r.bool_(); d.owner_generation = r.gen<OwnerGenerationTag>(); d.provenance = static_cast<Provenance>(r.u32());
        if (!owners.emplace(d.owner_id, d).second) throw BrokerError(ErrorCode::DuplicateIdentity, "duplicate owner id");
    }

    // live-worker registry is ephemeral; recover clears it.
    live_workers.clear();
    const auto sentinel = r.u64(); (void)sentinel;

    // recovery pass: a fresh coordinator never trusts prior incarnation liveness.
    for (auto& kv : leases) { if (kv.second.desc.state == LeaseState::ACTIVE) kv.second.desc.state = LeaseState::REVALIDATION_REQUIRED; kv.second.desc.freshness = Freshness::REVALIDATION_REQUIRED; }
    for (auto& kv : allocations) { if (kv.second.desc.state == AllocationState::ALLOCATED || kv.second.desc.state == AllocationState::IN_USE) { kv.second.desc.freshness = Freshness::REVALIDATION_REQUIRED; } }
}  // Broker::Impl::deserialize


}  // namespace resourcebroker