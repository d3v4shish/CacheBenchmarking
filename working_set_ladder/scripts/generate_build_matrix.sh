#!/usr/bin/env bash
# Cache Search Lab v2 — full compilation-matrix generator.
#
# This emits requested configurations; it does not compile or benchmark them.
# Keeping unavailable rows is intentional: a missing compiler, linker, library,
# or allocator is evidence that must not disappear from the article matrix.

set -Eeuo pipefail

if [[ $# -ne 1 || "$1" != /* ]]; then
  echo "usage: $0 /absolute/new-or-empty/build_manifest.tsv" >&2
  exit 64
fi

MATRIX_PATH=$1
if [[ -e "$MATRIX_PATH" && -s "$MATRIX_PATH" ]]; then
  echo "refusing to overwrite non-empty matrix: $MATRIX_PATH" >&2
  exit 73
fi
mkdir -p -- "$(dirname -- "$MATRIX_PATH")"

# Historical identifiers are retained even if the current host has newer tools.
# The runner records the executable's full --version output and SHA-256 before
# it accepts a build. Do not replace a missing name with the default compiler.
TOOLCHAINS=(
  "gcc13:gcc:g++-13"
  "clang18:clang:clang++-18"
  "gcc16:gcc:g++-16"
  "clang23:clang:clang++-23"
)
OPTIMIZATIONS=(O1 O2 O3 Os Oz)
TARGETS=(baseline_generic baseline_haswell_tune haswell native)
# Implementation describes source behavior; vector policy describes compiler
# permission. Scalar and AVX2 have only their meaningful policy combinations.
IMPLEMENTATIONS=(
  "scalar:vector_off"
  "auto:vector_default"
  "auto:vector_off"
  "avx2:vector_default"
)
UNROLLS=(default no_unroll aggressive)
INLINES=(default no_inline aggressive)
PGO_MODES=(none balanced hot dram)
PIE_MODES=(pie nopie)
LINKERS=(default bfd gold lld)
STDLIBS=(default libstdcxx libcxx)
ALLOCATORS=(system glibc jemalloc mimalloc)

printf '%s\n' \
  $'config_id\tcompiler_id\tcompiler_family\tcompiler_path\toptimization\ttarget\timplementation\tvector_policy\tunroll\tinline\tlto\tpgo\tpie\tlinker\tstdlib\tallocator\tavailability' \
  >"$MATRIX_PATH"

config_number=0
for toolchain in "${TOOLCHAINS[@]}"; do
  IFS=: read -r compiler_id compiler_family compiler_path <<<"$toolchain"
  compiler_availability=available
  command -v "$compiler_path" >/dev/null 2>&1 || compiler_availability=missing_compiler
  for optimization in "${OPTIMIZATIONS[@]}"; do
    for target in "${TARGETS[@]}"; do
      for implementation_policy in "${IMPLEMENTATIONS[@]}"; do
        IFS=: read -r implementation vector_policy <<<"$implementation_policy"
        for unroll in "${UNROLLS[@]}"; do
          for inline in "${INLINES[@]}"; do
            if [[ "$compiler_family" == gcc ]]; then LTOS=(none full); else LTOS=(none full thin); fi
            for lto in "${LTOS[@]}"; do
              for pgo in "${PGO_MODES[@]}"; do
                for pie in "${PIE_MODES[@]}"; do
                  for linker in "${LINKERS[@]}"; do
                    for stdlib in "${STDLIBS[@]}"; do
                      status=$compiler_availability
                      [[ "$compiler_family" == gcc && "$stdlib" == libcxx ]] && status=unsupported_compiler_stdlib_pair
                      for allocator in "${ALLOCATORS[@]}"; do
                        config_number=$((config_number + 1))
                        config_id=$(printf 'wsl-%07d' "$config_number")
                        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                          "$config_id" "$compiler_id" "$compiler_family" "$compiler_path" \
                          "$optimization" "$target" "$implementation" "$vector_policy" \
                          "$unroll" "$inline" "$lto" "$pgo" "$pie" "$linker" "$stdlib" \
                          "$allocator" "$status" >>"$MATRIX_PATH"
                      done
                    done
                  done
                done
              done
            done
          done
        done
      done
    done
  done
done

echo "generated $config_number requested build rows: $MATRIX_PATH"
