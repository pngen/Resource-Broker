// protocol.cpp - Framed protocol codecs.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/protocol.hpp"
#include "resourcebroker/serdes.hpp"
#include "resourcebroker/digest.hpp"
#include "resourcebroker/error.hpp"

#include <cstring>

namespace resourcebroker {
namespace {
const char kMagic[4] = {'R','B','P','F'};
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kMaxPayload = 16u * 1024u * 1024u;
}

static void write_amount_p(ByteWriter& w, const ResourceAmount& a) {
    switch (a.dimension()) {
        case Dimension::Bytes: w.u8(0); w.u64(a.as_bytes().value()); break;
        case Dimension::BytesPerSecond: w.u8(1); w.u64(a.as_bytes_per_second().value()); break;
        case Dimension::Count: w.u8(2); w.u64(a.as_count().value()); break;
        case Dimension::Share: w.u8(3); w.u64(a.as_share().value()); break;
        case Dimension::Other: { w.u8(4); std::uint64_t bits; std::memcpy(&bits, &(const double&)a.as_other(), 8); w.u64(bits); break; }
    }
}
static ResourceAmount read_amount_p(ByteReader& r, ResourceClass cls) {
    const std::uint8_t k = r.u8(); const std::uint64_t v = r.u64();
    const auto d = dimension_of(cls);
    if (d == Dimension::Bytes) { if (k != 0) throw BrokerError(ErrorCode::MalformedPayload, "amount kind mismatch"); return ResourceAmount::bytes(Bytes{v}); }
    if (d == Dimension::BytesPerSecond) { if (k != 1) throw BrokerError(ErrorCode::MalformedPayload, "amount kind mismatch"); return ResourceAmount::bytes_per_second(BytesPerSecond{v}); }
    if (d == Dimension::Count) { if (k != 2) throw BrokerError(ErrorCode::MalformedPayload, "amount kind mismatch"); return ResourceAmount::count(Count{v}); }
    if (d == Dimension::Share) { if (k != 3) throw BrokerError(ErrorCode::MalformedPayload, "amount kind mismatch"); return ResourceAmount::share(ComputeShare{v}); }
    double dv; std::memcpy(&dv, &v, 8); return ResourceAmount::other(dv);
}
static void write_auth_p(ByteWriter& w, const AuthorityEnvelope& a) { w.gen(a.coordinator_epoch); w.id(a.worker); w.id(a.worker_boot); w.gen(a.broker_generation); }
static AuthorityEnvelope read_auth_p(ByteReader& r) { AuthorityEnvelope a; a.coordinator_epoch=r.gen<CoordinatorEpochTag>(); a.worker=r.id<WorkerIdTag>(); a.worker_boot=r.id<WorkerBootIdTag>(); a.broker_generation=r.gen<BrokerGenerationTag>(); return a; }

std::vector<std::uint8_t> encode_frame(MessageKind kind, const std::vector<std::uint8_t>& payload) {
    if (payload.size() > kMaxPayload) throw BrokerError(ErrorCode::MalformedPayload, "payload too large");
    std::vector<std::uint8_t> out;
    out.reserve(4 + 2 + 2 + 4 + 4 + payload.size());
    out.insert(out.end(), kMagic, kMagic + 4);
    ByteWriter w(out); w.u16(kVersion); w.u16(static_cast<std::uint16_t>(kind)); w.u32(static_cast<std::uint32_t>(payload.size()));
    w.u32(Crc32::compute(payload.data(), payload.size()));
    if (!payload.empty()) out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

DecodedFrame decode_frame(const std::uint8_t* data, std::size_t len) {
    if (len < 4 + 2 + 2 + 4 + 4) throw BrokerError(ErrorCode::TruncatedPayload, "frame too short");
    for (int i = 0; i < 4; ++i) if (data[i] != static_cast<std::uint8_t>(kMagic[i])) throw BrokerError(ErrorCode::BadMagic, "bad frame magic");
    ByteReader r(std::span<const std::uint8_t>(data, len));
    r.u8(); r.u8(); r.u8(); r.u8();
    const std::uint16_t version = r.u16();
    if (version != kVersion) throw BrokerError(ErrorCode::UnsupportedVersion, "unsupported protocol version");
    const std::uint16_t kindv = r.u16();
    const std::uint32_t plen = r.u32();
    const std::uint32_t crc = r.u32();
    if (plen > kMaxPayload) throw BrokerError(ErrorCode::MalformedPayload, "oversized payload");
    if (plen != r.remaining()) throw BrokerError(ErrorCode::TruncatedPayload, "payload length mismatch");
    const auto payload = r.raw(plen);
    if (Crc32::compute(payload.data(), payload.size()) != crc) throw BrokerError(ErrorCode::ChecksumMismatch, "frame checksum mismatch");
    DecodedFrame df; df.kind = static_cast<MessageKind>(kindv); df.payload.assign(payload.begin(), payload.end());
    return df;
}

std::vector<std::uint8_t> encode_hello(WorkerId worker) { std::vector<std::uint8_t> p; ByteWriter w(p); w.id(worker); return p; }
HelloDecoded decode_hello(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); HelloDecoded h; h.worker = r.id<WorkerIdTag>(); return h; }

std::vector<std::uint8_t> encode_request(const ResourceRequest& req, const AuthorityEnvelope& auth) {
    std::vector<std::uint8_t> p; ByteWriter w(p);
    w.id(req.request_id); w.gen(req.request_generation); w.id(req.owner); w.id(req.tenant); w.id(req.workload); w.gen(req.workload_generation);
    w.u32(static_cast<std::uint32_t>(req.priority)); w.bool_(req.urgent); w.u64(static_cast<std::uint64_t>(req.deadline.nanoseconds())); w.u64(static_cast<std::uint64_t>(req.duration_expectation.nanoseconds()));
    w.bool_(req.requires_lease); w.u64(static_cast<std::uint64_t>(req.lease_duration.nanoseconds())); w.u32(static_cast<std::uint32_t>(req.reclaimability)); w.bool_(req.preemption_eligible); w.bool_(req.atomic);
    w.gen(req.policy_generation); w.gen(req.authority); w.u32(static_cast<std::uint32_t>(req.provenance)); w.string(req.description);
    w.u32(static_cast<std::uint32_t>(req.requirements.size()));
    for (const auto& rq : req.requirements) {
        w.u32(static_cast<std::uint32_t>(rq.resource_class)); w.id(rq.class_id); w.u32(static_cast<std::uint32_t>(rq.kind)); w.u32(static_cast<std::uint32_t>(rq.semantics));
        write_amount_p(w, rq.requested); write_amount_p(w, rq.minimum); write_amount_p(w, rq.preferred);
        w.id(rq.pool_hint); w.id(rq.node_hint); w.id(rq.device_hint); w.id(rq.backend_hint);
    }
    write_auth_p(w, auth);
    return p;
}
RequestDecoded decode_request(const std::vector<std::uint8_t>& p) {
    ByteReader r(std::span<const std::uint8_t>(p.data(), p.size()));
    RequestDecoded d; d.req.request_id=r.id<ResourceRequestIdTag>(); d.req.request_generation=r.gen<RequestGenerationTag>();
    d.req.owner=r.id<OwnerIdTag>(); d.req.tenant=r.id<TenantIdTag>(); d.req.workload=r.id<WorkloadIdTag>(); d.req.workload_generation=r.gen<WorkloadGenerationTag>();
    d.req.priority=static_cast<Priority>(r.u32()); d.req.urgent=r.bool_(); d.req.deadline=Duration(static_cast<std::int64_t>(r.u64())); d.req.duration_expectation=Duration(static_cast<std::int64_t>(r.u64()));
    d.req.requires_lease=r.bool_(); d.req.lease_duration=Duration(static_cast<std::int64_t>(r.u64())); d.req.reclaimability=static_cast<Reclaimability>(r.u32()); d.req.preemption_eligible=r.bool_(); d.req.atomic=r.bool_();
    d.req.policy_generation=r.gen<PolicyGenerationTag>(); d.req.authority=r.gen<AuthorityGenerationTag>(); d.req.provenance=static_cast<Provenance>(r.u32()); d.req.description=r.string();
    const auto n = r.u32(); if (n > 64) throw BrokerError(ErrorCode::MalformedPayload, "too many requirements");
    for (std::uint32_t i = 0; i < n; ++i) { ResourceRequirement rq; rq.resource_class=static_cast<ResourceClass>(r.u32()); rq.class_id=r.id<ResourceClassIdTag>(); rq.kind=static_cast<RequirementKind>(r.u32()); rq.semantics=static_cast<CapacitySemantics>(r.u32());
        rq.requested=read_amount_p(r, rq.resource_class); rq.minimum=read_amount_p(r, rq.resource_class); rq.preferred=read_amount_p(r, rq.resource_class);
        rq.pool_hint=r.id<ResourcePoolIdTag>(); rq.node_hint=r.id<NodeIdTag>(); rq.device_hint=r.id<DeviceIdTag>(); rq.backend_hint=r.id<BackendIdTag>(); d.req.requirements.push_back(rq); }
    d.auth = read_auth_p(r);
    return d;
}

std::vector<std::uint8_t> encode_grant(const WireResult& rr) { std::vector<std::uint8_t> p; ByteWriter w(p); w.u32(static_cast<std::uint32_t>(rr.outcome)); w.id(rr.reservation_id); w.string(rr.message); return p; }
WireResult decode_grant(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); WireResult w; w.outcome=static_cast<RequestOutcome>(r.u32()); w.reservation_id=r.id<ReservationIdTag>(); w.message=r.string(); return w; }
std::vector<std::uint8_t> encode_activate(ReservationId res, const AuthorityEnvelope& auth) { std::vector<std::uint8_t> p; ByteWriter w(p); w.id(res); write_auth_p(w, auth); return p; }
ActivateDecoded decode_activate(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); ActivateDecoded a; a.res=r.id<ReservationIdTag>(); a.auth=read_auth_p(r); return a; }
std::vector<std::uint8_t> encode_release(ReservationId res, const AuthorityEnvelope& auth) { std::vector<std::uint8_t> p; ByteWriter w(p); w.id(res); write_auth_p(w, auth); return p; }
ReleaseDecoded decode_release(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); ReleaseDecoded a; a.res=r.id<ReservationIdTag>(); a.auth=read_auth_p(r); return a; }
std::vector<std::uint8_t> encode_query(const std::string& what, const AuthorityEnvelope& auth) { std::vector<std::uint8_t> p; ByteWriter w(p); w.string(what); write_auth_p(w, auth); return p; }
QueryDecoded decode_query(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); QueryDecoded q; q.what=r.string(); q.auth=read_auth_p(r); return q; }
std::vector<std::uint8_t> encode_ack(const std::string& status) { std::vector<std::uint8_t> p; ByteWriter w(p); w.string(status); return p; }
std::string decode_ack(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); return r.string(); }


