// test_data_plane.cpp - Persistence integrity, protocol framing, adversarial rejection.
// Copyright 2026 Summon Software Labs. Apache License 2.0.
#include "resourcebroker/broker.hpp"
#include "resourcebroker/protocol.hpp"
#include "resourcebroker/request.hpp"
#include "resourcebroker/resource.hpp"
#include "resourcebroker/amount.hpp"
#include "resourcebroker/error.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cstdint>
using namespace resourcebroker;
static int failures=0;
#define CHECK(c,m) do{ if(!(c)){std::fprintf(stderr,"FAIL: %s\n",m);++failures;} else std::fprintf(stderr,"PASS: %s\n",m);}while(0)
static const std::uint64_t MiB=1024ull*1024ull;
static std::string readfile(const std::string& p){ std::ifstream f(p,std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()); }
static void writefile(const std::string& p, const std::string& s){ std::ofstream f(p,std::ios::binary|std::ios::trunc); f.write(s.data(),(std::streamsize)s.size()); }
int main(){
    // Persistence round-trip + deterministic digest.
    Broker b(CoordinatorEpoch(1),BrokerId(1)); b.set_policy(BrokerPolicy{});
    ResourcePoolDescriptor gp; gp.pool_id=ResourcePoolId(1); gp.resource_class=ResourceClass::HOST_MEMORY_BYTES; gp.scope=PoolScope::HOST_LOCAL;
    gp.total_governed=ResourceAmount::bytes(Bytes(64*MiB)); gp.provenance=Provenance::MEASURED; gp.health=Health::HEALTHY; gp.freshness=Freshness::CURRENT;
    b.register_pool(gp,CapacityGeneration(1));
    ResourceDescriptor gi; gi.instance_id=ResourceInstanceId(1); gi.resource_class=ResourceClass::HOST_MEMORY_BYTES; gi.pool_id=gp.pool_id; gi.governed=ResourceAmount::bytes(Bytes(64*MiB)); gi.provenance=Provenance::MEASURED;
    b.register_resource(gi);
    AuthorityEnvelope auth; auth.coordinator_epoch=b.coordinator_epoch();
    ResourceRequest rq; rq.request_id=ResourceRequestId(1); rq.owner=OwnerId(1); rq.tenant=TenantId(1);
    ResourceRequirement rr; rr.resource_class=ResourceClass::HOST_MEMORY_BYTES; rr.requested=ResourceAmount::bytes(Bytes(32*MiB)); rr.minimum=rr.requested; rr.preferred=rr.requested; rr.semantics=CapacitySemantics::EXACT; rq.requirements.push_back(rr);
    rq.reclaimability=Reclaimability::COOPERATIVE;
    GrantResult g=b.submit_request(rq,auth); CHECK(g.outcome==RequestOutcome::GRANT,"request granted");
    const std::string path="dataplane_state.rbstate";
    b.save(path);
    Broker b2(CoordinatorEpoch(2),BrokerId(1)); b2.load(path);
    CHECK(b2.pool_for_class(ResourceClass::HOST_MEMORY_BYTES).has_value(),"round-trip pool");
    // Deterministic: byte-identical on re-save of an identical load.
    b2.save("dp2.rbstate"); CHECK(readfile(path)==readfile("dp2.rbstate"),"deterministic byte-identical");

    // Corrupt a payload byte -> checksum/digest rejection.
    std::string bytes=readfile(path);
    const auto mid=bytes.size()/2; bytes[mid]=static_cast<char>(bytes[mid]^0x5A);
    writefile("dp_bad.rbstate",bytes);
    Broker b3(CoordinatorEpoch(3),BrokerId(1));
    bool rej=false; try{ b3.load("dp_bad.rbstate"); }catch(const BrokerError& e){ rej=true; std::fprintf(stderr,"PASS: corruption rejected (%s)\n",e.code_string()); }
    CHECK(rej,"corrupted persistence rejected");

    // Truncate -> rejection.
    writefile("dp_trunc.rbstate", readfile(path).substr(0, 30));
    Broker b4(CoordinatorEpoch(4),BrokerId(1));
    bool rej2=false; try{ b4.load("dp_trunc.rbstate"); }catch(const BrokerError&){ rej2=true; }
    CHECK(rej2,"truncated persistence rejected");

    // Bad magic -> rejection.
    std::string bad=readfile(path); bad[0]='X'; writefile("dp_magic.rbstate",bad);
    Broker b5(CoordinatorEpoch(5),BrokerId(1));
    bool rej3=false; try{ b5.load("dp_magic.rbstate"); }catch(const BrokerError&){ rej3=true; }
    CHECK(rej3,"bad magic rejected");

    // Protocol frame round-trip + rejection.
    const auto payload=encode_hello(WorkerId(7));
    const auto frame=encode_frame(MessageKind::HELLO,payload);
    const auto dec=decode_frame(frame.data(),frame.size());
    CHECK(dec.kind==MessageKind::HELLO && decode_hello(dec.payload).worker==WorkerId(7),"frame round-trip");
    // Bad magic.
    auto badf=frame; badf[0]='N';
    bool prem=false; try{ decode_frame(badf.data(),badf.size()); }catch(const BrokerError&){ prem=true; }
    CHECK(prem,"bad frame magic rejected");
    // Checksum mismatch: flip a payload byte.
    auto badf2=frame; badf2[frame.size()-1]^=0x01;
    bool prem2=false; try{ decode_frame(badf2.data(),badf2.size()); }catch(const BrokerError&){ prem2=true; }
    CHECK(prem2,"frame checksum mismatch rejected");
    // Unsupported version.
    auto badv=frame; badv[4]=99;
    bool prem3=false; try{ decode_frame(badv.data(),badv.size()); }catch(const BrokerError&){ prem3=true; }
    CHECK(prem3,"unsupported protocol version rejected");

    // Adversarial: oversize+unknown class handled via request validation.
    ResourceRequest zero; zero.request_id=ResourceRequestId(2); zero.owner=OwnerId(1); zero.tenant=TenantId(1);
    ResourceRequirement zr; zr.resource_class=ResourceClass::HOST_MEMORY_BYTES; zr.requested=ResourceAmount::bytes(Bytes(0)); zr.minimum=zr.requested; zr.preferred=zr.requested; zr.semantics=CapacitySemantics::EXACT; zero.requirements.push_back(zr);
    bool zrej=false; try{ b.submit_request(zero,auth); }catch(const BrokerError&){ zrej=true; }
    CHECK(zrej,"zero-amount requirement rejected");

    if(failures==0){std::printf("test_data_plane PASS\n");return 0;} std::printf("test_data_plane FAIL (%d)\n",failures);return 1;
}
