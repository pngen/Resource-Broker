// test_identity_units.cpp - Identity, unit, amount, and capacity invariants.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/identities.hpp"
#include "resourcebroker/units.hpp"
#include "resourcebroker/amount.hpp"
#include "resourcebroker/capacity.hpp"
#include "resourcebroker/error.hpp"
#include <cstdio>
using namespace resourcebroker;
static int failures = 0;
#define CHECK(c,m) do { if(!(c)){std::fprintf(stderr,"FAIL: %s\n",m);++failures;} else std::fprintf(stderr,"PASS: %s\n",m);} while(0)
static const std::uint64_t GiB=1024ull*1024ull*1024ull;
int main(){
    // Identity non-interchangeability is enforced by the type system; verify values & ordering.
    BrokerId a(5); BrokerId b(3);
    CHECK(a.is_valid() && b.is_valid(), "ids valid");
    CHECK(b < a, "id ordering");
    ResourcePoolId p(5);
    CHECK(a.value()==p.value(), "distinct types share raw value but are non-interchangeable");
    CHECK(++b == BrokerId(4), "increment");
    // Generations explicitly comparable.
    CoordinatorEpoch e1(10); CoordinatorEpoch e2(20);
    CHECK(e1 < e2, "generation ordering");
    // Units.
    Bytes g(4*GiB), pin(512*1024*1024ull);
    CHECK(g > pin, "bytes compare");
    CHECK((g+pin) == Bytes(4*GiB+512ull*1024*1024), "bytes add");
    ComputeShare c1 = ComputeShare::from_fraction(0.5);
    ComputeShare half = ComputeShare::from_fraction(0.5);
    CHECK(c1 == half, "compute share fixed point");
    CHECK(ComputeShare::whole().value()==1000000, "compute share unit count");
    CHECK(Count(5) > Count(3), "count compare");
    // Amount dimension discipline.
    ResourceAmount ab = ResourceAmount::bytes(Bytes(1024));
    CHECK(ab.dimension()==Dimension::Bytes, "amount dimension bytes");
    try { ab.as_count(); std::fprintf(stderr,"FAIL: no throw\n"); ++failures; } catch (...) { std::fprintf(stderr,"PASS: mismatched accessor throws\n"); }
    try { ResourceAmount x = ab + ResourceAmount::count(Count(1)); std::fprintf(stderr,"FAIL: dim add\n"); ++failures; } catch (...) { std::fprintf(stderr,"PASS: dimension add throws\n"); }
    // Capacity ledger invariant + transitions.
    CapacityLedger led(ResourceClass::GPU_MEMORY_BYTES, ResourceAmount::bytes(Bytes(8*GiB)), Provenance::MEASURED);
    ResourceAmount r4 = ResourceAmount::bytes(Bytes(4*GiB));
    CHECK(led.can_reserve(r4), "can reserve 4 GiB");
    led.reserve(r4);
    CHECK(led.reserved().as_bytes().value()==4*GiB, "reserved 4 GiB");
    led.allocate(r4);
    CHECK(led.allocated().as_bytes().value()==4*GiB && led.reserved().is_zero(), "allocate moves reserved->allocated");
    led.release_allocation_to_reserved(r4);
    CHECK(led.allocated().is_zero() && led.reserved().as_bytes().value()==4*GiB, "release allocation returns to reserved");
    led.release_reserved(r4);
    CHECK(led.free().as_bytes().value()==8*GiB, "free returns to baseline");
    // Non-negative & invariant on bad release.
    try { led.release_reserved(ResourceAmount::bytes(Bytes(1*GiB))); std::fprintf(stderr,"FAIL: underflow\n"); ++failures; } catch (...) { std::fprintf(stderr,"PASS: over-release throws\n"); }
    // restore_state invariant check.
    try { led.restore_state(ResourceAmount::bytes(Bytes(8*GiB)),ResourceAmount::bytes(Bytes(8*GiB)),ResourceAmount::bytes(Bytes(8*GiB)),ResourceAmount::bytes(Bytes(8*GiB)),ResourceAmount::bytes(Bytes(9*GiB)),ResourceAmount::bytes(Bytes(0)),ResourceAmount::bytes(Bytes(0)),ResourceAmount::bytes(Bytes(0))); std::fprintf(stderr,"FAIL: restore\n"); ++failures; } catch (...) { std::fprintf(stderr,"PASS: restore_state rejects invariant violation\n"); }
    if(failures==0){std::printf("test_identity_units PASS\n");return 0;} std::printf("test_identity_units FAIL (%d)\n",failures);return 1;
}