std::vector<std::uint8_t> encode_hello_result(WorkerBootId boot, CoordinatorEpoch epoch) { std::vector<std::uint8_t> p; ByteWriter w(p); w.u64(boot.value()); w.gen(epoch); return p; }
HelloResult decode_hello_result(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); HelloResult h; h.boot = WorkerBootId(r.u64()); h.epoch = r.gen<CoordinatorEpochTag>(); return h; }

static void write_res_p(ByteWriter& w, const ResourceDescriptor& r) {
    w.id(r.instance_id); w.id(r.class_id); w.u32(static_cast<std::uint32_t>(r.resource_class));
    w.id(r.node); w.id(r.device); w.id(r.backend); w.id(r.pool_id); w.gen(r.resource_generation); w.gen(r.capacity_generation);
    w.u32(static_cast<std::uint32_t>(r.health)); w.u32(static_cast<std::uint32_t>(r.freshness)); w.u32(static_cast<std::uint32_t>(r.provenance)); w.gen(r.policy_generation);
    write_amount_p(w, r.nominal); write_amount_p(w, r.governed); write_amount_p(w, r.available); write_amount_p(w, r.reserved); write_amount_p(w, r.allocated); write_amount_p(w, r.reclaimable);
}
static ResourceDescriptor read_res_p(ByteReader& r) {
    ResourceDescriptor d; d.instance_id=r.id<ResourceInstanceIdTag>(); d.class_id=r.id<ResourceClassIdTag>();
    d.resource_class=static_cast<ResourceClass>(r.u32()); d.node=r.id<NodeIdTag>(); d.device=r.id<DeviceIdTag>(); d.backend=r.id<BackendIdTag>(); d.pool_id=r.id<ResourcePoolIdTag>();
    d.resource_generation=r.gen<ResourceGenerationTag>(); d.capacity_generation=r.gen<CapacityGenerationTag>();
    d.health=static_cast<Health>(r.u32()); d.freshness=static_cast<Freshness>(r.u32()); d.provenance=static_cast<Provenance>(r.u32()); d.policy_generation=r.gen<PolicyGenerationTag>();
    d.nominal=read_amount_p(r, d.resource_class); d.governed=read_amount_p(r, d.resource_class); d.available=read_amount_p(r, d.resource_class); d.reserved=read_amount_p(r, d.resource_class); d.allocated=read_amount_p(r, d.resource_class); d.reclaimable=read_amount_p(r, d.resource_class);
    return d;
}
std::vector<std::uint8_t> encode_register_resource(const ResourceDescriptor& res, const AuthorityEnvelope& auth) { std::vector<std::uint8_t> p; ByteWriter w(p); write_res_p(w, res); write_auth_p(w, auth); return p; }
RegisterResourceDecoded decode_register_resource(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); RegisterResourceDecoded d; d.res=read_res_p(r); d.auth=read_auth_p(r); return d; }

