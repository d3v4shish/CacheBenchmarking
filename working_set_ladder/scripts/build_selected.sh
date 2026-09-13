#!/usr/bin/env bash
# Build exactly one manifest row. PGO rows fail closed until a separately
# retained training profile is supplied; this prevents an accidental
# profile-less build from being mislabeled PGO.

set -Eeuo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
  echo "usage: $0 build_manifest.tsv config_id /absolute/output-directory [pgo-profile-directory]" >&2
  exit 64
fi

MATRIX_PATH=$1
CONFIG_ID=$2
OUT_DIR=$3
PGO_DIR=${4:-}
if [[ "$OUT_DIR" != /* || "$OUT_DIR" == *'..'* ]]; then
  echo "unsafe output directory: $OUT_DIR" >&2
  exit 64
fi
if [[ -e "$OUT_DIR" ]]; then
  echo "refusing to overwrite build directory: $OUT_DIR" >&2
  exit 73
fi

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BATCH_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)
SOURCE="$BATCH_ROOT/src/working_set_ladder.cpp"
ROW=$(awk -F '\t' -v id="$CONFIG_ID" 'NR > 1 && $1 == id { print; found=1; exit } END { if (!found) exit 64 }' "$MATRIX_PATH") || {
  echo "unknown config ID: $CONFIG_ID" >&2
  exit 64
}
IFS=$'\t' read -r id compiler_id compiler_family compiler optimization target implementation vector_policy \
  unroll inline lto pgo pie linker stdlib allocator availability <<<"$ROW"
if [[ "$availability" != available ]]; then
  echo "configuration is intentionally unavailable: $availability" >&2
  exit 69
fi
command -v "$compiler" >/dev/null 2>&1 || { echo "compiler disappeared: $compiler" >&2; exit 69; }
if [[ -n "${WSL_PGO_GENERATE:-}" && "$pgo" == none ]]; then
  echo "instrumented PGO build needs a named balanced, hot, or dram row" >&2
  exit 64
fi
if [[ -z "${WSL_PGO_GENERATE:-}" && "$pgo" != none && ( -z "$PGO_DIR" || ! -d "$PGO_DIR" ) ]]; then
  echo "PGO build requires a retained profile directory" >&2
  exit 64
fi

mkdir -p -- "$OUT_DIR/manifest"
FLAGS=(-std=c++20 -Wall -Wextra -Wpedantic -Werror "-$optimization")
case "$target" in
  baseline_generic) FLAGS+=(-march=x86-64 -mtune=generic) ;;
  baseline_haswell_tune) FLAGS+=(-march=x86-64 -mtune=haswell) ;;
  haswell) FLAGS+=(-march=haswell -mtune=haswell) ;;
  native) FLAGS+=(-march=native -mtune=native) ;;
  *) echo "unknown target: $target" >&2; exit 65 ;;
esac
if [[ "$vector_policy" == vector_off ]]; then
  if [[ "$compiler_family" == gcc ]]; then FLAGS+=(-fno-tree-vectorize -fno-tree-slp-vectorize)
  else FLAGS+=(-fno-vectorize -fno-slp-vectorize); fi
fi
case "$unroll" in
  no_unroll) FLAGS+=(-fno-unroll-loops) ;;
  aggressive) FLAGS+=(-funroll-loops) ;;
  default) ;;
  *) exit 65 ;;
esac
case "$inline" in
  no_inline) FLAGS+=(-fno-inline) ;;
  aggressive) FLAGS+=(-finline-functions) ;;
  default) ;;
  *) exit 65 ;;
esac
case "$lto" in
  none) ;;
  full) [[ "$compiler_family" == gcc ]] && FLAGS+=(-flto=auto) || FLAGS+=(-flto=full) ;;
  thin) FLAGS+=(-flto=thin) ;;
  *) exit 65 ;;
esac
[[ "$pie" == pie ]] && FLAGS+=(-fPIE -pie) || FLAGS+=(-fno-PIE -no-pie)
[[ "$linker" == default ]] || FLAGS+=("-fuse-ld=$linker")
if [[ "$compiler_family" == clang ]]; then
  [[ "$stdlib" == libstdcxx ]] && FLAGS+=(-stdlib=libstdc++)
  [[ "$stdlib" == libcxx ]] && FLAGS+=(-stdlib=libc++)
fi
if [[ -n "${WSL_PGO_GENERATE:-}" ]]; then
  mkdir -p -- "$WSL_PGO_GENERATE"
  if [[ "$compiler_family" == gcc ]]; then FLAGS+=("-fprofile-generate=$WSL_PGO_GENERATE")
  else FLAGS+=("-fprofile-instr-generate=$WSL_PGO_GENERATE/default.profraw"); fi
elif [[ "$pgo" != none ]]; then
  if [[ "$compiler_family" == gcc ]]; then FLAGS+=("-fprofile-use=$PGO_DIR" -fprofile-correction)
  else FLAGS+=("-fprofile-instr-use=$PGO_DIR/$pgo.profdata"); fi
fi

printf '%q ' "$compiler" "${FLAGS[@]}" -o "$OUT_DIR/working_set_ladder" "$SOURCE" >"$OUT_DIR/manifest/build_command.txt"
printf '\n' >>"$OUT_DIR/manifest/build_command.txt"
"$compiler" "${FLAGS[@]}" -o "$OUT_DIR/working_set_ladder" "$SOURCE"
"$compiler" --version >"$OUT_DIR/manifest/compiler_version.txt"
"$compiler" -dM -E -x c++ /dev/null >"$OUT_DIR/manifest/compiler_macros.txt"
sha256sum "$SOURCE" "$OUT_DIR/working_set_ladder" >"$OUT_DIR/manifest/source_and_binary_sha256.txt"
size --format=SysV "$OUT_DIR/working_set_ladder" >"$OUT_DIR/manifest/section_sizes.txt"
objdump -d -Mintel "$OUT_DIR/working_set_ladder" >"$OUT_DIR/manifest/disassembly_with_opcodes.txt"
nm -C --defined-only "$OUT_DIR/working_set_ladder" >"$OUT_DIR/manifest/symbols.txt"
printf '%s\n' "$ROW" >"$OUT_DIR/manifest/build_row.tsv"
printf '%s\n' "$allocator" >"$OUT_DIR/manifest/allocator.txt"
