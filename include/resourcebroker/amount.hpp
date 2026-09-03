// amount.hpp - Dimension-checked resource amounts.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// A ResourceAmount is a tagged value over the strong unit types. It is never
// implicitly converted between dimensions; operations on mismatched
// dimensions throw. The dimension must agree with the ResourceClass it is
// used against.
#pragma once

#include <variant>
#include <stdexcept>
#include <string>
#include "units.hpp"
#include "enums.hpp"
#include "error.hpp"

namespace resourcebroker {

class ResourceAmount {
public:
    // Stored representation; the "Other" case holds a generic scalar.
    using Storage = std::variant<Bytes, BytesPerSecond, Count, ComputeShare, double>;

    ResourceAmount() : storage_(0.0) {}

    // Construct the canonical amount for a dimension.
    static ResourceAmount bytes(Bytes v) { return ResourceAmount{Storage{v}}; }
    static ResourceAmount bytes_per_second(BytesPerSecond v) { return ResourceAmount{Storage{v}}; }
    static ResourceAmount count(Count v) { return ResourceAmount{Storage{v}}; }
    static ResourceAmount share(ComputeShare v) { return ResourceAmount{Storage{v}}; }
    static ResourceAmount other(double v) { return ResourceAmount{Storage{v}}; }

    // Build from a logical value matching a resource class dimension.
    // Throws if the class dimension is not a known scalar dimension.
    static ResourceAmount of_class(ResourceClass cls, std::uint64_t quantity) {
        switch (dimension_of(cls)) {
            case Dimension::Bytes: return bytes(Bytes{quantity});
            case Dimension::BytesPerSecond: return bytes_per_second(BytesPerSecond{quantity});
            case Dimension::Count: return count(Count{quantity});
            case Dimension::Share: return share(ComputeShare{quantity});  // micro-share units
            case Dimension::Other: return other(static_cast<double>(quantity));
        }
        throw BrokerError(ErrorCode::InvalidArgument, "unknown resource dimension");
    }

    Dimension dimension() const {
        switch (storage_.index()) {
            case 0: return Dimension::Bytes;
            case 1: return Dimension::BytesPerSecond;
            case 2: return Dimension::Count;
            case 3: return Dimension::Share;
            case 4: return Dimension::Other;
        }
        return Dimension::Other;
    }

    bool is_zero() const {
        switch (storage_.index()) {
            case 0: return std::get<Bytes>(storage_).value() == 0;
            case 1: return std::get<BytesPerSecond>(storage_).value() == 0;
            case 2: return std::get<Count>(storage_).value() == 0;
            case 3: return std::get<ComputeShare>(storage_).value() == 0;
            case 4: return std::get<double>(storage_) == 0.0;
        }
        return true;
    }

    // Accessors throw if the stored kind does not match the requested dimension.
    Bytes as_bytes() const { return std::get<Bytes>(storage_); }
    BytesPerSecond as_bytes_per_second() const { return std::get<BytesPerSecond>(storage_); }
    Count as_count() const { return std::get<Count>(storage_); }
    ComputeShare as_share() const { return std::get<ComputeShare>(storage_); }
    double as_other() const { return std::get<double>(storage_); }

    ResourceAmount operator+(const ResourceAmount& o) const {
        if (dimension() != o.dimension()) {
            throw BrokerError(ErrorCode::InvalidArgument, "cannot add amounts of different dimensions");
        }
        switch (storage_.index()) {
            case 0: return bytes(std::get<Bytes>(storage_) + std::get<Bytes>(o.storage_));
            case 1: return bytes_per_second(std::get<BytesPerSecond>(storage_) + std::get<BytesPerSecond>(o.storage_));
            case 2: return count(std::get<Count>(storage_) + std::get<Count>(o.storage_));
            case 3: return share(std::get<ComputeShare>(storage_) + std::get<ComputeShare>(o.storage_));
            case 4: return other(std::get<double>(storage_) + std::get<double>(o.storage_));
        }
        return *this;
    }

    ResourceAmount operator-(const ResourceAmount& o) const {
        if (dimension() != o.dimension()) {
            throw BrokerError(ErrorCode::InvalidArgument, "cannot subtract amounts of different dimensions");
        }
        switch (storage_.index()) {
            case 0: return bytes(std::get<Bytes>(storage_) - std::get<Bytes>(o.storage_));
            case 1: return bytes_per_second(std::get<BytesPerSecond>(storage_) - std::get<BytesPerSecond>(o.storage_));
            case 2: return count(std::get<Count>(storage_) - std::get<Count>(o.storage_));
            case 3: return share(std::get<ComputeShare>(storage_) - std::get<ComputeShare>(o.storage_));
            case 4: return other(std::get<double>(storage_) - std::get<double>(o.storage_));
        }
        return *this;
    }

    bool operator<(const ResourceAmount& o) const { return compare(o) < 0; }
    bool operator<=(const ResourceAmount& o) const { return compare(o) <= 0; }
    bool operator>(const ResourceAmount& o) const { return compare(o) > 0; }
    bool operator>=(const ResourceAmount& o) const { return compare(o) >= 0; }
    bool operator==(const ResourceAmount& o) const { return compare(o) == 0; }
    bool operator!=(const ResourceAmount& o) const { return compare(o) != 0; }

    // Compare only within the same dimension; throws otherwise.
    int compare(const ResourceAmount& o) const {
        if (dimension() != o.dimension()) {
            throw BrokerError(ErrorCode::InvalidArgument, "cannot compare amounts of different dimensions");
        }
        switch (storage_.index()) {
            case 0: {
                const auto a = std::get<Bytes>(storage_).value(); const auto b = std::get<Bytes>(o.storage_).value();
                return a < b ? -1 : (a > b ? 1 : 0);
            }
            case 1: {
                const auto a = std::get<BytesPerSecond>(storage_).value(); const auto b = std::get<BytesPerSecond>(o.storage_).value();
                return a < b ? -1 : (a > b ? 1 : 0);
            }
            case 2: {
                const auto a = std::get<Count>(storage_).value(); const auto b = std::get<Count>(o.storage_).value();
                return a < b ? -1 : (a > b ? 1 : 0);
            }
            case 3: {
                const auto a = std::get<ComputeShare>(storage_).value(); const auto b = std::get<ComputeShare>(o.storage_).value();
                return a < b ? -1 : (a > b ? 1 : 0);
            }
            case 4: {
                const double a = std::get<double>(storage_); const double b = std::get<double>(o.storage_);
                return a < b ? -1 : (a > b ? 1 : 0);
            }
        }
        return 0;
    }

    std::string to_string() const {
        switch (storage_.index()) {
            case 0: return std::get<Bytes>(storage_).to_string();
            case 1: return std::get<BytesPerSecond>(storage_).to_string();
            case 2: return std::get<Count>(storage_).to_string();
            case 3: return std::get<ComputeShare>(storage_).to_string();
            case 4: return std::to_string(std::get<double>(storage_));
        }
        return "?";
    }

private:
    explicit ResourceAmount(Storage s) : storage_(std::move(s)) {}
    Storage storage_;
};

inline std::ostream& operator<<(std::ostream& os, const ResourceAmount& a) { os << a.to_string(); return os; }

}  // namespace resourcebroker
