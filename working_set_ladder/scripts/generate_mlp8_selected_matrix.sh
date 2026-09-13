#!/usr/bin/env bash
# Emit the Haswell GCC/Clang policies for the MLP8 retest. The ladder uses
# GCC scalar; remaining rows are preserved for later code-generation controls.
set -Eeuo pipefail

[[ $# == 1 && $1 == /* ]] || {
  echo "usage: $0 /absolute/new-or-empty/mlp8_build_manifest.tsv" >&2
  exit 64
}
matrix_path=$1
[[ ! -e "$matrix_path" || ! -s "$matrix_path" ]] || {
  echo "refusing to overwrite non-empty manifest: $matrix_path" >&2
  exit 73
}
mkdir -p -- "$(dirname -- "$matrix_path")"
availability() { command -v "$1" >/dev/null 2>&1 && printf 'available\n' || printf 'missing_compiler\n'; }

printf '%s\n' \
  $'config_id\tcompiler_id\tcompiler_family\tcompiler_path\toptimization\ttarget\timplementation\tvector_policy\tunroll\tinline\tlto\tpgo\tpie\tlinker\tstdlib\tallocator\tavailability' \
  >"$matrix_path"
for row in \
  "mlp8-gcc13-scalar:gcc13:gcc:g++-13:scalar:vector_off" \
  "mlp8-gcc13-auto:gcc13:gcc:g++-13:auto:vector_default" \
  "mlp8-clang18-scalar:clang18:clang:clang++-18:scalar:vector_off" \
  "mlp8-clang18-auto:clang18:clang:clang++-18:auto:vector_default"; do
  IFS=: read -r id compiler_id compiler_family compiler implementation vector_policy <<<"$row"
  printf '%s\t%s\t%s\t%s\tO3\thaswell\t%s\t%s\tdefault\tdefault\tnone\tnone\tnopie\tdefault\tdefault\tsystem\t%s\n' \
    "$id" "$compiler_id" "$compiler_family" "$compiler" "$implementation" "$vector_policy" "$(availability "$compiler")" \
    >>"$matrix_path"
done
