// Go front end for the late-prefetch node cycle. Go has no prefetch builtin,
// so the one-instruction helper is explicit in prefetch_amd64.s. Its CALL/RET
// is part of the measured Go code shape and is not hidden as a Cgo call.
package main

/*
#cgo CFLAGS: -I${SRCDIR}/..
#include "pmu_scope.h"
*/
import "C"
import (
	prefetch "./prefetchasm"
	"fmt"
	"os"
	"runtime"
	"strconv"
	"unsafe"
)

type node struct { next uint32; value uint32; padding [56]byte }
type pmuResult struct { cycles, instructions, refCycles, timeEnabled, timeRunning uint64; available bool }

func splitmix64(state *uint64) uint64 { *state += 0x9e3779b97f4a7c15; value := *state; value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9; value = (value ^ (value >> 27)) * 0x94d049bb133111eb; return value ^ (value >> 31) }
func shuffle(values []uint32) { state := uint64(20260910) ^ 0xabcddcba; for index := len(values); index > 1; index-- { swapIndex := int(splitmix64(&state) % uint64(index)); values[index-1], values[swapIndex] = values[swapIndex], values[index-1] } }

func chaseWithPrefetch(nodes []node, start uint32, operations uint64) uint64 {
	index, sum := start, uint64(0)
	for step := uint64(0); step < operations; step++ {
		current := nodes[index]                         // demand-load current line
		next := current.next                            // successor supplies next address
		prefetch.T2(unsafe.Pointer(&nodes[next]))       // explicit assembly PREFETCHT2
		sum += uint64(current.value)                     // tiny overlap window only
		index = next
	}
	return sum ^ uint64(index)
}

func measure(work func() uint64) (uint64, uint64, pmuResult) {
	var scope C.scan_pmu_scope; var result C.scan_pmu_result
	if C.scan_pmu_open(&scope) != 0 || C.scan_pmu_start(&scope) != 0 { panic("could not start PMU") }
	begun := uint64(C.scan_clock_now_ns()); checksum := work(); elapsed := uint64(C.scan_clock_now_ns()) - begun
	if C.scan_pmu_stop(&scope, &result) != 0 { panic("could not read PMU") }
	available := C.scan_pmu_is_available(&scope) != 0; C.scan_pmu_close(&scope)
	return checksum, elapsed, pmuResult{uint64(result.cycles), uint64(result.instructions), uint64(result.ref_cycles), uint64(result.time_enabled), uint64(result.time_running), available}
}

func main() {
	if len(os.Args) != 3 { panic("usage: prefetch_go footprint_bytes passes") }
	bytes, err := strconv.Atoi(os.Args[1]); if err != nil || bytes < 1024 || bytes%64 != 0 { panic("bytes must be a multiple of 64") }
	passes, err := strconv.ParseUint(os.Args[2], 10, 64); if err != nil || passes == 0 { panic("passes must be positive") }
	count := bytes / 64; raw := make([]byte, bytes+63); base := (uintptr(unsafe.Pointer(&raw[0])) + 63) &^ uintptr(63); nodes := unsafe.Slice((*node)(unsafe.Pointer(base)), count)
	order := make([]uint32, count); for index := range order { order[index] = uint32(index) }; shuffle(order)
	state, onePassSum := uint64(20260910), uint64(0); for index := range order { nodes[order[index]].next = order[(index+1)%count]; nodes[order[index]].value = uint32(splitmix64(&state)); onePassSum += uint64(nodes[order[index]].value) }
	start := order[0]; operations := uint64(count) * passes; expected := onePassSum*passes ^ uint64(start)
	checksum, elapsed, pmu := measure(func() uint64 { return chaseWithPrefetch(nodes, start, operations) })
	runtime.KeepAlive(raw); runtime.KeepAlive(order)
	if checksum != expected || !pmu.available || pmu.timeEnabled == 0 || pmu.timeEnabled != pmu.timeRunning { panic("invalid checksum or multiplexed PMU interval") }
	fmt.Printf("bytes=%d,passes=%d,operations=%d,elapsed_ns=%d,checksum=%d,expected_checksum=%d,pmu_cycles=%d,pmu_instructions=%d,pmu_ref_cycles=%d,pmu_time_enabled=%d,pmu_time_running=%d,pmu_available=%t\n", bytes, passes, operations, elapsed, checksum, expected, pmu.cycles, pmu.instructions, pmu.refCycles, pmu.timeEnabled, pmu.timeRunning, pmu.available)
}
