/*
 * Cache Search Lab v2 — working-set-ladder analyser
 *
 * Decision question
 * -----------------
 * Convert retained, unaggregated working-set samples into auditable summary
 * files without silently accepting short, migrated, multiplexed, or page-fault
 * rows as normal cache evidence.  This program reads the CSV emitted by the
 * working_set_ladder benchmark; it does not fabricate missing counters.
 *
 * Plain-language model
 * --------------------
 * A result point is a group of repeated samples having the same build, access
 * mode, memory state, footprint, and PMU profile.  We report each group's
 * median and p05/p95.  A normal row is accepted only when it ran on the
 * requested CPU, met the minimum duration, had no faults, and—when it has
 * counters—each event had exact time_enabled/time_running for its enabled
 * interval. Page-cold
 * is intentionally a separate first-touch state and is never mixed into warm
 * cache summaries.
 *
 * Method sources
 *  [1] https://man7.org/linux/man-pages/man2/perf_event_open.2.html
 *  [2] https://www.agner.org/optimize/microarchitecture.pdf
 *
 * Usage
 * -----
 * analyze_working_set samples.csv counters.csv output-directory 250
 *
 * Output files
 *  summary.csv          timing distributions and acceptance counts
 *  counter_summary.csv  one row per accepted configuration/event
 *  quality_summary.csv  explicit rejection reasons
 *  visual_data.csv      summary-shaped input for the static article companion
 */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Sample {
  std::string sample_id;
  std::string build_id;
  std::string mode;
  std::string implementation;
  std::string state;
  std::string profile;
  uint64_t footprint_bytes = 0;
  uint64_t logical_operations = 0;
  uint64_t wall_ns = 0;
  uint64_t tsc_ticks = 0;
  int cpu_before = -1;
  int cpu_after = -1;
  long minor_faults = 0;
  long major_faults = 0;
  bool first_touch_exception = false;
};

struct Counter {
  std::string sample_id;
  std::string event;
  uint64_t value = 0;
  uint64_t enabled = 0;
  uint64_t running = 0;
};

struct Key {
  std::string build_id;
  std::string mode;
  std::string implementation;
  std::string state;
  std::string profile;
  uint64_t footprint_bytes = 0;

  bool operator<(const Key& other) const {
    return std::tie(build_id, mode, implementation, state, profile, footprint_bytes) <
           std::tie(other.build_id, other.mode, other.implementation, other.state, other.profile,
                    other.footprint_bytes);
  }
};

std::vector<std::string> split_csv(const std::string& line) {
  std::vector<std::string> cells;
  size_t begin = 0;
  while (begin <= line.size()) {
    const size_t comma = line.find(',', begin);
    cells.push_back(line.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin));
    if (comma == std::string::npos) break;
    begin = comma + 1;
  }
  return cells;
}

uint64_t as_u64(std::string_view value, std::string_view field) {
  try {
    size_t used = 0;
    const uint64_t result = std::stoull(std::string(value), &used);
    if (used != value.size()) throw std::invalid_argument("trailing");
    return result;
  } catch (const std::exception&) {
    throw std::runtime_error("invalid unsigned value for " + std::string(field));
  }
}

long as_long(std::string_view value, std::string_view field) {
  try {
    size_t used = 0;
    const long result = std::stol(std::string(value), &used);
    if (used != value.size()) throw std::invalid_argument("trailing");
    return result;
  } catch (const std::exception&) {
    throw std::runtime_error("invalid integer value for " + std::string(field));
  }
}

const std::string& field(const std::vector<std::string>& cells,
                         const std::unordered_map<std::string, size_t>& header,
                         std::string_view name) {
  const auto it = header.find(std::string(name));
  if (it == header.end() || it->second >= cells.size()) throw std::runtime_error("missing field: " + std::string(name));
  return cells[it->second];
}

std::unordered_map<std::string, size_t> read_header(std::ifstream& input) {
  std::string line;
  if (!std::getline(input, line)) throw std::runtime_error("CSV is empty");
  const auto names = split_csv(line);
  std::unordered_map<std::string, size_t> header;
  for (size_t index = 0; index < names.size(); ++index) {
    if (!header.emplace(names[index], index).second) throw std::runtime_error("duplicate CSV header: " + names[index]);
  }
  return header;
}

