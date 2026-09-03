// identities.hpp - Strong non-interchangeable identities and generations.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#pragma once

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>

namespace resourcebroker {

template <typename Tag>
class StrongId {
public:
    using value_type = std::uint64_t;
    constexpr StrongId() noexcept : value_(0) {}
    explicit constexpr StrongId(value_type v) noexcept : value_(v) {}
    constexpr value_type value() const noexcept { return value_; }
    constexpr bool is_valid() const noexcept { return value_ != 0; }
    constexpr StrongId& operator++() noexcept { ++value_; return *this; }
    constexpr StrongId operator++(int) noexcept { StrongId c(*this); ++value_; return c; }
    friend constexpr bool operator==(StrongId a, StrongId b) noexcept { return a.value_ == b.value_; }
    friend constexpr bool operator!=(StrongId a, StrongId b) noexcept { return a.value_ != b.value_; }
    friend constexpr bool operator<(StrongId a, StrongId b) noexcept { return a.value_ < b.value_; }
    friend constexpr bool operator<=(StrongId a, StrongId b) noexcept { return a.value_ <= b.value_; }
    friend constexpr bool operator>(StrongId a, StrongId b) noexcept { return a.value_ > b.value_; }
    friend constexpr bool operator>=(StrongId a, StrongId b) noexcept { return a.value_ >= b.value_; }
    std::string to_string() const { return std::to_string(value_); }
private:
    value_type value_;
};

template <typename Tag>
inline std::ostream& operator<<(std::ostream& os, const StrongId<Tag>& id) { os << id.value(); return os; }

template <typename Tag>
class StrongGeneration {
public:
    using value_type = std::uint64_t;
    constexpr StrongGeneration() noexcept : value_(0) {}
    explicit constexpr StrongGeneration(value_type v) noexcept : value_(v) {}
    constexpr value_type value() const noexcept { return value_; }
    constexpr bool is_valid() const noexcept { return value_ != 0; }
    constexpr StrongGeneration& operator++() noexcept { ++value_; return *this; }
    constexpr StrongGeneration operator++(int) noexcept { StrongGeneration c(*this); ++value_; return c; }
    friend constexpr bool operator==(StrongGeneration a, StrongGeneration b) noexcept { return a.value_ == b.value_; }
    friend constexpr bool operator!=(StrongGeneration a, StrongGeneration b) noexcept { return a.value_ != b.value_; }
    friend constexpr bool operator<(StrongGeneration a, StrongGeneration b) noexcept { return a.value_ < b.value_; }
    friend constexpr bool operator<=(StrongGeneration a, StrongGeneration b) noexcept { return a.value_ <= b.value_; }
    friend constexpr bool operator>(StrongGeneration a, StrongGeneration b) noexcept { return a.value_ > b.value_; }
    friend constexpr bool operator>=(StrongGeneration a, StrongGeneration b) noexcept { return a.value_ >= b.value_; }
    std::string to_string() const { return std::to_string(value_); }
private:
    value_type value_;
};

template <typename Tag>
inline std::ostream& operator<<(std::ostream& os, const StrongGeneration<Tag>& g) { os << g.value(); return os; }

struct BrokerIdTag; struct ResourcePoolIdTag; struct ResourceClassIdTag; struct ResourceInstanceIdTag;
struct ResourceRequestIdTag; struct ReservationIdTag; struct AllocationIdTag; struct LeaseIdTag;
struct OwnerIdTag; struct TenantIdTag; struct WorkloadIdTag; struct NodeIdTag; struct WorkerIdTag;
struct WorkerBootIdTag; struct DeviceIdTag; struct BackendIdTag; struct PolicyIdTag; struct ObservationIdTag;
struct AttemptIdTag; struct DispatchIdTag; struct PreemptionIdTag; struct RecallIdTag; struct ReclamationIdTag;

using BrokerId = StrongId<BrokerIdTag>;
using ResourcePoolId = StrongId<ResourcePoolIdTag>;
using ResourceClassId = StrongId<ResourceClassIdTag>;
using ResourceInstanceId = StrongId<ResourceInstanceIdTag>;
using ResourceRequestId = StrongId<ResourceRequestIdTag>;
using ReservationId = StrongId<ReservationIdTag>;
using AllocationId = StrongId<AllocationIdTag>;
using LeaseId = StrongId<LeaseIdTag>;
using OwnerId = StrongId<OwnerIdTag>;
using TenantId = StrongId<TenantIdTag>;
using WorkloadId = StrongId<WorkloadIdTag>;
using NodeId = StrongId<NodeIdTag>;
using WorkerId = StrongId<WorkerIdTag>;
using WorkerBootId = StrongId<WorkerBootIdTag>;
using DeviceId = StrongId<DeviceIdTag>;
using BackendId = StrongId<BackendIdTag>;
using PolicyId = StrongId<PolicyIdTag>;
using ObservationId = StrongId<ObservationIdTag>;
using AttemptId = StrongId<AttemptIdTag>;
using DispatchId = StrongId<DispatchIdTag>;
using PreemptionId = StrongId<PreemptionIdTag>;
using RecallId = StrongId<RecallIdTag>;
using ReclamationId = StrongId<ReclamationIdTag>;

struct CoordinatorEpochTag; struct BrokerGenerationTag; struct ResourcePoolGenerationTag; struct ResourceGenerationTag;
struct CapacityGenerationTag; struct RequestGenerationTag; struct ReservationGenerationTag; struct AllocationGenerationTag;
struct LeaseGenerationTag; struct OwnerGenerationTag; struct TenantGenerationTag; struct WorkloadGenerationTag;
struct WorkerGenerationTag; struct DeviceGenerationTag; struct BackendGenerationTag; struct PolicyGenerationTag;
struct ObservationGenerationTag; struct AttemptGenerationTag; struct DispatchGenerationTag; struct PreemptionGenerationTag;
struct RecallGenerationTag; struct ReclamationGenerationTag; struct AuthorityGenerationTag;

using CoordinatorEpoch = StrongGeneration<CoordinatorEpochTag>;
using BrokerGeneration = StrongGeneration<BrokerGenerationTag>;
using ResourcePoolGeneration = StrongGeneration<ResourcePoolGenerationTag>;
using ResourceGeneration = StrongGeneration<ResourceGenerationTag>;
using CapacityGeneration = StrongGeneration<CapacityGenerationTag>;
using RequestGeneration = StrongGeneration<RequestGenerationTag>;
using ReservationGeneration = StrongGeneration<ReservationGenerationTag>;
using AllocationGeneration = StrongGeneration<AllocationGenerationTag>;
using LeaseGeneration = StrongGeneration<LeaseGenerationTag>;
using OwnerGeneration = StrongGeneration<OwnerGenerationTag>;
using TenantGeneration = StrongGeneration<TenantGenerationTag>;
using WorkloadGeneration = StrongGeneration<WorkloadGenerationTag>;
using WorkerGeneration = StrongGeneration<WorkerGenerationTag>;
using DeviceGeneration = StrongGeneration<DeviceGenerationTag>;
using BackendGeneration = StrongGeneration<BackendGenerationTag>;
using PolicyGeneration = StrongGeneration<PolicyGenerationTag>;
using ObservationGeneration = StrongGeneration<ObservationGenerationTag>;
using AttemptGeneration = StrongGeneration<AttemptGenerationTag>;
using DispatchGeneration = StrongGeneration<DispatchGenerationTag>;
using PreemptionGeneration = StrongGeneration<PreemptionGenerationTag>;
using RecallGeneration = StrongGeneration<RecallGenerationTag>;
using ReclamationGeneration = StrongGeneration<ReclamationGenerationTag>;
using AuthorityGeneration = StrongGeneration<AuthorityGenerationTag>;

}  // namespace resourcebroker

