#!/usr/bin/env bash
# Reversible DUT preparation. The measurement runner only verifies this state.
set -Eeuo pipefail

if [[ $# != 1 || ( $1 != prepare && $1 != restore ) ]]; then
  echo "usage: $0 prepare|restore" >&2
  exit 64
fi
cpu=4
sibling=5
state_file=${LINEAR_SCAN_DUT_STATE_FILE:-/tmp/linear-scan-v2-dut-state-$USER}
target_khz=${LINEAR_SCAN_TARGET_KHZ:-2200000}

write_root() { printf '%s\n' "$2" | sudo tee "$1" >/dev/null; }
read_state() { awk -F= -v key="$2" '$1 == key { print substr($0, length(key) + 2) }' "$1"; }

if [[ $1 == prepare ]]; then
  [[ ! -e $state_file ]] || { echo "state file already exists: $state_file" >&2; exit 65; }
  sudo -v
  {
    printf 'governor=%s\n' "$(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor)"
    printf 'min_khz=%s\n' "$(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq)"
    printf 'max_khz=%s\n' "$(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq)"
    printf 'no_turbo=%s\n' "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)"
    printf 'sibling_online=%s\n' "$(cat /sys/devices/system/cpu/cpu$sibling/online)"
  } > "$state_file"
  write_root /sys/devices/system/cpu/intel_pstate/no_turbo 1
  write_root /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor performance
  # Lower min before max so a higher pre-existing minimum cannot reject max.
  write_root /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq "$target_khz"
  write_root /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq "$target_khz"
  write_root /sys/devices/system/cpu/cpu$sibling/online 0
  echo "prepared; restore with $0 restore"
  exit 0
fi

[[ -f $state_file ]] || { echo "missing state file: $state_file" >&2; exit 65; }
sudo -v
old_max=$(read_state "$state_file" max_khz)
old_min=$(read_state "$state_file" min_khz)
write_root /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq "$old_max"
write_root /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq "$old_min"
write_root /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor "$(read_state "$state_file" governor)"
write_root /sys/devices/system/cpu/intel_pstate/no_turbo "$(read_state "$state_file" no_turbo)"
write_root /sys/devices/system/cpu/cpu$sibling/online "$(read_state "$state_file" sibling_online)"
rm -f "$state_file"
echo "restored"