std::vector<Sample> read_samples(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read samples CSV: " + path.string());
  const auto header = read_header(input);
  std::vector<Sample> rows;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    const auto cells = split_csv(line);
    Sample row;
    row.sample_id = field(cells, header, "sample_id");
    row.build_id = field(cells, header, "build_id");
    row.mode = field(cells, header, "mode");
    row.implementation = field(cells, header, "implementation");
    row.state = field(cells, header, "state");
    row.profile = field(cells, header, "perf_profile");
    row.footprint_bytes = as_u64(field(cells, header, "footprint_bytes"), "footprint_bytes");
    row.logical_operations = as_u64(field(cells, header, "logical_operations"), "logical_operations");
    row.wall_ns = as_u64(field(cells, header, "wall_ns"), "wall_ns");
    row.tsc_ticks = as_u64(field(cells, header, "tsc_ticks"), "tsc_ticks");
    row.cpu_before = static_cast<int>(as_long(field(cells, header, "cpu_before"), "cpu_before"));
    row.cpu_after = static_cast<int>(as_long(field(cells, header, "cpu_after"), "cpu_after"));
    row.minor_faults = as_long(field(cells, header, "minor_faults"), "minor_faults");
    row.major_faults = as_long(field(cells, header, "major_faults"), "major_faults");
    row.first_touch_exception = as_u64(field(cells, header, "first_touch_exception"), "first_touch_exception") != 0;
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<Counter> read_counters(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot read counters CSV: " + path.string());
  const auto header = read_header(input);
  std::vector<Counter> rows;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    const auto cells = split_csv(line);
    rows.push_back({field(cells, header, "sample_id"), field(cells, header, "event"),
                    as_u64(field(cells, header, "value"), "value"),
                    as_u64(field(cells, header, "time_enabled"), "time_enabled"),
                    as_u64(field(cells, header, "time_running"), "time_running")});
  }
  return rows;
}

double percentile(std::vector<double> values, double probability) {
  if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
  std::sort(values.begin(), values.end());
  const size_t rank = static_cast<size_t>(std::ceil(probability * values.size()));
  return values.at(std::max<size_t>(1, rank) - 1);
}

void number(std::ostream& output, double value) {
  if (!std::isnan(value)) output << std::setprecision(12) << value;
}

Key key_for(const Sample& sample) {
  return {sample.build_id, sample.mode, sample.implementation, sample.state, sample.profile, sample.footprint_bytes};
}

bool has_exact_counters(const Sample& sample, const std::unordered_map<std::string, std::vector<Counter>>& counters) {
  if (sample.profile == "none") return true;
  const auto it = counters.find(sample.sample_id);
  if (it == counters.end() || it->second.empty()) return false;
  for (const Counter& counter : it->second) {
    if (counter.enabled == 0 || counter.enabled != counter.running) return false;
  }
  return true;
}

