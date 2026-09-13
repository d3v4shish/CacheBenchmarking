// Go control for the strict 4-byte random index cycle.
package main

/*
#cgo CFLAGS: -I${SRCDIR}
#include "pmu_scope.h"
*/
import "C"
import (
	"fmt"
	"os"
	"runtime"
	"strconv"
)

type pmuResult struct {
	cycles, instructions, refCycles, timeEnabled, timeRunning uint64
	available                                                 bool
}

func splitmix64(s *uint64) uint64 {
	*s += 0x9e3779b97f4a7c15
	x := *s
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9
	x = (x ^ (x >> 27)) * 0x94d049bb133111eb
	return x ^ (x >> 31)
}
func shuffle(v []uint32) {
	s := uint64(20260909) ^ 0x12233445
	for i := len(v); i > 1; i-- {
		j := int(splitmix64(&s) % uint64(i))
		v[i-1], v[j] = v[j], v[i-1]
	}
}

// next[index] supplies the following address. Range would enumerate values,
// so this explicit loop keeps the data dependency visible in the source.
func chase(next []uint32, start uint32, operations uint64) uint32 {
	index := start
	for step := uint64(0); step < operations; step++ {
		index = next[index]
	}
	return index
}
func measure(f func() uint32) (uint32, uint64, pmuResult) {
	var s C.scan_pmu_scope
	var r C.scan_pmu_result
	if C.scan_pmu_open(&s) != 0 || C.scan_pmu_start(&s) != 0 {
		panic("could not start PMU")
	}
	begun := uint64(C.scan_clock_now_ns())
	checksum := f()
	elapsed := uint64(C.scan_clock_now_ns()) - begun
	if C.scan_pmu_stop(&s, &r) != 0 {
		panic("could not read PMU")
	}
	available := C.scan_pmu_is_available(&s) != 0
	C.scan_pmu_close(&s)
	return checksum, elapsed, pmuResult{uint64(r.cycles), uint64(r.instructions), uint64(r.ref_cycles), uint64(r.time_enabled), uint64(r.time_running), available}
}
func main() {
	if len(os.Args) != 3 {
		panic("usage: index_go bytes passes")
	}
	bytes, e := strconv.Atoi(os.Args[1])
	if e != nil || bytes < 64 || bytes%64 != 0 {
		panic("bytes must be a multiple of 64")
	}
	passes, e := strconv.ParseUint(os.Args[2], 10, 64)
	if e != nil || passes == 0 {
		panic("passes must be positive")
	}
	entries := bytes / 4
	next := make([]uint32, entries)
	order := make([]uint32, entries)
	for i := range order {
		order[i] = uint32(i)
	}
	shuffle(order)
	for i := range order {
		next[order[i]] = order[(i+1)%entries]
	}
	start := order[0]
	operations := uint64(entries) * passes
	checksum, elapsed, pmu := measure(func() uint32 { return chase(next, start, operations) })
	runtime.KeepAlive(next)
	runtime.KeepAlive(order)
	if checksum != start || !pmu.available || pmu.timeEnabled == 0 || pmu.timeEnabled != pmu.timeRunning {
		panic("invalid checksum or PMU interval")
	}
	fmt.Printf("bytes=%d,passes=%d,operations=%d,elapsed_ns=%d,checksum=%d,expected_checksum=%d,pmu_cycles=%d,pmu_instructions=%d,pmu_ref_cycles=%d,pmu_time_enabled=%d,pmu_time_running=%d,pmu_available=%t\n", bytes, passes, operations, elapsed, checksum, start, pmu.cycles, pmu.instructions, pmu.refCycles, pmu.timeEnabled, pmu.timeRunning, pmu.available)
}
