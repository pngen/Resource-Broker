# Resource Broker

Resource Broker is an open-source, vendor-neutral C++20 runtime that provides the
common arbitration boundary for scarce heterogeneous AI infrastructure resources.

It turns heterogeneous resource claims into explicit requests, reservations, leases,
allocations, releases, recalls, preemptions, and authoritative accounting.

## The systems question

Resource Broker owns one boundary:

> Who gets scarce heterogeneous AI infrastructure resources, how much, for how long,
> under what guarantees and priorities, and which reservation or allocation remains
> authoritative after contention, cancellation, failure, restart, or policy change?

The architectural thesis is:

> Resource availability is not resource ownership.

Modern AI infrastructure contains accelerator memory, compute capacity, host memory,
pinned memory, cache capacity, transfer bandwidth, storage bandwidth, model residency,
execution slots, and other scarce resources demanded simultaneously by inference,
compilation, movement, checkpointing, warming, recovery, and background work. Those
demands cannot be governed safely through disconnected counters, best-effort
allocation, or subsystem-local reservations.

Resource Broker provides that common boundary. It does not replace the runtimes that
decide whether work may enter, what tenants are entitled to, how links are shaped, or
where work is placed.

## What Resource Broker is not

- **Admission Fabric** decides whether new work should enter. Resource Broker does not.
- **Quota Fabric** defines what tenants are entitled to consume. Resource Broker only
  references tenant/owner identity and optional entitlement metadata.
- **Bandwidth Governor** governs link capacity among competing flows. Resource Broker
  reserves bandwidth classes as scalar governed capacity but does not implement flow
  shaping.
- **Storage Fabric**, **Runtime Registry**, and **placement/scheduling runtimes** are
  outside the boundary.

Resource Broker owns cross-resource reservation and arbitration once demand exists.

## Resource classes

Resources are modeled explicitly and never reduced to one untyped integer. Supported
classes include:

- GPU memory bytes
- GPU compute share
- host memory bytes
- pinned host memory bytes
- cache bytes
- model residency bytes
- execution slots
- transfer bandwidth (B/s)
- storage bandwidth (B/s)
- network bandwidth (B/s)
- concurrent workload slots
- generic scalar resource

Distinct strong unit types prevent accidentally mixing resource dimensions.

## Resource pools

Pools group compatible resources of a single class. Pools may be DEVICE_LOCAL,
NODE_LOCAL, HOST_LOCAL, CLUSTER_CLASS, BACKEND_LOCAL, SYNTHETIC_REMOTE, or UNKNOWN.
UNKNOWN remains UNKNOWN; the model does not guess.

## Capacity model

Capacity tracks nominal, reported, measured, governed, reserved, allocated,
reclaimable, unavailable, and free-governed amounts separately, with explicit
provenance (MEASURED, REPORTED, DERIVED, SYNTHETIC, UNKNOWN). Derived capacity is
never mislabeled measured. Capacity changes require a CapacityGeneration, and stale
reports cannot overwrite current capacity.

The accounting invariant is enforced atomically for every reservation/allocation/
release/reclaim action:

    governed >= reserved + allocated + unavailable

No negative values, no overflow, and no silent overcommit unless policy explicitly
allows bounded soft overcommit.

## Requests, reservations, allocations

A ResourceRequest is a set of resource requirements evaluated atomically when
configured. Requirements distinguish REQUIRED / PREFERRED / OPTIONAL and capacity
semantics EXACT / MINIMUM / UP_TO / ELASTIC_RANGE. An elastic request may accept less
than preferred but never below its defined minimum; a hard requirement is never
silently shrunk. Unknown capacity never produces a silent grant.

A reservation is a first-class authoritative state. Its lifecycle is PLANNED,
RESERVED, ACTIVATING, ACTIVE, RECALL_REQUESTED, PREEMPTING, RELEASING, RELEASED,
EXPIRED, REVOKED, STALE, FAILED with all transitions guarded. Reservation and physical
allocation are distinct: a reservation may reserve capacity before any physical
CUDA allocation is made. Physical allocation failure never leaves stale broker
accounting.

Multi-resource reservation is transactional: validate request, validate authority,
resolve pools, evaluate hard constraints, reserve provisional capacity for every
required dimension, validate cross-resource consistency, then commit atomically. If
any dimension fails, every earlier provisional reservation is rolled back and no
partial authoritative reservation remains.

## Leases

Reservations may use leases with acquire, renew, expire, revoke, and revalidate.
Liveness is tied to a WorkerBootId incarnation, not solely to wall-clock expiration.
A stale WorkerBootId cannot renew a fresh lease.

## Priorities and reclaimability

Priorities are CRITICAL, HIGH, NORMAL, LOW, BACKGROUND. Priority does not
automatically override hard guarantees; a lower-priority reservation protected by
explicit non-preemptible policy remains protected. Reclaimability is NON_RECLAIMABLE,
COOPERATIVE, RECLAIMABLE, or PREEMPTIBLE. Recall and preemption have separate
semantics and never imply arbitrary process termination by the broker.

Borrowed capacity is explicit, attributable, recallable, generation-fenced, bounded,
and visible in accounting. Resource Broker consumes external borrow-allowed decisions
and manages the resulting resource claim; it does not duplicate quota entitlement
logic.

## Arbitration

Arbitration is deterministic and explainable. Hard constraints are considered first,
then named factors such as priority class, hard guarantee, existing reservation
strength, deadline/urgency, starvation age, progress, reclaimability, preemption cost,
locality, fragmentation impact, minimum useful allocation, fairness history, and policy
preference. It is not one opaque master score. Ties break deterministically and an
explanation is always produced.