std::string rejection_reason(const Sample& sample, const std::unordered_map<std::string, std::vector<Counter>>& counters,
                             uint64_t minimum_duration_ns) {
  if (sample.cpu_before != sample.cpu_after) return "migration";
  if (!sample.first_touch_exception && sample.wall_ns < minimum_duration_ns) return "short_sample";
  if (!sample.first_touch_exception && (sample.minor_faults != 0 || sample.major_faults != 0)) return "timed_page_fault";
  if (!has_exact_counters(sample, counters)) return "counter_missing_or_multiplexed";
  return "";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 5) {
      std::cerr << "usage: analyze_working_set samples.csv counters.csv output-directory minimum-duration-ms\n";
      return 64;
    }
    const uint64_t minimum_duration_ns = as_u64(argv[4], "minimum duration") * 1'000'000ULL;
    const auto samples = read_samples(argv[1]);
    const auto counter_rows = read_counters(argv[2]);
    const std::filesystem::path output_dir(argv[3]);
    std::filesystem::create_directories(output_dir);

    std::unordered_map<std::string, std::vector<Counter>> counters_by_sample;
    for (const Counter& counter : counter_rows) counters_by_sample[counter.sample_id].push_back(counter);

    std::map<Key, std::vector<const Sample*>> accepted;
    std::map<Key, std::map<std::string, int>> rejected;
    for (const Sample& sample : samples) {
      const std::string reason = rejection_reason(sample, counters_by_sample, minimum_duration_ns);
      if (reason.empty()) accepted[key_for(sample)].push_back(&sample);
      else ++rejected[key_for(sample)][reason];
    }

    std::ofstream summary(output_dir / "summary.csv");
    std::ofstream visual(output_dir / "visual_data.csv");
    std::ofstream quality(output_dir / "quality_summary.csv");
    std::ofstream counter_summary(output_dir / "counter_summary.csv");
    if (!summary || !visual || !quality || !counter_summary) throw std::runtime_error("cannot create analysis outputs");
    const std::string summary_header =
        "build_id,mode,implementation,state,perf_profile,footprint_bytes,accepted_rows,"
        "median_ns_per_operation,p05_ns_per_operation,p95_ns_per_operation,"
        "median_tsc_ticks_per_operation,p05_tsc_ticks_per_operation,p95_tsc_ticks_per_operation\n";
    summary << summary_header;
    visual << summary_header;
    quality << "build_id,mode,implementation,state,perf_profile,footprint_bytes,reason,rows\n";
    counter_summary << "build_id,mode,implementation,state,perf_profile,footprint_bytes,event,accepted_rows,"
                       "median_per_operation,p05_per_operation,p95_per_operation\n";

    for (const auto& [key, rows] : accepted) {
      std::vector<double> ns_per_op;
      std::vector<double> ticks_per_op;
      std::map<std::string, std::vector<double>> event_per_op;
      for (const Sample* sample : rows) {
        if (sample->logical_operations == 0) continue;
        ns_per_op.push_back(static_cast<double>(sample->wall_ns) / sample->logical_operations);
        ticks_per_op.push_back(static_cast<double>(sample->tsc_ticks) / sample->logical_operations);
        const auto counter_it = counters_by_sample.find(sample->sample_id);
        if (counter_it != counters_by_sample.end()) {
          for (const Counter& counter : counter_it->second) {
            event_per_op[counter.event].push_back(static_cast<double>(counter.value) / sample->logical_operations);
          }
        }
      }
      const auto write_summary = [&](std::ostream& output) {
        output << key.build_id << ',' << key.mode << ',' << key.implementation << ',' << key.state << ','
               << key.profile << ',' << key.footprint_bytes << ',' << rows.size() << ',';
        number(output, percentile(ns_per_op, 0.50)); output << ',';
        number(output, percentile(ns_per_op, 0.05)); output << ',';
        number(output, percentile(ns_per_op, 0.95)); output << ',';
        number(output, percentile(ticks_per_op, 0.50)); output << ',';
        number(output, percentile(ticks_per_op, 0.05)); output << ',';
        number(output, percentile(ticks_per_op, 0.95)); output << '\n';
      };
      write_summary(summary);
      write_summary(visual);
      for (const auto& [event, values] : event_per_op) {
        counter_summary << key.build_id << ',' << key.mode << ',' << key.implementation << ',' << key.state << ','
                        << key.profile << ',' << key.footprint_bytes << ',' << event << ',' << values.size() << ',';
        number(counter_summary, percentile(values, 0.50)); counter_summary << ',';
        number(counter_summary, percentile(values, 0.05)); counter_summary << ',';
        number(counter_summary, percentile(values, 0.95)); counter_summary << '\n';
      }
    }
    for (const auto& [key, reasons] : rejected) {
      for (const auto& [reason, count] : reasons) {
        quality << key.build_id << ',' << key.mode << ',' << key.implementation << ',' << key.state << ','
                << key.profile << ',' << key.footprint_bytes << ',' << reason << ',' << count << '\n';
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "analyze_working_set: " << error.what() << '\n';
    return 1;
  }
}
