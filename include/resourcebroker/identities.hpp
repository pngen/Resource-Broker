// identities.hpp - Strong non-interchangeable identities and generations.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#pragma once

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>

namespace resourcebroker {

// A strong, non-interchangeable identifier that wraps a raw 64-bit value.
// Distinct tag types make identifiers of different kinds impossible to mix
// implicitly. A zero value denotes an invalid/unspecified identifier.
template <typename Tag>
class StrongId {
public:
    using value_type = std::uint64_t;

    constexpr StrongId() noexcept : value_(0) {}
    explicit constexpr StrongId(value_type v) noexcept : value_(v) {}

    constexpr value_type value() const noexcept { return value_; }
    constexpr bool is_valid() const noexcept { return value_ != 0; }

    constexpr StrongId& operator++() noexcept {
        ++value_;
        return *this;
    }
    constexpr StrongId operator++(int) noexcept {
        StrongId copy(*this);
        ++value_;
        return copy;
    }

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
inline std::ostream& operator<<(std::ostream& os, const StrongId<Tag>& id) {
    os << id.value();
    return os;
}

template <typename Tag>
struct std::hash<resourcebroker::StrongId<Tag>> {
    std::size_t operator()(const resourcebroker::StrongId<Tag>& id) const noexcept {
        return std::hash<typename resourcebroker::StrongId<Tag>::value_type>()(id.value());
    }
};

// A strong, explicitly comparable generation counter. A generation is
// incarnation-scoped: the broker compares generations only after the
// incarnation (WorkerBootId / CoordinatorEpoch) has been verified to match.
// Thus a large generation number owned by a stale incarnation can never fence
// a fresh one.
template <typename Tag>
class StrongGeneration {
public:
    using value_type = std::uint64_t;

    constexpr StrongGeneration() noexcept : value_(0) {}
    explicit constexpr StrongGeneration(value_type v) noexcept : value_(v) {}

    constexpr value_type value() const noexcept { return value_; }
    constexpr bool is_valid() const noexcept { return value_ != 0; }

    constexpr StrongGeneration& operator++() noexcept {
        ++value_;
        return *this;
    }
    constexpr StrongGeneration operator++(int) noexcept {
        StrongGeneration copy(*this);
        ++value_;
        return copy;
    }

    // Explicit comparison operators. Callers that perform cross-incarnation
    // comparisons must first establish incarnations match.
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
inline std::ostream& operator<<(std::ostream& os, const StrongGeneration<Tag>& g) {
    os << g.value();
    return os;
}

template <typename Tag>
struct std::hash<resourcebroker::StrongGeneration<Tag>> {
    std::size_t operator()(const resourcebroker::StrongGeneration<Tag>& g) const noexcept {
        return std::hash<typename resourcebroker::StrongGeneration<Tag>::value_type>()(g.value());
    }
};

// Identity tag declarations. Each tag is an incomplete distinct type used only
// to make the corresponding strong type non-interchangeable.
struct BrokerIdTag;
struct ResourcePoolIdTag;
struct ResourceClassIdTag;
struct ResourceInstanceIdTag;
struct ResourceRequestIdTag;
struct ReservationIdTag;
struct AllocationIdTag;
struct LeaseIdTag;
struct OwnerIdTag;
struct TenantIdTag;
struct WorkloadIdTag;
struct NodeIdTag;
struct WorkerIdTag;
struct WorkerBootIdTag;
struct DeviceIdTag;
struct BackendIdTag;
struct PolicyIdTag;
struct ObservationIdTag;
struct AttemptIdTag;
struct DispatchIdTag;
struct PreemptionIdTag;
struct RecallIdTag;
struct ReclamationIdTag;

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

// Generation tag declarations.
struct CoordinatorEpochTag;
struct BrokerGenerationTag;
struct ResourcePoolGenerationTag;
struct ResourceGenerationTag;
struct CapacityGenerationTag;
struct RequestGenerationTag;
struct ReservationGenerationTag;
struct AllocationGenerationTag;
struct LeaseGenerationTag;
struct OwnerGenerationTag;
struct TenantGenerationTag;
struct WorkloadGenerationTag;
struct WorkerGenerationTag;
struct DeviceGenerationTag;
struct BackendGenerationTag;
struct PolicyGenerationTag;
struct ObservationGenerationTag;
struct AttemptGenerationTag;
struct DispatchGenerationTag;
struct PreemptionGenerationTag;
struct RecallGenerationTag;
struct ReclamationGenerationTag;
struct AuthorityGenerationTag;

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
