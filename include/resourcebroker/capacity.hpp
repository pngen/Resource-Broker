// capacity.hpp - Capacity tracking with a strict accounting invariant.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
//
// For each governed resource dimension, the invariant is:
//     governed >= reserved + allocated + unavailable
// with no negative values and no silent overcommit. Every mutation is checked.
#pragma once

#include "amount.hpp"
#include "enums.hpp"
#include "identities.hpp"
#include "error.hpp"

namespace resourcebroker {

// A single-dimension capacity ledger for one pool/resource. All amounts
// share the dimension of the owning resource class. Reclaimable is a view
// of the allocated amount (reclaimable <= allocated), never an extra charge.
class CapacityLedger {
public:
    CapacityLedger(ResourceClass cls, ResourceAmount governed, Provenance prov)
        : cls_(cls), provenance_(prov) {
        if (dimension_of(cls_) != governed.dimension()) {
            throw BrokerError(ErrorCode::UnsupportedCapacity,
                              "capacity dimension does not match resource class");
        }
        nominal_ = governed;
        reported_ = governed;
        measured_ = ResourceAmount::of_class(cls_, 0);
        reserved_ = ResourceAmount::of_class(cls_, 0);
        allocated_ = ResourceAmount::of_class(cls_, 0);
        reclaimable_ = ResourceAmount::of_class(cls_, 0);
        unavailable_ = ResourceAmount::of_class(cls_, 0);
        governed_ = governed;
    }

    ResourceClass resource_class() const { return cls_; }
    Provenance provenance() const { return provenance_; }

    ResourceAmount nominal() const { return nominal_; }
    ResourceAmount reported() const { return reported_; }
    ResourceAmount measured() const { return measured_; }
    ResourceAmount governed() const { return governed_; }
    ResourceAmount reserved() const { return reserved_; }
    ResourceAmount allocated() const { return allocated_; }
    ResourceAmount reclaimable() const { return reclaimable_; }
    ResourceAmount unavailable() const { return unavailable_; }
    ResourceAmount free() const { return governed_ - reserved_ - allocated_ - unavailable_; }

    void set_nominal(ResourceAmount n) { nominal_ = n; }
    void set_reported(ResourceAmount r) { reported_ = r; }
    void set_measured(ResourceAmount m) { measured_ = m; }

    // Set governed capacity; throws if the target cannot cover commitments.
    void set_governed(ResourceAmount target) {
        if (target.dimension() != governed_.dimension()) {
            throw BrokerError(ErrorCode::UnsupportedCapacity, "new governed capacity dimension mismatch");
        }
        if (target < (reserved_ + allocated_ + unavailable_)) {
            throw BrokerError(ErrorCode::InsufficientCapacity, "governed capacity below committed amount");
        }
        governed_ = target;
    }

    // Account for capacity that is administratively unavailable.
    void set_unavailable(ResourceAmount amount) {
        if ((governed_ - reserved_ - allocated_) < amount) {
            throw BrokerError(ErrorCode::InsufficientCapacity, "unavailable exceeds free governed capacity");
        }
        unavailable_ = amount;
    }

    bool can_reserve(const ResourceAmount& amount) const {
        if (amount.dimension() != governed_.dimension()) return false;
        return free() >= amount;
    }

    void reserve(const ResourceAmount& amount) {
        if (!can_reserve(amount)) {
            throw BrokerError(ErrorCode::InsufficientCapacity, "cannot reserve: insufficient free governed capacity");
        }
        reserved_ = reserved_ + amount;
    }

    // Move amount from reserved into allocated (physical/logical allocation).
    void allocate(const ResourceAmount& amount) {
        if (amount.dimension() != governed_.dimension() || reserved_ < amount) {
            throw BrokerError(ErrorCode::InsufficientCapacity, "cannot allocate: amount not reserved");
        }
        reserved_ = reserved_ - amount;
        allocated_ = allocated_ + amount;
    }

    void release_allocated(const ResourceAmount& amount) {
        if (allocated_ < amount) {
            throw BrokerError(ErrorCode::InvalidState, "cannot release: allocated less than amount");
        }
        const ResourceAmount reclaimRemoved = reclaimable_ >= amount ? amount : reclaimable_;
        reclaimable_ = reclaimable_ - reclaimRemoved;
        allocated_ = allocated_ - amount;
    }

    void release_reserved(const ResourceAmount& amount) {
        if (reserved_ < amount) {
            throw BrokerError(ErrorCode::InvalidState, "cannot release: reserved less than amount");
        }
        reserved_ = reserved_ - amount;
    }

    // Directly bind capacity to an allocation on a resource instance that is
    // not managed through a reservation's reserved bucket.
    void bind_allocated(const ResourceAmount& amount) {
        if (free() < amount) {
            throw BrokerError(ErrorCode::InsufficientCapacity, "bind_allocated: insufficient free capacity");
        }
        allocated_ = allocated_ + amount;
    }

    // Return a released allocation back to the reservation's reserved bucket
    // (capacity stays committed to the reservation, just no longer bound).
    void release_allocation_to_reserved(const ResourceAmount& amount) {
        if (allocated_ < amount) {
            throw BrokerError(ErrorCode::InvalidState, "release_allocation_to_reserved: allocated less than amount");
        }
        const ResourceAmount reclaimRemoved = reclaimable_ >= amount ? amount : reclaimable_;
        reclaimable_ = reclaimable_ - reclaimRemoved;
        allocated_ = allocated_ - amount;
        reserved_ = reserved_ + amount;
    }

    void mark_reclaimable(const ResourceAmount& amount) {
        if (amount.dimension() != governed_.dimension() || allocated_ < amount) {
            throw BrokerError(ErrorCode::InvalidState, "cannot mark reclaimable: exceeds allocated");
        }
        reclaimable_ = amount;
    }

    // Persistence reconstruction: set the full snapshot, validating the
    // accounting invariant and the reclaimable<=allocated property.
    void restore_state(const ResourceAmount& nominal, const ResourceAmount& reported,
                       const ResourceAmount& measured, const ResourceAmount& governed,
                       const ResourceAmount& reserved, const ResourceAmount& allocated,
                       const ResourceAmount& reclaimable, const ResourceAmount& unavailable) {
        if (governed < (reserved + allocated + unavailable)) {
            throw BrokerError(ErrorCode::AccountingInvariant, "restore_state violates accounting invariant");
        }
        if (reclaimable > allocated) {
            throw BrokerError(ErrorCode::AccountingInvariant, "restore_state: reclaimable exceeds allocated");
        }
        nominal_ = nominal; reported_ = reported; measured_ = measured; governed_ = governed;
        reserved_ = reserved; allocated_ = allocated; reclaimable_ = reclaimable; unavailable_ = unavailable;
    }

    ResourceAmount deficit_if_governed(const ResourceAmount& target) const {
        const ResourceAmount committed = reserved_ + allocated_ + unavailable_;
        return target < committed ? (committed - target) : ResourceAmount::of_class(cls_, 0);
    }

private:
    ResourceClass cls_;
    Provenance provenance_;
    ResourceAmount nominal_;
    ResourceAmount reported_;
    ResourceAmount measured_;
    ResourceAmount governed_;
    ResourceAmount reserved_;
    ResourceAmount allocated_;
    ResourceAmount reclaimable_;
    ResourceAmount unavailable_;
};

}  // namespace resourcebroker