std::vector<std::uint8_t> encode_report_allocation(ReservationId res, ResourceInstanceId instance, ResourceClass cls, ResourceAmount amount, std::string evidence, const AuthorityEnvelope& auth) { std::vector<std::uint8_t> p; ByteWriter w(p); w.id(res); w.id(instance); w.u32(static_cast<std::uint32_t>(cls)); write_amount_p(w, amount); w.string(evidence); write_auth_p(w, auth); return p; }
ReportAllocationDecoded decode_report_allocation(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); ReportAllocationDecoded d; d.res=r.id<ReservationIdTag>(); d.instance=r.id<ResourceInstanceIdTag>(); d.cls=static_cast<ResourceClass>(r.u32()); d.amount=read_amount_p(r, d.cls); d.evidence=r.string(); d.auth=read_auth_p(r); return d; }

std::vector<std::uint8_t> encode_renew_lease(LeaseId lease, const AuthorityEnvelope& auth) { std::vector<std::uint8_t> p; ByteWriter w(p); w.id(lease); write_auth_p(w, auth); return p; }
RenewLeaseDecoded decode_renew_lease(const std::vector<std::uint8_t>& p) { ByteReader r(std::span<const std::uint8_t>(p.data(), p.size())); RenewLeaseDecoded d; d.lease=r.id<LeaseIdTag>(); d.auth=read_auth_p(r); return d; }
std::vector<std::uint8_t> encode_query_result(const std::string& text) { return encode_ack(text); }
std::string decode_query_result(const std::vector<std::uint8_t>& p) { return decode_ack(p); }

}  // namespace resourcebroker