## Capacity shrink and reclamation

When capacity shrinks below active commitments, the broker never silently rewrites
history. It identifies the overcommitted amount, surfaces RECALL_REQUIRED or
PREEMPT_REQUIRED according to policy, produces a deterministic reclamation plan, and
preserves exact accounting. Protected workloads are not preempted merely because it is
convenient.

## Authority, freshness, and recovery

Every mutation carries authority: CoordinatorEpoch, WorkerBootId, and the relevant
generation(s). Authority is incarnation-scoped: a larger generation owned by a stale
WorkerBootId never fences a fresh process incarnation. Recovered physical observations
do not silently become CURRENT; they require revalidation. On recovery, live worker
authority is cleared, physical allocation evidence and active leases require
revalidation, and logical reservations are preserved only according to explicit
recovery policy.

Persistence is deterministic, versioned binary with magic, version, bounded counts,
CRC-32, and a SHA-256 semantic digest, written atomically (temp, flush, close, rename)
and rejected on bad magic, unsupported version, truncation, checksum mismatch, digest
mismatch, impossible counts, duplicate identity, generation regression, malformed
records, or impossible accounting.

## Multiprocess authority proof

A coordinator and worker processes are real OS processes over a bounded, versioned,
framed TCP protocol. The proof exercises registration, an atomic multi-resource grant, a
competing request, real process kill/restart, stale WorkerBootId/epoch/generation
rejection, post-restart fresh requests, and coordinator persistence/recovery with exact
final accounting.

## Real host-memory and CUDA proofs

- **Real host memory**: a bounded governed host-memory pool is reserved, physically
  allocated, written, verified, released, and the accounting returns exactly to
  baseline. The pool never claims all free system memory.
- **Real CUDA (RTX 5090)**: a bounded governed GPU-memory pool is reserved, a real
  cudaMalloc allocation is made and used with a real kernel, verified against a CPU
  reference, released, and the accounting closes exactly. An oversize request is
  rejected/deferred by the broker before any physical allocation. Broker-governed GPU
  capacity is kept explicitly separate from total physical device memory.
- **Pinned host memory**: cudaHostAlloc pinned memory is used for allocation and
  released, with accounting closure.

## Synthetic heterogeneous-resource scenarios

Deterministic synthetic pools cover GPU-memory contention, pinned-memory contention,
execution-slot contention, bandwidth contention, multi-resource atomic success, final-
dimension atomic rollback, elastic partial grant, hard-requirement preservation,
high-priority versus low-priority reclaimable reservations, protected reservation
survival, recall satisfying an incoming guarantee, preemption-required, capacity
generation rollover, worker restart, stale lease renewal, stale release, duplicate
release, capacity shrink, degraded or unavailable pools, unknown capacity, fairness,
deterministic tie-break, policy-generation change, remote synthetic bandwidth, and
recovery/revalidation. All synthetic facts carry explicit SYNTHETIC provenance.

## CLI

The `resource-broker` executable exposes register-pool, capacity, request, reservations,
allocations, activate, release, lease-renew, recall, preempt, reclamation-plan, owner,
explain, simulate, save, recover, and benchmark commands. Output exposes pools, classes,
capacity, reserved, allocated, available, reclaimable, reservation and allocation ids,
owners, WorkerBootIds, generations, priority, reclaimability, lease, authority,
provenance, outcome, binding constraint, and explanation.

## Examples

Runnable examples are provided in the `examples/` directory demonstrating resource
identity, single-resource reservation, multi-resource atomic reservation, elastic
requests, GPU-memory reservation, pinned memory, execution slots, contention
arbitration, recall, preemption planning, capacity shrink, lease liveness, persistence
and recovery, multiprocess authority, and CUDA resource use. Examples use the real
public library API.

## Benchmarks

Completed-work benchmarks report ops/s and ns/op for request canonicalization,
single- and multi-resource evaluation, reservation commit and release, allocation
update, capacity query, arbitration, reclamation planning, lease renewal, capacity
update, index lookup, persistence serialize/recover, protocol encode/decode, and
concurrent request/release workloads. Honest reporting notes where 100k scale is not
practical.

## Downstream package consumption

Install to a clean prefix, then: `find_package(ResourceBroker CONFIG REQUIRED)` and
`target_link_libraries(... ResourceBroker::resourcebroker)`. The independent consumer
registers pools, submits single- and multi-resource requests, grants a reservation,
activates an allocation, creates contention, observes a defer/reclamation decision,
releases, verifies accounting, prints an explanation, and finishes invariant checks
cleanly.

## Limitations

- Real host and CUDA resource allocation are validated where those proofs pass.
- GPU compute share is governed logical capacity unless physical partitioning is
  actually implemented; its provenance states policy/derived rather than measured.
- Bandwidth, storage, and network resource classes are policy/provider capacities and
  may be synthetic where physical sources are unavailable.
- Remote and multi-node resources are synthetic where unavailable.
- Resource Broker arbitrates resources; it does not replace admission, quota,
  scheduling, placement, transfer, or workload-preemption runtimes.
- Recovered physical allocations require revalidation.
- Unknown facts remain UNKNOWN.

## Build

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure

CUDA is optional and enabled with -DRESOURCEBROKER_ENABLE_CUDA_PROOF=ON when a CUDA
device is available. CUDA remains optional for ordinary consumers.

## License
Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.