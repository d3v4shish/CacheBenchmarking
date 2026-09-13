#!/usr/bin/env bash
# Static v2 contract checks. These run without PMU permission or a DUT.
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

for file in src/linear_scan.c src/main.rs src/main.go src/cpp_native.cpp; do
  grep -q 'scan_pmu_open' "$root/$file"
  grep -q 'scan_pmu_start' "$root/$file"
  grep -q 'scan_pmu_stop' "$root/$file"
done
for file in src/index_c.c src/index_cpp.cpp src/index_rust.rs src/index_go.go; do
  grep -q 'scan_pmu_open' "$root/$file"
  grep -q 'scan_pmu_start' "$root/$file"
  grep -q 'scan_pmu_stop' "$root/$file"
done
for file in src/prefetch_c.c src/prefetch_cpp.cpp src/prefetch_rust.rs src/prefetch_go/main.go; do
  grep -q 'scan_pmu_open' "$root/$file"
  grep -q 'scan_pmu_start' "$root/$file"
  grep -q 'scan_pmu_stop' "$root/$file"
done
for file in src/mlp8_c.c src/mlp8_cpp.cpp src/mlp8_rust.rs src/mlp8_go.go; do
  grep -q 'scan_pmu_open' "$root/$file"
  grep -q 'scan_pmu_start' "$root/$file"
  grep -q 'scan_pmu_stop' "$root/$file"
done
# The index-cycle controls must retain a true loop-carried address dependency;
# none may substitute a direct random-index load or an independent stream.
grep -q 'index=next\[index\]' "$root/src/index_c.c"
grep -q 'index=next\[index\]' "$root/src/index_cpp.cpp"
grep -q 'index=next\[index as usize\]' "$root/src/index_rust.rs"
grep -q 'index = next\[index\]' "$root/src/index_go.go"
grep -q '__builtin_prefetch' "$root/src/prefetch_c.c"
grep -q '__builtin_prefetch' "$root/src/prefetch_cpp.cpp"
grep -q '_mm_prefetch' "$root/src/prefetch_rust.rs"
grep -q 'prefetch.T2' "$root/src/prefetch_go/main.go"
grep -q 'PREFETCHT2' "$root/src/prefetch_go/prefetchasm/prefetch_amd64.s"
grep -q 'lane7' "$root/src/mlp8_c.c"
grep -q 'lane7' "$root/src/mlp8_cpp.cpp"
grep -q 'lane7' "$root/src/mlp8_rust.rs"
grep -q 'lane7' "$root/src/mlp8_go.go"
for script in scripts/run_mlp8_language_on_haswell_dut.sh scripts/validate_mlp8_language_results.sh scripts/analyze_mlp8_language_results.sh; do
  test -x "$root/$script"
  bash -n "$root/$script"
done
for script in scripts/run_mlp8_cpp_compilers_on_haswell_dut.sh scripts/validate_mlp8_cpp_compilers_results.sh scripts/analyze_mlp8_cpp_compilers_results.sh; do
  test -x "$root/$script"
  bash -n "$root/$script"
done
for script in scripts/run_mlp8_lane_pair_on_haswell_dut.sh scripts/validate_mlp8_lane_pair_results.sh scripts/analyze_mlp8_lane_pair_results.sh; do
  test -x "$root/$script"
  bash -n "$root/$script"
done
for script in scripts/run_prefetch_language_on_haswell_dut.sh scripts/validate_prefetch_language_results.sh scripts/run_prefetch_cpp_compilers_on_haswell_dut.sh scripts/validate_prefetch_cpp_compilers_results.sh scripts/analyze_prefetch_cpp_compilers_results.sh; do
  test -x "$root/$script"
done
test -x "$root/scripts/run_prefetch_paired_control_on_haswell_dut.sh"
grep -q 'PREFETCH_ENABLED' "$root/src/prefetch_cpp.cpp"
grep -q 'i & 1' "$root/src/main.rs"
grep -q 'index&1' "$root/src/main.go"
grep -q 'i & 1' "$root/src/cpp_native.cpp"
grep -q 'string16' "$root/src/main.go"
grep -q 'String16' "$root/src/main.rs"
grep -q 'String16' "$root/src/cpp_native.cpp"
grep -q 'avx_u32' "$root/src/main.rs"
grep -q 'u8_f32' "$root/src/rust_unroll.rs"
grep -q 'unroll8' "$root/src/cpp_unroll.cpp"
printf 'source contract: ok\n'
