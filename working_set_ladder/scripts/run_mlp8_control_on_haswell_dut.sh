#!/usr/bin/env bash
# A separately labelled MLP control for the random-access article.  It does
# not claim to be the direct-index workload: it advances eight independent
# 64-byte-node chains to expose overlapped miss latency.
set -Eeuo pipefail
[[ $# == 2 && $1 == /home/d3v/mlp8-control-run-* && -x $2/working_set_ladder ]] || { echo "usage: $0 /home/d3v/mlp8-control-run-UNIQUE /absolute/build-dir" >&2; exit 64; }
run_root=$1; build_root=$2; bin=$build_root/working_set_ladder; cpu=4; sibling=5; repetitions=15; seed=20260909; cases=$(mktemp); trap 'rm -f "$cases"' EXIT
[[ ! -e $run_root ]] || { echo "result directory exists" >&2; exit 73; }
[[ $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor) == performance && $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_min_freq) == $(cat /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_max_freq) && $(cat /sys/devices/system/cpu/intel_pstate/no_turbo) == 1 && $(cat /proc/sys/kernel/perf_event_paranoid) -le 0 && $(cat /sys/devices/system/cpu/cpu$sibling/online) == 0 ]] || { echo "DUT gate failed" >&2; exit 69; }
mkdir -p "$run_root"/{raw,manifest}; { date --iso-8601=seconds; uname -a; lscpu; grep -H . /sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_{governor,cur_freq,min_freq,max_freq}; cat /sys/devices/system/cpu/intel_pstate/no_turbo; cat /proc/sys/kernel/perf_event_paranoid; } > "$run_root/manifest/pre_run_machine.txt" 2>&1
sha256sum "$bin" > "$run_root/manifest/binary_sha256.txt"; objdump -d -Mintel "$bin" > "$run_root/manifest/disassembly_with_opcodes.txt"
for footprint in 32768 36864 262144 1048576 6291456 8388608 536870912; do printf '%s\n' "$footprint" >> "$cases"; done
samples="$run_root/raw/samples.csv"; counters="$run_root/raw/counters.csv"; printf 'round,footprint_bytes,shuffle_key\n' > "$run_root/raw/run_order.csv"
shuffle_cases(){ local round=$1; awk -v seed="$seed" -v round="$round" 'BEGIN{state=(seed+round*104729)%2147483647}{state=(state*48271)%2147483647;printf "%010d,%s\n",state,$0}' "$cases" | sort -t, -k1,1n; }
for round in $(seq 1 "$repetitions"); do while IFS=, read -r key footprint; do printf '%s,%s,%s\n' "$round" "$footprint" "$key" >> "$run_root/raw/run_order.csv"; taskset --cpu-list "$cpu" "$bin" --mode mlp8_cycle --implementation scalar --state warm --perf-profile basic --build-id random-access-mlp8-control --footprint-bytes "$footprint" --round "$round" --expected-cpu "$cpu" --min-duration-ms 250 --seed "$seed" --output "$samples" --counter-output "$counters"; done < <(shuffle_cases "$round"); done
awk -F, 'NR==1 {next} {if($5!="mlp8_cycle"||$6!="scalar"||$7!="warm"||$8!="basic"||$15!=4||$16!=4||$17!=0||$18!=0||$20!=0||$13<250000000)bad=1;groups[$9]++} END{for(g in groups)if(groups[g]!=15)bad=1;if(length(groups)!=7||NR-1!=105)bad=1;exit bad}' "$samples" || { echo "invalid mlp8 sample" >&2; exit 65; }
awk -F, 'NR>1 {print $9 "," $13/$11 "," $14/$11}' "$samples" | sort -t, -k1,1n -k2,2n | awk -F, 'BEGIN{print "footprint_bytes,repetitions,median_ns_per_node,median_tsc_ticks_per_node,p10_tsc_ticks_per_node,p90_tsc_ticks_per_node"}function emit(){if(n!=15)exit 65;printf "%s,%d,%.9f,%.9f,%.9f,%.9f\n",size,n,ns[8],ticks[8],ticks[2],ticks[14]}{if(NR==1||$1!=size){if(NR>1)emit();size=$1;n=0}ns[++n]=$2;ticks[n]=$3}END{emit()}' > "$run_root/raw/summary_mlp8.csv"
[[ $(wc -l < "$run_root/raw/summary_mlp8.csv") == 8 ]] || exit 65; sha256sum "$run_root/raw"/*.csv "$run_root/manifest"/*.txt > "$run_root/manifest/result_sha256.txt"; echo "completed $run_root"
