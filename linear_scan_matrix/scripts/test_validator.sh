#!/usr/bin/env bash
# Deterministic synthetic fixture for the raw-result validator.
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT

printf 'sample_id,round,variant,implementation,value_type,footprint_bytes,passes,elapsed_ns,checksum,expected_checksum,pmu_cycles,pmu_instructions,pmu_ref_cycles,pmu_time_enabled,pmu_time_running,pmu_available\n' > "$fixture/samples.csv"
printf 'sample_id,event,value,unit,time_enabled,time_running\n' > "$fixture/pmu.csv"
printf 'round,sample_id,variant,implementation,value_type,footprint_bytes,passes,shuffle_key\n' > "$fixture/run_order.csv"

for round in $(seq 1 15); do
  for group in $(seq 1 285); do
    id="r${round}-g${group}"
    printf '%s,%s,v%s,scalar,int,32768,1,1,7,7,11,13,17,19,19,1\n' "$id" "$round" "$group" >> "$fixture/samples.csv"
    printf '%s,cycles,11,count,19,19\n' "$id" >> "$fixture/pmu.csv"
    printf '%s,instructions,13,count,19,19\n' "$id" >> "$fixture/pmu.csv"
    printf '%s,ref-cycles,17,count,19,19\n' "$id" >> "$fixture/pmu.csv"
    printf '%s,%s,v%s,scalar,int,32768,1,%s\n' "$round" "$id" "$group" "$group" >> "$fixture/run_order.csv"
  done
done
"$root/scripts/validate_results.sh" "$fixture" >/dev/null
printf 'validator fixture: ok\n'
