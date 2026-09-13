// C++ control for the strict 4-byte index cycle.  `next[index]` is both the
// data result and the address input of the following iteration; no SIMD stream
// or independent miss exists in this loop.
#include "pmu_scope.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
static volatile uint32_t sink;
static uint64_t splitmix64(uint64_t&s){s+=UINT64_C(0x9e3779b97f4a7c15);uint64_t x=s;x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9);x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb);return x^(x>>31);}
static void shuffle(std::vector<uint32_t>&v){uint64_t s=UINT64_C(20260909)^UINT64_C(0x12233445);for(size_t i=v.size();i>1;--i){size_t j=splitmix64(s)%i;std::swap(v[i-1],v[j]);}}
__attribute__((noinline)) static uint32_t chase(const uint32_t*next,uint32_t start,uint64_t operations){uint32_t index=start;for(uint64_t step=0;step<operations;++step)index=next[index];return index;}
int main(int argc,char**argv){if(argc!=3)throw std::runtime_error("usage: index_cpp bytes passes");size_t bytes=std::strtoull(argv[1],0,10);uint64_t passes=std::strtoull(argv[2],0,10);if(bytes<64||bytes%64||!passes)throw std::runtime_error("bytes must be a nonzero multiple of 64");size_t entries=bytes/4;std::vector<uint32_t> next(entries),order(entries);for(size_t i=0;i<entries;++i)order[i]=i;shuffle(order);for(size_t i=0;i<entries;++i)next[order[i]]=order[(i+1)%entries];uint32_t start=order[0],expected=start;uint64_t operations=entries*passes;scan_pmu_scope scope{};scan_pmu_result pmu{};if(scan_pmu_open(&scope)||scan_pmu_start(&scope))throw std::runtime_error("could not start PMU");uint64_t begun=scan_clock_now_ns(),checksum=chase(next.data(),start,operations),elapsed=scan_clock_now_ns()-begun;if(scan_pmu_stop(&scope,&pmu))throw std::runtime_error("could not read PMU");bool available=scan_pmu_is_available(&scope);scan_pmu_close(&scope);sink=checksum;if(checksum!=expected||!available||!pmu.time_enabled||pmu.time_enabled!=pmu.time_running)throw std::runtime_error("invalid checksum or PMU interval");std::cout<<"bytes="<<bytes<<",passes="<<passes<<",operations="<<operations<<",elapsed_ns="<<elapsed<<",checksum="<<checksum<<",expected_checksum="<<expected<<",pmu_cycles="<<pmu.cycles<<",pmu_instructions="<<pmu.instructions<<",pmu_ref_cycles="<<pmu.ref_cycles<<",pmu_time_enabled="<<pmu.time_enabled<<",pmu_time_running="<<pmu.time_running<<",pmu_available="<<available<<'\n';}
