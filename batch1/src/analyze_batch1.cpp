/*
 * Batch 1 raw-result analyzer.
 *
 * Decision question
 * -----------------
 * Convert the DUT's unaggregated Batch 1 CSV into transparent summaries without
 * hiding a counter, migration, or calculation behind a spreadsheet formula.
 * This program does not rerun a benchmark and does not modify raw evidence.
 * It reads one CSV and writes derived CSV files into a new/selected directory.
 *
 * Plain-language model
 * --------------------
 * A median is the middle retained result after sorting; it is less fooled by
 * one interrupted sample than an average.  MAD is the middle distance from
 * that median.  A paired latency estimate compares a target row and its empty
 * loop from the same randomized round, then divides their net cycles by the
 * number of generated target instructions.  The raw rows remain authoritative.
 *
 * Important interpretation rules
 * ------------------------------
 * - Timing remains useful when expanded PMU events multiplex, but the affected
 *   counter values are not treated as exact port/uop counts.
 * - PMU cycle values are core cycles.  TSC ticks and reference cycles are kept
 *   as frequency cross-checks, not silently substituted for core cycles.
 * - The page-state rows are state-semantic checks; their page faults are a
 *   result, not a reason to discard them as noise.
 * - A CSV field that is empty means "event not requested", never numerical 0.
 *
 * Sources for the measurement terms used here
 * -------------------------------------------
 * [1] Agner Fog, instruction latency and reciprocal throughput:
 *     https://www.agner.org/optimize/instruction_tables.pdf
 * [2] Linux perf_event_open(2), time_enabled/time_running semantics:
 *     https://man7.org/linux/man-pages/man2/perf_event_open.2.html
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// Keeping names beside their exact CSV spelling makes a schema mismatch fail
// loudly instead of shifting every later column by one unnoticed position.
struct Row {
  std::string mode;
  std::string state;
  std::string perf_profile;
  int round = -1;
  uint64_t iterations = 0;
  uint64_t target_instructions_per_iteration = 0;
  uint64_t dynamic_target_instructions = 0;
  double tsc_ticks = 0;
  double wall_ns = 0;
  int cpu_before = -1;
  int cpu_after = -1;
  double minor_faults = 0;
  double major_faults = 0;
  std::optional<double> cycles;
  std::optional<double> cycles_enabled;
  std::optional<double> cycles_running;
  std::optional<double> instructions;
  std::optional<double> ref_cycles;
  std::optional<double> uops_issued;
  std::optional<double> uops_retired;
  std::optional<double> port0_active_cycles;
  std::optional<double> port1_active_cycles;
  std::optional<double> port5_active_cycles;
};

// A group describes one reproducible configuration.  Iteration count belongs
// in the key: the Batch 1 harness ladder deliberately runs the same mode at
// four lengths, and merging them would erase the very overhead trend we need.
struct GroupKey {
  std::string mode;
  std::string state;
  std::string profile;
  uint64_t iterations = 0;
  uint64_t target_instructions_per_iteration = 0;

  bool operator<(const GroupKey& other) const {
    return std::tie(mode, state, profile, iterations, target_instructions_per_iteration) <
           std::tie(other.mode, other.state, other.profile, other.iterations,
                    other.target_instructions_per_iteration);
  }
};

// All output is decimal CSV, so this simple splitter deliberately rejects
// quoted commas.  The runner writes no quoted fields; accepting a changed CSV
// dialect would make an analyst think it had been parsed when it had not.
std::vector<std::string> split_csv(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  for (char character : line) {
    if (character == ',') {
      fields.push_back(field);
      field.clear();
    } else {
      field.push_back(character);
    }
  }
  fields.push_back(field);
  return fields;
}

uint64_t parse_u64(const std::string& text, std::string_view name) {
  if (text.empty()) throw std::runtime_error("required numeric field is empty: " + std::string(name));
  size_t consumed = 0;
  const uint64_t value = std::stoull(text, &consumed);
  if (consumed != text.size()) throw std::runtime_error("bad integer in " + std::string(name));
  return value;
}

int parse_int(const std::string& text, std::string_view name) {
  if (text.empty()) throw std::runtime_error("required integer field is empty: " + std::string(name));
  size_t consumed = 0;
  const int value = std::stoi(text, &consumed);
  if (consumed != text.size()) throw std::runtime_error("bad integer in " + std::string(name));
  return value;
}

std::optional<double> parse_optional_double(const std::string& text, std::string_view name) {
  if (text.empty()) return std::nullopt;
  size_t consumed = 0;
  const double value = std::stod(text, &consumed);
  if (consumed != text.size()) throw std::runtime_error("bad number in " + std::string(name));
  return value;
}

// Accessing a named column gives readable errors when the harness changes.  It
// avoids magic numeric indexes whose meaning a beginner cannot safely audit.
const std::string& field(const std::vector<std::string>& fields,
                         const std::unordered_map<std::string, size_t>& header,
                         std::string_view name) {
  const auto found = header.find(std::string(name));
  if (found == header.end()) throw std::runtime_error("CSV header lacks " + std::string(name));
  if (found->second >= fields.size()) throw std::runtime_error("CSV row is shorter than header");
  return fields[found->second];
}

Row parse_row(const std::vector<std::string>& fields,
              const std::unordered_map<std::string, size_t>& header) {
  Row row;
  row.mode = field(fields, header, "mode");
  row.state = field(fields, header, "state");
  row.perf_profile = field(fields, header, "perf_profile");
  row.round = parse_int(field(fields, header, "round"), "round");
  row.iterations = parse_u64(field(fields, header, "iterations"), "iterations");
  row.target_instructions_per_iteration =
      parse_u64(field(fields, header, "target_instructions_per_iteration"),
                "target_instructions_per_iteration");
  row.dynamic_target_instructions =
      parse_u64(field(fields, header, "dynamic_target_instructions"),
                "dynamic_target_instructions");
  row.tsc_ticks = static_cast<double>(parse_u64(field(fields, header, "tsc_ticks"), "tsc_ticks"));
  row.wall_ns = static_cast<double>(parse_u64(field(fields, header, "wall_ns"), "wall_ns"));
  row.cpu_before = parse_int(field(fields, header, "cpu_before"), "cpu_before");
  row.cpu_after = parse_int(field(fields, header, "cpu_after"), "cpu_after");
  row.minor_faults = static_cast<double>(parse_u64(field(fields, header, "minor_faults"), "minor_faults"));
  row.major_faults = static_cast<double>(parse_u64(field(fields, header, "major_faults"), "major_faults"));
  row.cycles = parse_optional_double(field(fields, header, "cycles"), "cycles");
  row.cycles_enabled = parse_optional_double(field(fields, header, "cycles_enabled"), "cycles_enabled");
  row.cycles_running = parse_optional_double(field(fields, header, "cycles_running"), "cycles_running");
  row.instructions = parse_optional_double(field(fields, header, "instructions"), "instructions");
  row.ref_cycles = parse_optional_double(field(fields, header, "ref_cycles"), "ref_cycles");
  row.uops_issued = parse_optional_double(field(fields, header, "uops_issued_any"), "uops_issued_any");
  row.uops_retired = parse_optional_double(field(fields, header, "uops_retired_all"), "uops_retired_all");
  row.port0_active_cycles = parse_optional_double(field(fields, header, "port0_active_cycles"), "port0_active_cycles");
  row.port1_active_cycles = parse_optional_double(field(fields, header, "port1_active_cycles"), "port1_active_cycles");
  row.port5_active_cycles = parse_optional_double(field(fields, header, "port5_active_cycles"), "port5_active_cycles");
  return row;
}

std::vector<Row> read_rows(const std::filesystem::path& input) {
  std::ifstream file(input);
  if (!file) throw std::runtime_error("cannot open raw CSV: " + input.string());
  std::string line;
  if (!std::getline(file, line)) throw std::runtime_error("raw CSV is empty");
  const std::vector<std::string> names = split_csv(line);
  std::unordered_map<std::string, size_t> header;
  for (size_t index = 0; index < names.size(); ++index) {
    if (!header.emplace(names[index], index).second) {
      throw std::runtime_error("duplicate CSV header: " + names[index]);
    }
  }

  std::vector<Row> rows;
  size_t line_number = 1;
  while (std::getline(file, line)) {
    ++line_number;
    if (line.empty()) continue;
    try {
      rows.push_back(parse_row(split_csv(line), header));
    } catch (const std::exception& error) {
      throw std::runtime_error("CSV line " + std::to_string(line_number) + ": " + error.what());
    }
  }
  return rows;
}

// Nearest-rank percentiles are easy to explain: p05 is the first sorted value
// at or above 5% of the sample count.  With 15 repetitions this is deliberately
// coarse, so the article also links the full raw CSV rather than overclaiming.
double percentile(std::vector<double> values, double probability) {
  if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
  std::sort(values.begin(), values.end());
  const size_t rank = static_cast<size_t>(std::ceil(probability * values.size()));
  return values.at(std::max<size_t>(1, rank) - 1);
}

double median(const std::vector<double>& values) { return percentile(values, 0.5); }

double mad(const std::vector<double>& values) {
  if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
  const double center = median(values);
  std::vector<double> deviations;
  deviations.reserve(values.size());
  for (double value : values) deviations.push_back(std::abs(value - center));
  return median(deviations);
}

// The minimum running/enabled fraction is conservative: one multiplexed event
// is enough to make a supposedly simultaneous counter attribution unsafe.
double minimum_running_ratio(const Row& row) {
  std::vector<double> ratios;
  const auto add_ratio = [&ratios](const std::optional<double>& enabled,
                                    const std::optional<double>& running) {
    if (enabled && running && *enabled > 0) ratios.push_back(*running / *enabled);
  };
  add_ratio(row.cycles_enabled, row.cycles_running);
  // The remaining enabled/running columns exist in raw CSV but Batch 1 only
  // needs cycle-group validity here.  Expanded profile's visible cycle ratio
  // already demonstrates scheduling pressure; raw rows retain every event.
  if (ratios.empty()) return std::numeric_limits<double>::quiet_NaN();
  return *std::min_element(ratios.begin(), ratios.end());
}

bool timing_valid(const Row& row) {
  // CPU 4 was requested.  A migration is a hard rejection even if a timestamp
  // was successfully read because a per-core result no longer has one core.
  return row.cpu_before == 4 && row.cpu_after == 4;
}

bool cycle_counter_exact(const Row& row) {
  if (!row.cycles || !row.cycles_enabled || !row.cycles_running) return false;
  return *row.cycles_enabled == *row.cycles_running;
}

void write_number(std::ostream& output, double value) {
  if (std::isnan(value)) return;
  output << std::setprecision(12) << value;
}

std::string pair_key(std::string_view mode, std::string_view state, std::string_view profile,
                     uint64_t iterations, int round) {
  return std::string(mode) + "|" + std::string(state) + "|" + std::string(profile) + "|" +
         std::to_string(iterations) + "|" + std::to_string(round);
}

// Per-configuration summary.  Values with missing/unsafe counters stay blank;
// this makes absence/multiplexing visible in charts rather than turning it into
// a reassuring but false zero.
void write_group_summary(const std::filesystem::path& output_path,
                         const std::map<GroupKey, std::vector<const Row*>>& groups) {
  std::ofstream output(output_path);
  if (!output) throw std::runtime_error("cannot write " + output_path.string());
  output << "mode,state,perf_profile,iterations,target_instructions_per_iteration,rows,"
            "timing_valid_rows,exact_cycle_rows,median_tsc_ticks_per_iteration,"
            "p05_tsc_ticks_per_iteration,p95_tsc_ticks_per_iteration,mad_tsc_ticks_per_iteration,"
            "median_wall_ns,median_core_cycles_per_target_instruction,"
            "median_instructions_per_target_instruction,median_ref_cycles_per_iteration,"
            "median_core_to_ref_ratio,median_min_cycle_running_ratio,"
            "median_minor_faults,median_major_faults\n";

  for (const auto& [key, rows] : groups) {
    std::vector<double> tsc_per_iteration, wall_ns, cycles_per_target, instructions_per_target;
    std::vector<double> ref_per_iteration, core_to_ref, min_ratios, minor_faults, major_faults;
    int valid_rows = 0;
    int exact_cycles = 0;
    for (const Row* row : rows) {
      if (!timing_valid(*row)) continue;
      ++valid_rows;
      tsc_per_iteration.push_back(row->tsc_ticks / row->iterations);
      wall_ns.push_back(row->wall_ns);
      minor_faults.push_back(row->minor_faults);
      major_faults.push_back(row->major_faults);
      min_ratios.push_back(minimum_running_ratio(*row));
      if (cycle_counter_exact(*row)) {
        ++exact_cycles;
        if (row->dynamic_target_instructions != 0) {
          cycles_per_target.push_back(*row->cycles / row->dynamic_target_instructions);
          if (row->instructions) {
            instructions_per_target.push_back(*row->instructions / row->dynamic_target_instructions);
          }
        }
        if (row->ref_cycles) {
          ref_per_iteration.push_back(*row->ref_cycles / row->iterations);
          core_to_ref.push_back(*row->cycles / *row->ref_cycles);
        }
      }
    }
    output << key.mode << ',' << key.state << ',' << key.profile << ',' << key.iterations << ','
           << key.target_instructions_per_iteration << ',' << rows.size() << ',' << valid_rows << ','
           << exact_cycles << ',';
    write_number(output, median(tsc_per_iteration)); output << ',';
    write_number(output, percentile(tsc_per_iteration, 0.05)); output << ',';
    write_number(output, percentile(tsc_per_iteration, 0.95)); output << ',';
    write_number(output, mad(tsc_per_iteration)); output << ',';
    write_number(output, median(wall_ns)); output << ',';
    write_number(output, median(cycles_per_target)); output << ',';
    write_number(output, median(instructions_per_target)); output << ',';
    write_number(output, median(ref_per_iteration)); output << ',';
    write_number(output, median(core_to_ref)); output << ',';
    write_number(output, median(min_ratios)); output << ',';
    write_number(output, median(minor_faults)); output << ',';
    write_number(output, median(major_faults)); output << '\n';
  }
}

// Pair target and empty-loop rows by randomized round.  This does not make two
// separately scheduled samples literally simultaneous; it is a transparent,
// lower-bias subtraction whose per-round deltas remain available in output.
void write_pair_summary(const std::filesystem::path& output_path, const std::vector<Row>& rows) {
  std::map<std::string, const Row*> index;
  for (const Row& row : rows) {
    index[pair_key(row.mode, row.state, row.perf_profile, row.iterations, row.round)] = &row;
  }

  std::ofstream output(output_path);
  if (!output) throw std::runtime_error("cannot write " + output_path.string());
  output << "comparison,iterations,pairs,median_net_tsc_ticks_per_target_instruction,"
            "p05_net_tsc_ticks_per_target_instruction,p95_net_tsc_ticks_per_target_instruction,"
            "median_net_core_cycles_per_target_instruction,"
            "p05_net_core_cycles_per_target_instruction,p95_net_core_cycles_per_target_instruction\n";

  const std::vector<std::string> targets = {"add_latency", "imul_latency", "shift_latency"};
  for (const std::string& target : targets) {
    std::set<uint64_t> iterations_set;
    for (const Row& row : rows) {
      if (row.mode == target && row.state == "warm" && row.perf_profile == "basic") {
        iterations_set.insert(row.iterations);
      }
    }
    for (uint64_t iterations : iterations_set) {
      std::vector<double> net_tsc, net_cycles;
      for (int round = 0; round < 1000; ++round) {
        const auto target_it = index.find(pair_key(target, "warm", "basic", iterations, round));
        const auto empty_it = index.find(pair_key("empty", "warm", "basic", iterations, round));
        if (target_it == index.end() || empty_it == index.end()) continue;
        const Row& target_row = *target_it->second;
        const Row& empty_row = *empty_it->second;
        if (!timing_valid(target_row) || !timing_valid(empty_row) ||
            target_row.dynamic_target_instructions == 0) continue;
        net_tsc.push_back((target_row.tsc_ticks - empty_row.tsc_ticks) /
                          target_row.dynamic_target_instructions);
        if (cycle_counter_exact(target_row) && cycle_counter_exact(empty_row)) {
          net_cycles.push_back((*target_row.cycles - *empty_row.cycles) /
                               target_row.dynamic_target_instructions);
        }
      }
      output << target << "_minus_empty," << iterations << ',' << net_tsc.size() << ',';
      write_number(output, median(net_tsc)); output << ',';
      write_number(output, percentile(net_tsc, 0.05)); output << ',';
      write_number(output, percentile(net_tsc, 0.95)); output << ',';
      write_number(output, median(net_cycles)); output << ',';
      write_number(output, percentile(net_cycles, 0.05)); output << ',';
      write_number(output, percentile(net_cycles, 0.95)); output << '\n';
    }
  }

  // XOR and AND-zero have the same final result and same 128-triplet body.
  // Their difference is therefore presented as cycles per whole triplet, not
  // invented as a one-way latency of XOR or AND.
  std::vector<double> zero_tsc_delta, zero_cycles_delta;
  for (int round = 0; round < 1000; ++round) {
    const auto xor_it = index.find(pair_key("xor_break", "warm", "basic", 12000000, round));
    const auto and_it = index.find(pair_key("and_zero_control", "warm", "basic", 12000000, round));
    if (xor_it == index.end() || and_it == index.end()) continue;
    const Row& xor_row = *xor_it->second;
    const Row& and_row = *and_it->second;
    const double triplets = static_cast<double>(xor_row.iterations) * 128.0;
    zero_tsc_delta.push_back((and_row.tsc_ticks - xor_row.tsc_ticks) / triplets);
    if (cycle_counter_exact(xor_row) && cycle_counter_exact(and_row)) {
      zero_cycles_delta.push_back((*and_row.cycles - *xor_row.cycles) / triplets);
    }
  }
  output << "and_zero_control_minus_xor_break_per_triplet,12000000," << zero_tsc_delta.size() << ',';
  write_number(output, median(zero_tsc_delta)); output << ',';
  write_number(output, percentile(zero_tsc_delta, 0.05)); output << ',';
  write_number(output, percentile(zero_tsc_delta, 0.95)); output << ',';
  write_number(output, median(zero_cycles_delta)); output << ',';
  write_number(output, percentile(zero_cycles_delta, 0.05)); output << ',';
  write_number(output, percentile(zero_cycles_delta, 0.95)); output << '\n';
}

void write_quality_summary(const std::filesystem::path& output_path, const std::vector<Row>& rows) {
  int migrated = 0, basic_multiplexed = 0, expanded_multiplexed = 0, fault_rows = 0;
  double total_minor_faults = 0;
  std::vector<double> expanded_cycle_ratios;
  for (const Row& row : rows) {
    if (!timing_valid(row)) ++migrated;
    if (row.perf_profile == "basic" && row.cycles_enabled && row.cycles_running &&
        *row.cycles_enabled != *row.cycles_running) ++basic_multiplexed;
    if (row.perf_profile == "expanded" && row.cycles_enabled && row.cycles_running &&
        *row.cycles_enabled != *row.cycles_running) {
      ++expanded_multiplexed;
      expanded_cycle_ratios.push_back(*row.cycles_running / *row.cycles_enabled);
    }
    if (row.minor_faults != 0 || row.major_faults != 0) ++fault_rows;
    total_minor_faults += row.minor_faults;
  }
  std::ofstream output(output_path);
  if (!output) throw std::runtime_error("cannot write " + output_path.string());
  output << "metric,value\n";
  output << "raw_rows," << rows.size() << '\n';
  output << "migration_rows," << migrated << '\n';
  output << "basic_cycle_multiplexed_rows," << basic_multiplexed << '\n';
  output << "expanded_cycle_multiplexed_rows," << expanded_multiplexed << '\n';
  output << "expanded_cycle_running_enabled_median,";
  write_number(output, median(expanded_cycle_ratios)); output << '\n';
  output << "fault_rows," << fault_rows << '\n';
  output << "total_minor_faults," << total_minor_faults << '\n';
}

int run(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: analyze_batch1 RAW_SAMPLES_CSV OUTPUT_DIRECTORY\n";
    return 64;
  }
  const std::filesystem::path raw_path(argv[1]);
  const std::filesystem::path output_dir(argv[2]);
  if (!std::filesystem::exists(raw_path)) throw std::runtime_error("raw CSV does not exist");
  std::filesystem::create_directories(output_dir);

  const std::vector<Row> rows = read_rows(raw_path);
  if (rows.empty()) throw std::runtime_error("raw CSV has no data rows");

  std::map<GroupKey, std::vector<const Row*>> groups;
  for (const Row& row : rows) {
    groups[{row.mode, row.state, row.perf_profile, row.iterations,
            row.target_instructions_per_iteration}].push_back(&row);
  }
  write_group_summary(output_dir / "group_summary.csv", groups);
  write_pair_summary(output_dir / "paired_summary.csv", rows);
  write_quality_summary(output_dir / "quality_summary.csv", rows);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "analyze_batch1: " << error.what() << '\n';
    return 1;
  }
}
