#!/usr/bin/env bash
# Derive reproducible medians from one retained DUT directory.  The script
# never changes raw rows; it writes only the two derived tables beside them.
set -Eeuo pipefail
if [[ $# != 1 || ! -f $1/samples.csv ]]; then
  echo "usage: $0 /path/to/retrieved-dut-result-directory" >&2; exit 64
fi
root=$1
samples=$root/samples.csv
if [[ -f $root/pmu.csv ]]; then perf=$root/pmu.csv
elif [[ -f $root/perf_reconstructed.csv ]]; then perf=$root/perf_reconstructed.csv
else echo "missing PMU records" >&2; exit 64
fi

awk -F, 'NR > 1 {
  width=($5=="short" ? 2 : (($5=="int" || $5=="float") ? 4 : ($5=="double" ? 8 : 16)))
  ns=$8/($7*($6/width)); print $3 "," $4 "," $5 "," $6 "," ns
}' "$samples" | sort -t, -k1,1 -k2,2 -k3,3 -k4,4n -k5,5n |
awk -F, 'BEGIN { print "variant,implementation,value_type,footprint_bytes,repetitions,median_ns_per_element,p10_ns_per_element,p90_ns_per_element,min_ns_per_element,max_ns_per_element" }
function emit() { if (n != 15) { print "wrong timing repetitions for " lang "," impl "," type "," size > "/dev/stderr"; exit 65 } print sprintf("%s,%s,%s,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f", lang, impl, type, size, n, v[8], v[2], v[14], v[1], v[n]) }
{
  if (NR == 1 || $1 != lang || $2 != impl || $3 != type || $4 != size) {
    if (NR > 1) emit(); lang=$1; impl=$2; type=$3; size=$4; n=0
  }
  v[++n]=$5
}
END { emit() }' > "$root/summary_ns_per_element.csv"

awk -F, 'NR == FNR {
  if (FNR > 1 && $2 == "cycles") cycles[$1]=$3
  else if (FNR > 1 && $2 == "instructions") instructions[$1]=$3
  else if (FNR > 1 && $2 == "ref-cycles") refcycles[$1]=$3
  next
}
FNR > 1 {
  width=($5=="short" ? 2 : (($5=="int" || $5=="float") ? 4 : ($5=="double" ? 8 : 16)))
  ops=$7*($6/width)
  print $3 "," $4 "," $5 "," $6 "," cycles[$1]/ops "," instructions[$1]/ops "," refcycles[$1]/ops
}' "$perf" "$samples" | sort -t, -k1,1 -k2,2 -k3,3 -k4,4n -k5,5n |
awk -F, 'BEGIN { print "variant,implementation,value_type,footprint_bytes,repetitions,median_cycles_per_element,p10_cycles_per_element,p90_cycles_per_element,median_instructions_per_element,p10_instructions_per_element,p90_instructions_per_element,median_ref_cycles_per_element" }
function emit() { if (n != 15) { print "wrong PMU repetitions for " lang "," impl "," type "," size > "/dev/stderr"; exit 65 } print sprintf("%s,%s,%s,%s,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f", lang, impl, type, size, n, c[8], c[2], c[14], i[8], i[2], i[14], r[8]) }
{
  if (NR == 1 || $1 != lang || $2 != impl || $3 != type || $4 != size) {
    if (NR > 1) emit(); lang=$1; impl=$2; type=$3; size=$4; n=0
  }
  c[++n]=$5; i[n]=$6; r[n]=$7
}
END { emit() }' > "$root/summary_perf_per_element.csv"

expected_rows=$(awk -F, 'NR > 1 { groups[$3 FS $4 FS $5 FS $6] = 1 } END { print length(groups) + 1 }' "$samples")
[[ $(wc -l < "$root/summary_ns_per_element.csv") == "$expected_rows" ]] || { echo "unexpected timing summary rows" >&2; exit 65; }
[[ $(wc -l < "$root/summary_perf_per_element.csv") == "$expected_rows" ]] || { echo "unexpected perf summary rows" >&2; exit 65; }
