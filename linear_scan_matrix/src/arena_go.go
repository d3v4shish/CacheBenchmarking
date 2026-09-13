// Go control for the 64-byte arena pointer chase.  The backing byte slice is
// rounded up and aligned manually: this prevents an otherwise legal Go
// allocation from making every 64-byte Node straddle two cache lines.
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
	"unsafe"
)

type node struct {
	next    uint32
	value   uint32
	padding [56]byte
}
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
	s := uint64(20260909) ^ 0xabcddcba
	for i := len(v); i > 1; i-- {
		j := int(splitmix64(&s) % uint64(i))
		v[i-1], v[j] = v[j], v[i-1]
	}
}

// Each iteration has a genuine load-to-use chain: reading nodes[index].next
// provides the address used by the next iteration.  There is no SIMD version.
func chase(nodes []node, start uint32, operations uint64) uint64 {
	index := start
	var sum uint64
	for step := uint64(0); step < operations; step++ {
		n := nodes[index]
		sum += uint64(n.value)
		index = n.next
	}
	return sum ^ uint64(index)
}
func measure(f func() uint64) (uint64, uint64, pmuResult) {
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
		panic("usage: arena_go footprint_bytes passes")
	}
	bytes, e := strconv.Atoi(os.Args[1])
	if e != nil || bytes < 1024 || bytes%64 != 0 {
		panic("bytes must be a multiple of 64")
	}
	passes, e := strconv.ParseUint(os.Args[2], 10, 64)
	if e != nil || passes == 0 {
		panic("passes must be positive")
	}
	n := bytes / 64
	raw := make([]byte, bytes+63)
	base := (uintptr(unsafe.Pointer(&raw[0])) + 63) &^ uintptr(63)
	nodes := unsafe.Slice((*node)(unsafe.Pointer(base)), n)
	order := make([]uint32, n)
	for i := range order {
		order[i] = uint32(i)
	}
	shuffle(order)
	state := uint64(20260909)
	var one uint64
	for i := range order {
		nodes[order[i]].next = order[(i+1)%n]
		nodes[order[i]].value = uint32(splitmix64(&state))
		one += uint64(nodes[order[i]].value)
	}
	start := order[0]
	operations := uint64(n) * passes
	expected := one*passes ^ uint64(start)
	checksum, elapsed, pmu := measure(func() uint64 { return chase(nodes, start, operations) })
	runtime.KeepAlive(raw)
	runtime.KeepAlive(order)
	if checksum != expected || !pmu.available || pmu.timeEnabled == 0 || pmu.timeEnabled != pmu.timeRunning {
		panic("invalid checksum or multiplexed PMU interval")
	}
	fmt.Printf("bytes=%d,passes=%d,operations=%d,elapsed_ns=%d,checksum=%d,expected_checksum=%d,pmu_cycles=%d,pmu_instructions=%d,pmu_ref_cycles=%d,pmu_time_enabled=%d,pmu_time_running=%d,pmu_available=%t\n", bytes, passes, operations, elapsed, checksum, expected, pmu.cycles, pmu.instructions, pmu.refCycles, pmu.timeEnabled, pmu.timeRunning, pmu.available)
}