// Full std::hash specializations (partial specializations of standard
// templates are not portable across compilers, so each concrete identity
// and generation type gets its own explicit specialization).
#define RB_HASH_ID(TAG) namespace std { template<> struct hash<resourcebroker::StrongId<resourcebroker::TAG>> { std::size_t operator()(const resourcebroker::StrongId<resourcebroker::TAG>& v) const noexcept { return std::hash<std::uint64_t>()(v.value()); } }; }
#define RB_HASH_GEN(TAG) namespace std { template<> struct hash<resourcebroker::StrongGeneration<resourcebroker::TAG>> { std::size_t operator()(const resourcebroker::StrongGeneration<resourcebroker::TAG>& v) const noexcept { return std::hash<std::uint64_t>()(v.value()); } }; }

RB_HASH_ID(BrokerIdTag)
RB_HASH_ID(ResourcePoolIdTag)
RB_HASH_ID(ResourceClassIdTag)
RB_HASH_ID(ResourceInstanceIdTag)
RB_HASH_ID(ResourceRequestIdTag)
RB_HASH_ID(ReservationIdTag)
RB_HASH_ID(AllocationIdTag)
RB_HASH_ID(LeaseIdTag)
RB_HASH_ID(OwnerIdTag)
RB_HASH_ID(TenantIdTag)
RB_HASH_ID(WorkloadIdTag)
RB_HASH_ID(NodeIdTag)
RB_HASH_ID(WorkerIdTag)
RB_HASH_ID(WorkerBootIdTag)
RB_HASH_ID(DeviceIdTag)
RB_HASH_ID(BackendIdTag)
RB_HASH_ID(PolicyIdTag)
RB_HASH_ID(ObservationIdTag)
RB_HASH_ID(AttemptIdTag)
RB_HASH_ID(DispatchIdTag)
RB_HASH_ID(PreemptionIdTag)
RB_HASH_ID(RecallIdTag)
RB_HASH_ID(ReclamationIdTag)

RB_HASH_GEN(CoordinatorEpochTag)
RB_HASH_GEN(BrokerGenerationTag)
RB_HASH_GEN(ResourcePoolGenerationTag)
RB_HASH_GEN(ResourceGenerationTag)
RB_HASH_GEN(CapacityGenerationTag)
RB_HASH_GEN(RequestGenerationTag)
RB_HASH_GEN(ReservationGenerationTag)
RB_HASH_GEN(AllocationGenerationTag)
RB_HASH_GEN(LeaseGenerationTag)
RB_HASH_GEN(OwnerGenerationTag)
RB_HASH_GEN(TenantGenerationTag)
RB_HASH_GEN(WorkloadGenerationTag)
RB_HASH_GEN(WorkerGenerationTag)
RB_HASH_GEN(DeviceGenerationTag)
RB_HASH_GEN(BackendGenerationTag)
RB_HASH_GEN(PolicyGenerationTag)
RB_HASH_GEN(ObservationGenerationTag)
RB_HASH_GEN(AttemptGenerationTag)
RB_HASH_GEN(DispatchGenerationTag)
RB_HASH_GEN(PreemptionGenerationTag)
RB_HASH_GEN(RecallGenerationTag)
RB_HASH_GEN(ReclamationGenerationTag)
RB_HASH_GEN(AuthorityGenerationTag)

#undef RB_HASH_ID
#undef RB_HASH_GEN