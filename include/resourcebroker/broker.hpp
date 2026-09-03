// broker.hpp - Resource Broker coordinator state machine.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Broker owns cross-resource reservation and arbitration. It does NOT
// implement admission (Admission Fabric), quota (Quota Fabric), link
// bandwidth shaping (Bandwidth Governor), storage, runtime registry, or
// placement/scheduling. Those boundaries are preserved.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "authority.hpp"
#include "identities.hpp"
#include "amount.hpp"
#include "capacity.hpp"
#include "enums.hpp"
#include "request.hpp"
#include "reservation.hpp"
#include "policy.hpp"
#include "explanation.hpp"
#include "reclamation.hpp"
#include "resource.hpp"

namespace resourcebroker {

// Result of submitting a request for reservation.
struct GrantResult {
    RequestOutcome outcome = RequestOutcome::UNKNOWN;
    ReservationId reservation_id;
    Explanation explanation;
};

// A single provisional hold during a multi-resource transaction; used for
// deterministic rollback when a later dimension fails.
struct ProvisionalHold {
    ResourcePoolId pool_id;
    ResourceAmount amount;
};

class Broker {
public:
    explicit Broker(CoordinatorEpoch epoch, BrokerId id = BrokerId(1));
    ~Broker();
    Broker(const Broker&) = delete;
    Broker& operator=(const Broker&) = delete;

    BrokerId id() const;
    CoordinatorEpoch coordinator_epoch() const;
    BrokerGeneration broker_generation() const;

    // --- policy ---
    void set_policy(const BrokerPolicy& policy);
    BrokerPolicy policy() const;

    // --- live workers (incarnation registry) ---
    WorkerBootId register_worker(WorkerId worker);
    bool is_worker_live(WorkerBootId boot) const;
    std::vector<WorkerBootId> live_workers() const;
    void mark_worker_dead(WorkerBootId boot);

    // --- owners (references tenant/entitlement, no quota model) ---
    void register_owner(const OwnerDescriptor& owner);
    const OwnerDescriptor* find_owner(OwnerId owner) const;

    // --- pools / resources ---
    ResourcePoolId register_pool(const ResourcePoolDescriptor& pool, CapacityGeneration gen);
    ResourceInstanceId register_resource(const ResourceDescriptor& res);
    void update_capacity(ResourcePoolId pool, ResourceAmount new_governed, CapacityGeneration gen, const AuthorityEnvelope& auth);
    void update_health(ResourceInstanceId instance, Health health, const AuthorityEnvelope& auth);

    // --- central request -> reservation ---
    GrantResult submit_request(const ResourceRequest& request, const AuthorityEnvelope& auth);

    // --- reservation lifecycle ---
    void activate_reservation(ReservationId res, const AuthorityEnvelope& auth);
    void release_reservation(ReservationId res, const AuthorityEnvelope& auth);
    void expire_reservation(ReservationId res, const AuthorityEnvelope& auth);
    void revoke_reservation(ReservationId res, const AuthorityEnvelope& auth);

    // --- leases ---
    LeaseId acquire_lease(ReservationId res, const AuthorityEnvelope& auth);
    void renew_lease(LeaseId lease, const AuthorityEnvelope& auth);
    void revoke_lease(LeaseId lease, const AuthorityEnvelope& auth);
    void expire_lease(LeaseId lease, const AuthorityEnvelope& auth);
    LeaseDescriptor lease(LeaseId lease) const;

    // --- allocation (physical/logical) ---
    AllocationId report_allocation(ReservationId res, ResourceInstanceId instance, ResourceAmount amount, std::string evidence, const AuthorityEnvelope& auth);
    void release_allocation(AllocationId alloc, const AuthorityEnvelope& auth);

    // --- recall / preemption ---
    RecallDescriptor issue_recall(ReservationId res, ResourceAmount amount, std::string reason, const AuthorityEnvelope& auth);
    void acknowledge_recall(RecallId recall, const AuthorityEnvelope& auth);
    PreemptionDescriptor issue_preemption(ReservationId res, ResourceAmount amount, std::string reason, const AuthorityEnvelope& auth);
    void acknowledge_preemption(PreemptionId preemption, const AuthorityEnvelope& auth);
    ReclamationPlan plan_reclamation(ResourcePoolId pool, ResourceAmount deficit) const;

    // --- queries ---
    ResourcePoolDescriptor pool(ResourcePoolId pool) const;
    ResourceDescriptor resource(ResourceInstanceId instance) const;
    ReservationDescriptor reservation(ReservationId res) const;
    AllocationDescriptor allocation(AllocationId alloc) const;
    std::vector<ResourcePoolDescriptor> all_pools() const;
    std::vector<ResourceDescriptor> all_resources() const;
    std::vector<ReservationDescriptor> all_reservations() const;
    std::vector<AllocationDescriptor> all_allocations() const;
    std::vector<RecallDescriptor> all_recalls() const;
    std::vector<PreemptionDescriptor> all_preemptions() const;
    std::vector<ReclamationCandidate> reclaimable_candidates(ResourcePoolId pool) const;
    CapacityView capacity_view(ResourcePoolId pool) const;
    std::optional<ResourcePoolId> pool_for_class(ResourceClass cls) const;

    // --- explanation (deterministic, named factors) ---
    Explanation explain_request(const ResourceRequest& request, const AuthorityEnvelope& auth) const;

    // --- persistence ---
    void save(const std::string& path) const;
    void load(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::mutex mu_;
};

}  // namespace resourcebroker