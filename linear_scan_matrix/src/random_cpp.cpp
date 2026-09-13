// C++ member of the random-direct-load language control.  Its timed loop is
// intentionally plain: read the next prebuilt random index, then load one
// uint32_t payload through it.  The vectors' allocation, fill and shuffle are
// outside the PMU scope so only the access shape is compared.
#include "pmu_scope.h"
#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
static volatile uint32_t sink;
static uint64_t splitmix64(uint64_t& state) { state += UINT64_C(0x9e3779b97f4a7c15); uint64_t x=state; x=(x^(x>>30))*UINT64_C(0xbf58476d1ce4e5b9); x=(x^(x>>27))*UINT64_C(0x94d049bb133111eb); return x^(x>>31); }
static void shuffle(std::vector<uint32_t>& q) { uint64_t state=UINT64_C(20260909)^UINT64_C(0x5eed5eed); for (size_t i=q.size(); i>1; --i) { size_t j=static_cast<size_t>(splitmix64(state)%i); std::swap(q[i-1],q[j]); } }
__attribute__((noinline)) static uint32_t random_sum(const uint32_t* values, const uint32_t* queries, size_t words, uint64_t passes) {
  uint32_t sum=0;
  for (uint64_t pass=0; pass<passes; ++pass)
    for (size_t i=0; i<words; ++i)
      sum += values[queries[i]]; // query load -> address generation -> unpredictable payload load
  return sum;
}
int main(int argc, char** argv) {
  if (argc!=3) throw std::runtime_error("usage: random_cpp footprint_bytes passes");
  const size_t bytes=std::strtoull(argv[1],nullptr,10); const uint64_t passes=std::strtoull(argv[2],nullptr,10);
  if (bytes<64 || bytes%64 || !passes) throw std::runtime_error("footprint must be a nonzero multiple of 64");
  const size_t words=bytes/4; std::vector<uint32_t> values(words), queries(words); uint64_t state=UINT64_C(20260909); uint32_t one_pass=0;
  for (size_t i=0;i<words;++i) { values[i]=static_cast<uint32_t>(splitmix64(state)); queries[i]=static_cast<uint32_t>(i); one_pass += values[i]; }
  shuffle(queries); const uint64_t operations=static_cast<uint64_t>(words)*passes;
  scan_pmu_scope scope{}; scan_pmu_result pmu{}; if (scan_pmu_open(&scope)||scan_pmu_start(&scope)) throw std::runtime_error("could not start PMU");
  const uint64_t start=scan_clock_now_ns(); const uint32_t checksum=random_sum(values.data(),queries.data(),words,passes); const uint64_t elapsed=scan_clock_now_ns()-start;
  if (scan_pmu_stop(&scope,&pmu)) throw std::runtime_error("could not read PMU");
  const bool available=scan_pmu_is_available(&scope);
  scan_pmu_close(&scope);
  const uint32_t expected=static_cast<uint32_t>(one_pass*passes); sink=checksum;
  if (checksum!=expected || !available || !pmu.time_enabled || pmu.time_enabled!=pmu.time_running) throw std::runtime_error("invalid checksum or multiplexed PMU interval");
  std::cout << "bytes="<<bytes<<",passes="<<passes<<",operations="<<operations<<",elapsed_ns="<<elapsed<<",checksum="<<checksum<<",expected_checksum="<<expected<<",pmu_cycles="<<pmu.cycles<<",pmu_instructions="<<pmu.instructions<<",pmu_ref_cycles="<<pmu.ref_cycles<<",pmu_time_enabled="<<pmu.time_enabled<<",pmu_time_running="<<pmu.time_running<<",pmu_available="<<available<<'\n';
}
