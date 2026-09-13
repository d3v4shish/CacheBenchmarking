// Native C++ scalar v2 variant. std::vector owns the mapping, but allocation
// and fill are outside the timed/PMU region. It is intentionally separate from
// the shared C-compatible source used to compare C and C++ back ends.
#include "pmu_scope.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct String16 { std::uint8_t bytes[16]; };
static volatile std::uint64_t sink;

struct Measurement {
  std::uint64_t checksum;
  std::uint64_t elapsed_ns;
  scan_pmu_result pmu;
  bool pmu_available;
};

template <typename Scan>
Measurement measure(Scan scan) {
  scan_pmu_scope scope{-1, {-1, -1}};
  scan_pmu_result result{};
  if (scan_pmu_open(&scope) != 0) throw std::runtime_error("scan_pmu_open failed");
  if (scan_pmu_start(&scope) != 0) {
    scan_pmu_close(&scope);
    throw std::runtime_error("scan_pmu_start failed");
  }
  const std::uint64_t begin = scan_clock_now_ns();
  const std::uint64_t checksum = scan();
  const std::uint64_t elapsed_ns = scan_clock_now_ns() - begin;
  if (scan_pmu_stop(&scope, &result) != 0) {
    scan_pmu_close(&scope);
    throw std::runtime_error("scan_pmu_stop failed");
  }
  const bool pmu_available = scan_pmu_is_available(&scope) != 0;
  scan_pmu_close(&scope);
  return {checksum, elapsed_ns, result, pmu_available};
}

template <typename T>
std::uint64_t scalar_unsigned(const std::vector<T>& values, std::uint64_t passes) {
  T sum = 0;
  for (std::uint64_t pass = 0; pass < passes; ++pass)
    for (T value : values) sum = static_cast<T>(sum + value);
  return sum;
}

std::uint64_t scalar_float(const std::vector<float>& values, std::uint64_t passes) {
  float sum = 0.0f;
  for (std::uint64_t pass = 0; pass < passes; ++pass)
    for (float value : values) sum += value;
  std::uint32_t bits;
  std::memcpy(&bits, &sum, sizeof(bits));
  return bits;
}

std::uint64_t scalar_double(const std::vector<double>& values, std::uint64_t passes) {
  double sum = 0.0;
  for (std::uint64_t pass = 0; pass < passes; ++pass)
    for (double value : values) sum += value;
  std::uint64_t bits;
  std::memcpy(&bits, &sum, sizeof(bits));
  return bits;
}

std::uint64_t scalar_string16(const std::vector<String16>& values,
                              std::uint64_t passes) {
  std::uint64_t sum = 0;
  for (std::uint64_t pass = 0; pass < passes; ++pass)
    for (const auto& record : values)
      for (std::uint8_t byte : record.bytes) sum += byte;
  return sum;
}

std::uint64_t expected_checksum(const std::string& type, std::uint64_t count,
                                std::uint64_t passes) {
  const std::uint64_t operations = count * passes;
  if (type == "short") return static_cast<std::uint16_t>(operations * 0x0101);
  if (type == "int") return static_cast<std::uint32_t>(operations * 0x01010101);
  if (type == "float" || type == "double") return 0;
  if (type == "string16") return operations * 16;
  throw std::runtime_error("unknown type");
}

int main(int argc, char** argv) {
  if (argc != 5 || std::string(argv[2]) != "scalar") {
    std::cerr << "usage: cpp_native type scalar bytes passes\n";
    return 64;
  }
  const std::string type = argv[1];
  const std::size_t bytes = std::strtoull(argv[3], nullptr, 10);
  const std::uint64_t passes = std::strtoull(argv[4], nullptr, 10);
  if (!bytes || !passes) return 64;

  try {
    Measurement measured{};
    std::uint64_t expected = 0;
    if (type == "short" && bytes % 2 == 0) {
      std::vector<std::uint16_t> values(bytes / 2, 0x0101);
      expected = expected_checksum(type, values.size(), passes);
      measured = measure([&] { return scalar_unsigned(values, passes); });
    } else if (type == "int" && bytes % 4 == 0) {
      std::vector<std::uint32_t> values(bytes / 4, 0x01010101);
      expected = expected_checksum(type, values.size(), passes);
      measured = measure([&] { return scalar_unsigned(values, passes); });
    } else if (type == "float" && bytes % 4 == 0) {
      std::vector<float> values(bytes / 4);
      for (std::size_t i = 0; i < values.size(); ++i) values[i] = i & 1 ? 1.0f : -1.0f;
      expected = expected_checksum(type, values.size(), passes);
      measured = measure([&] { return scalar_float(values, passes); });
    } else if (type == "double" && bytes % 8 == 0) {
      std::vector<double> values(bytes / 8);
      for (std::size_t i = 0; i < values.size(); ++i) values[i] = i & 1 ? 1.0 : -1.0;
      expected = expected_checksum(type, values.size(), passes);
      measured = measure([&] { return scalar_double(values, passes); });
    } else if (type == "string16" && bytes % 16 == 0) {
      std::vector<String16> values(bytes / 16, String16{{1, 1, 1, 1, 1, 1, 1, 1,
                                                          1, 1, 1, 1, 1, 1, 1, 1}});
      expected = expected_checksum(type, values.size(), passes);
      measured = measure([&] { return scalar_string16(values, passes); });
    } else {
      throw std::runtime_error("bad arguments");
    }
    sink = measured.checksum;
    if (measured.checksum != expected ||
        (measured.pmu_available && (measured.pmu.time_enabled == 0 ||
         measured.pmu.time_running != measured.pmu.time_enabled)))
      throw std::runtime_error("invalid checksum or multiplexed PMU group");
    std::cout << "bytes=" << bytes << ",passes=" << passes
              << ",elapsed_ns=" << measured.elapsed_ns
              << ",checksum=" << measured.checksum
              << ",expected_checksum=" << expected
              << ",pmu_cycles=" << measured.pmu.cycles
              << ",pmu_instructions=" << measured.pmu.instructions
              << ",pmu_ref_cycles=" << measured.pmu.ref_cycles
              << ",pmu_time_enabled=" << measured.pmu.time_enabled
              << ",pmu_time_running=" << measured.pmu.time_running
              << ",pmu_available=" << measured.pmu_available << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 65;
  }
  return 0;
}
