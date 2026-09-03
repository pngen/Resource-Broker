// 01_resource_identity.cpp - Strong identities and units.
#include "resourcebroker/identities.hpp"
#include "resourcebroker/units.hpp"
#include <cstdio>
using namespace resourcebroker;
int main(){ BrokerId b(7); Bytes g(4ull*1024*1024*1024); ComputeShare s=ComputeShare::from_fraction(0.75);
 std::printf("broker=%llu gpu=%s share=%f\n",(unsigned long long)b.value(),g.to_string().c_str(),s.fraction()); return 0; }
