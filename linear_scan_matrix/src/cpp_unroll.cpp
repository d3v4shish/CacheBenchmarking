// Diagnostic-only C++ scalar FP loops. Manual bodies preserve the exact order
// of one accumulator while changing only loop-control work. This binary is
// built with auto-vectorisation and compiler loop unrolling disabled.
#include "pmu_scope.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static volatile std::uint64_t sink;

template <typename T>
std::uint64_t finish_bits(T value) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(value));
  return bits;
}

template <typename T>
std::uint64_t unroll1(const std::vector<T>& values, std::uint64_t passes) {
  T sum = 0;
  for (std::uint64_t pass = 0; pass < passes; ++pass)
    for (std::size_t i = 0; i < values.size(); ++i) sum += values[i];
  return finish_bits(sum);
}

template <typename T>
std::uint64_t unroll2(const std::vector<T>& values, std::uint64_t passes) {
  T sum = 0;
  for (std::uint64_t pass = 0; pass < passes; ++pass) {
    std::size_t i = 0;
    for (; i + 2 <= values.size(); i += 2) { sum += values[i]; sum += values[i + 1]; }
    for (; i < values.size(); ++i) sum += values[i];
  }
  return finish_bits(sum);
}

template <typename T>
std::uint64_t unroll8(const std::vector<T>& values, std::uint64_t passes) {
  T sum = 0;
  for (std::uint64_t pass = 0; pass < passes; ++pass) {
    std::size_t i = 0;
    for (; i + 8 <= values.size(); i += 8) {
      sum += values[i]; sum += values[i + 1]; sum += values[i + 2]; sum += values[i + 3];
      sum += values[i + 4]; sum += values[i + 5]; sum += values[i + 6]; sum += values[i + 7];
    }
    for (; i < values.size(); ++i) sum += values[i];
  }
  return finish_bits(sum);
}

template <typename Scan>
void run(Scan scan, std::size_t bytes, std::uint64_t passes) {
  scan_pmu_scope scope{-1, {-1, -1}};
  scan_pmu_result pmu{};
  if (scan_pmu_open(&scope) || scan_pmu_start(&scope)) throw std::runtime_error("PMU start failed");
  const std::uint64_t begin = scan_clock_now_ns();
  const std::uint64_t checksum = scan();
  const std::uint64_t elapsed = scan_clock_now_ns() - begin;
  if (scan_pmu_stop(&scope, &pmu)) throw std::runtime_error("PMU stop failed");
  const bool pmu_available = scan_pmu_is_available(&scope) != 0;
  scan_pmu_close(&scope);
  sink = checksum;
  if (checksum != 0 || (pmu_available && (pmu.time_enabled == 0 || pmu.time_running != pmu.time_enabled)))
    throw std::runtime_error("invalid result");
  std::cout << "bytes=" << bytes << ",passes=" << passes << ",elapsed_ns=" << elapsed
            << ",checksum=" << checksum << ",expected_checksum=0,pmu_cycles=" << pmu.cycles
            << ",pmu_instructions=" << pmu.instructions << ",pmu_ref_cycles=" << pmu.ref_cycles
            << ",pmu_time_enabled=" << pmu.time_enabled << ",pmu_time_running=" << pmu.time_running
            << ",pmu_available=" << pmu_available << '\n';
}

int main(int argc, char** argv) {
  if (argc != 5) { std::cerr << "usage: cpp_unroll float|double u1|u2|u8 bytes passes\n"; return 64; }
  const std::string type = argv[1], body = argv[2];
  const std::size_t bytes = std::strtoull(argv[3], nullptr, 10);
  const std::uint64_t passes = std::strtoull(argv[4], nullptr, 10);
  if (!bytes || !passes || !((body == "u1") || (body == "u2") || (body == "u8"))) return 64;
  try {
    if (type == "float" && bytes % 4 == 0) {
      std::vector<float> values(bytes / 4);
      for (std::size_t i = 0; i < values.size(); ++i) values[i] = i & 1 ? 1.0f : -1.0f;
      if (body == "u1") run([&] { return unroll1(values, passes); }, bytes, passes);
      else if (body == "u2") run([&] { return unroll2(values, passes); }, bytes, passes);
      else run([&] { return unroll8(values, passes); }, bytes, passes);
    } else if (type == "double" && bytes % 8 == 0) {
      std::vector<double> values(bytes / 8);
      for (std::size_t i = 0; i < values.size(); ++i) values[i] = i & 1 ? 1.0 : -1.0;
      if (body == "u1") run([&] { return unroll1(values, passes); }, bytes, passes);
      else if (body == "u2") run([&] { return unroll2(values, passes); }, bytes, passes);
      else run([&] { return unroll8(values, passes); }, bytes, passes);
    } else {
      throw std::runtime_error("bad arguments");
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 65;
  }
  return 0;
}
