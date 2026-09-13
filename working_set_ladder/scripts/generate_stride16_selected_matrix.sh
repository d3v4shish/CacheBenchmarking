#!/usr/bin/env bash
# Emit the four intentionally selected stride-16 C++ compiler products.
# This is not the exhaustive research matrix: it is the reproducible set used
# for the first retest, so unavailable compiler names remain visible evidence.

set -Eeuo pipefail

if [[ $# -ne 1 || "$1" != /* ]]; then
  echo "usage: $0 /absolute/new-or-empty/stride16_build_manifest.tsv" >&2
  exit 64
fi

MATRIX_PATH=$1
if [[ -e "$MATRIX_PATH" && -s "$MATRIX_PATH" ]]; then
  echo "refusing to overwrite non-empty manifest: $MATRIX_PATH" >&2
  exit 73
fi
mkdir -p -- "$(dirname -- "$MATRIX_PATH")"

availability() {
  command -v "$1" >/dev/null 2>&1 && printf 'available\n' || printf 'missing_compiler\n'
}

printf '%s\n' \
  $'config_id\tcompiler_id\tcompiler_family\tcompiler_path\toptimization\ttarget\timplementation\tvector_policy\tunroll\tinline\tlto\tpgo\tpie\tlinker\tstdlib\tallocator\tavailability' \
  >"$MATRIX_PATH"

for row in \
  "stride16-gcc13-scalar:gcc13:gcc:g++-13:scalar:vector_off" \
  "stride16-gcc13-auto:gcc13:gcc:g++-13:auto:vector_default" \
  "stride16-clang18-scalar:clang18:clang:clang++-18:scalar:vector_off" \
  "stride16-clang18-auto:clang18:clang:clang++-18:auto:vector_default"; do
  IFS=: read -r id compiler_id compiler_family compiler implementation vector_policy <<<"$row"
  printf '%s\t%s\t%s\t%s\tO3\thaswell\t%s\t%s\tdefault\tdefault\tnone\tnone\tnopie\tdefault\tdefault\tsystem\t%s\n' \
    "$id" "$compiler_id" "$compiler_family" "$compiler" "$implementation" "$vector_policy" "$(availability "$compiler")" \
    >>"$MATRIX_PATH"
done

echo "generated selected stride-16 matrix: $MATRIX_PATH"
