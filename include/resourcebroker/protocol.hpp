// protocol.hpp - Bounded versioned framed protocol for coordinator/workers.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
// Frame: magic(4='RBPF') | version(u16) | kind(u16) | length(u32) | crc32(payload)(u32) | payload.
// Readers reject bad magic, unsupported version, oversized payload, truncation,
// checksum mismatch, invalid enums, and trailing garbage.
#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "authority.hpp"
#include "request.hpp"
#include "reservation.hpp"
#include "resource.hpp"
namespace resourcebroker {

enum class MessageKind : std::uint16_t {
    HELLO = 1,
    REGISTER_RESOURCE = 2,
    UPDATE_CAPACITY = 3,
    REQUEST_RESOURCES = 4,
    GRANT_RESULT = 5,
    ACTIVATE_RESERVATION = 6,
    RENEW_LEASE = 7,
    REPORT_ALLOCATION = 8,
    RELEASE = 9,
    RECALL = 10,
    RECALL_ACK = 11,
    PREEMPT = 12,
    PREEMPT_ACK = 13,
    UPDATE_HEALTH = 14,
    QUERY = 15,
    QUERY_RESULT = 16,
    SAVE = 17,
    RECOVER = 18,
    ACK = 19,
    ERROR = 20
};

// Encode a frame; returns the full frame bytes.
std::vector<std::uint8_t> encode_frame(MessageKind kind, const std::vector<std::uint8_t>& payload);

// Decode a frame from a byte buffer consumed from a connection. Throws
// BrokerError on malformed input.
struct DecodedFrame { MessageKind kind; std::vector<std::uint8_t> payload; };
DecodedFrame decode_frame(const std::uint8_t* data, std::size_t len);

// A descriptor that carries a result/outcome over the wire.
struct WireResult {
    MessageKind kind = MessageKind::GRANT_RESULT;
    RequestOutcome outcome = RequestOutcome::UNKNOWN;
    ReservationId reservation_id;
    std::string message;
};

// Payload codecs for the principal messages.
std::vector<std::uint8_t> encode_hello(WorkerId worker);
struct HelloDecoded { WorkerId worker; WorkerBootId boot; };
HelloDecoded decode_hello(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_request(const ResourceRequest& req, const AuthorityEnvelope& auth);
struct RequestDecoded { ResourceRequest req; AuthorityEnvelope auth; };
RequestDecoded decode_request(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_grant(const WireResult& r);
WireResult decode_grant(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_activate(ReservationId res, const AuthorityEnvelope& auth);
struct ActivateDecoded { ReservationId res; AuthorityEnvelope auth; };
ActivateDecoded decode_activate(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_release(ReservationId res, const AuthorityEnvelope& auth);
struct ReleaseDecoded { ReservationId res; AuthorityEnvelope auth; };
ReleaseDecoded decode_release(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_query(const std::string& what, const AuthorityEnvelope& auth);
struct QueryDecoded { std::string what; AuthorityEnvelope auth; };
QueryDecoded decode_query(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_ack(const std::string& status);
std::string decode_ack(const std::vector<std::uint8_t>& p);

struct HelloResult { WorkerBootId boot; CoordinatorEpoch epoch; };
std::vector<std::uint8_t> encode_hello_result(WorkerBootId boot, CoordinatorEpoch epoch);
HelloResult decode_hello_result(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_register_resource(const ResourceDescriptor& res, const AuthorityEnvelope& auth);
struct RegisterResourceDecoded { ResourceDescriptor res; AuthorityEnvelope auth; };
RegisterResourceDecoded decode_register_resource(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_report_allocation(ReservationId res, ResourceInstanceId instance, ResourceClass cls, ResourceAmount amount, std::string evidence, const AuthorityEnvelope& auth);
struct ReportAllocationDecoded { ReservationId res; ResourceInstanceId instance; ResourceClass cls; ResourceAmount amount; std::string evidence; AuthorityEnvelope auth; };
ReportAllocationDecoded decode_report_allocation(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_renew_lease(LeaseId lease, const AuthorityEnvelope& auth);
struct RenewLeaseDecoded { LeaseId lease; AuthorityEnvelope auth; };
RenewLeaseDecoded decode_renew_lease(const std::vector<std::uint8_t>& p);

std::vector<std::uint8_t> encode_query_result(const std::string& text);
std::string decode_query_result(const std::vector<std::uint8_t>& p);
}  // namespace resourcebroker