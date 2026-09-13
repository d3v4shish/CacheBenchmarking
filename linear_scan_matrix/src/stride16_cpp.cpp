// C++ member of the executed stride-16 language matrix.  The vector owns the
// mapping before timing; only the two nested loops are inside the PMU scope.
#include "pmu_scope.h"

#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

static volatile uint32_t sink;

// Keep the vector ownership outside the hot function. Passing the raw view is
// still bounds-safe at construction time, but prevents container bookkeeping
// from becoming an accidental fifth instruction in the measured inner loop.
// +16 uint32_t is +64 bytes: one new Haswell cache line per useful load.
__attribute__((noinline)) static uint32_t stride16_sum(const uint32_t* values,
                                                        size_t words,
                                                        uint64_t passes) {
  uint32_t sum = 0;
  for (uint64_t pass = 0; pass < passes; ++pass) {
    for (size_t index = 0; index < words; index += 16) {
      sum += values[index];  // intentionally skip the remaining 60 line bytes
    }
  }
  return sum;
}

int main(int argc, char** argv) {
  if (argc != 3) throw std::runtime_error("usage: stride16_cpp bytes passes");
  const size_t bytes = std::strtoull(argv[1], nullptr, 10);
  const uint64_t passes = std::strtoull(argv[2], nullptr, 10);
  if (bytes < 64 || bytes % 64 || passes == 0)
    throw std::runtime_error("footprint must be a nonzero multiple of 64");

  std::vector<uint32_t> values(bytes / sizeof(uint32_t), UINT32_C(0x01010101));
  const uint64_t operations = (values.size() / 16) * passes;
  scan_pmu_scope pmu{};
  scan_pmu_result result{};
  if (scan_pmu_open(&pmu) || scan_pmu_start(&pmu))
    throw std::runtime_error("could not start in-scan PMU group");
  const uint64_t start_ns = scan_clock_now_ns();
  const uint32_t checksum = stride16_sum(values.data(), values.size(), passes);
  const uint64_t elapsed_ns = scan_clock_now_ns() - start_ns;
  if (scan_pmu_stop(&pmu, &result)) throw std::runtime_error("could not read PMU");
  const int pmu_available = scan_pmu_is_available(&pmu);
  scan_pmu_close(&pmu);

  const uint32_t expected = static_cast<uint32_t>(operations * UINT64_C(0x01010101));
  sink = checksum;
  if (checksum != expected || !pmu_available || result.time_enabled == 0 ||
      result.time_enabled != result.time_running)
    throw std::runtime_error("invalid checksum or multiplexed PMU interval");
  std::cout << "bytes=" << bytes << ",passes=" << passes
            << ",operations=" << operations << ",elapsed_ns=" << elapsed_ns
            << ",checksum=" << checksum << ",expected_checksum=" << expected
            << ",pmu_cycles=" << result.cycles
            << ",pmu_instructions=" << result.instructions
            << ",pmu_ref_cycles=" << result.ref_cycles
            << ",pmu_time_enabled=" << result.time_enabled
            << ",pmu_time_running=" << result.time_running
            << ",pmu_available=" << pmu_available << '\n';
}
