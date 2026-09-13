#!/usr/bin/env bash
# Emit the compiler policies used by the prefetch-chain retest.  The scalar
# ladder consumes the GCC 13 scalar row; the other rows are retained for the
# later same-source code-generation control.

set -Eeuo pipefail

if [[ $# -ne 1 || "$1" != /* ]]; then
  echo "usage: $0 /absolute/new-or-empty/prefetch_build_manifest.tsv" >&2
  exit 64
fi

matrix_path=$1
if [[ -e "$matrix_path" && -s "$matrix_path" ]]; then
  echo "refusing to overwrite non-empty manifest: $matrix_path" >&2
  exit 73
fi
mkdir -p -- "$(dirname -- "$matrix_path")"

availability() {
  command -v "$1" >/dev/null 2>&1 && printf 'available\n' || printf 'missing_compiler\n'
}

printf '%s\n' \
  $'config_id\tcompiler_id\tcompiler_family\tcompiler_path\toptimization\ttarget\timplementation\tvector_policy\tunroll\tinline\tlto\tpgo\tpie\tlinker\tstdlib\tallocator\tavailability' \
  >"$matrix_path"

for row in \
  "prefetch-gcc13-scalar:gcc13:gcc:g++-13:scalar:vector_off" \
  "prefetch-gcc13-auto:gcc13:gcc:g++-13:auto:vector_default" \
  "prefetch-clang18-scalar:clang18:clang:clang++-18:scalar:vector_off" \
  "prefetch-clang18-auto:clang18:clang:clang++-18:auto:vector_default"; do
  IFS=: read -r id compiler_id compiler_family compiler implementation vector_policy <<<"$row"
  printf '%s\t%s\t%s\t%s\tO3\thaswell\t%s\t%s\tdefault\tdefault\tnone\tnone\tnopie\tdefault\tdefault\tsystem\t%s\n' \
    "$id" "$compiler_id" "$compiler_family" "$compiler" "$implementation" "$vector_policy" "$(availability "$compiler")" \
    >>"$matrix_path"
done

echo "generated selected prefetch-chain matrix: $matrix_path"
