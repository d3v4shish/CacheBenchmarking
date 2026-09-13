// Go front end for the MLP8 random 64-byte-node cycle.
package main

/*
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

func splitmix64(state *uint64) uint64 {
	*state += 0x9e3779b97f4a7c15
	value := *state
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9
	value = (value ^ (value >> 27)) * 0x94d049bb133111eb
	return value ^ (value >> 31)
}

func shuffle(order []uint32) {
	state := uint64(20260910) ^ 0xabcddcba
	for i := len(order); i > 1; i-- {
		other := int(splitmix64(&state) % uint64(i))
		order[i-1], order[other] = order[other], order[i-1]
	}
}

// There is a dependent chain within each lane, but no dependency between
// lanes.  The explicit values prevent this from becoming one serial chain.
func traverse(nodes []node, start uint32, operations uint64) uint64 {
	if operations%8 != 0 {
		panic("operation count must divide eight lanes")
	}
	lane0 := start
	first := nodes[lane0]
	lane1 := first.next
	first = nodes[lane1]
	lane2 := first.next
	first = nodes[lane2]
	lane3 := first.next
	first = nodes[lane3]
	lane4 := first.next
	first = nodes[lane4]
	lane5 := first.next
	first = nodes[lane5]
	lane6 := first.next
	first = nodes[lane6]
	lane7 := first.next
	var sum uint64
	for step := uint64(0); step < operations/8; step++ {
		// One independent node load per lane; this is not a software prefetch.
		node0 := nodes[lane0]
		node1 := nodes[lane1]
		node2 := nodes[lane2]
		node3 := nodes[lane3]
		node4 := nodes[lane4]
		node5 := nodes[lane5]
		node6 := nodes[lane6]
		node7 := nodes[lane7]
		sum += uint64(node0.value)
		sum += uint64(node1.value)
		sum += uint64(node2.value)
		sum += uint64(node3.value)
		sum += uint64(node4.value)
		sum += uint64(node5.value)
		sum += uint64(node6.value)
		sum += uint64(node7.value)
		lane0 = node0.next
		lane1 = node1.next
		lane2 = node2.next
		lane3 = node3.next
		lane4 = node4.next
		lane5 = node5.next
		lane6 = node6.next
		lane7 = node7.next
	}
	return sum ^ uint64(lane0) ^ uint64(lane1) ^ uint64(lane2) ^ uint64(lane3) ^
		uint64(lane4) ^ uint64(lane5) ^ uint64(lane6) ^ uint64(lane7)
}

func measure(work func() uint64) (uint64, uint64, pmuResult) {
	var scope C.scan_pmu_scope
	var raw C.scan_pmu_result
	if C.scan_pmu_open(&scope) != 0 || C.scan_pmu_start(&scope) != 0 {
		panic("could not start PMU")
	}
	begun := uint64(C.scan_clock_now_ns())
	checksum := work()
	elapsed := uint64(C.scan_clock_now_ns()) - begun
	if C.scan_pmu_stop(&scope, &raw) != 0 {
		panic("could not read PMU")
	}
	available := C.scan_pmu_is_available(&scope) != 0
	C.scan_pmu_close(&scope)
	return checksum, elapsed, pmuResult{
		cycles:      uint64(raw.cycles),
		instructions: uint64(raw.instructions),
		refCycles:   uint64(raw.ref_cycles),
		timeEnabled: uint64(raw.time_enabled),
		timeRunning: uint64(raw.time_running),
		available:   available,
	}
}

func main() {
	if len(os.Args) != 3 {
		panic("usage: mlp8_go footprint_bytes passes")
	}
	bytes, err := strconv.Atoi(os.Args[1])
	if err != nil || bytes < 1024 || bytes%64 != 0 {
		panic("footprint must be a nonzero multiple of 64 bytes")
	}
	passes, err := strconv.ParseUint(os.Args[2], 10, 64)
	if err != nil || passes == 0 {
		panic("passes must be positive")
	}
	if unsafe.Sizeof(node{}) != 64 {
		panic("node must occupy exactly one cache line")
	}
	count := bytes / 64
	if count%8 != 0 {
		panic("node count must divide eight lanes")
	}

	// Manually align the backing storage so Go's slice start cannot straddle
	// cache lines.  Fill/permutation happen before the PMU interval.
	raw := make([]byte, bytes+63)
	base := (uintptr(unsafe.Pointer(&raw[0])) + 63) &^ uintptr(63)
	nodes := unsafe.Slice((*node)(unsafe.Pointer(base)), count)
	order := make([]uint32, count)
	for i := range order {
		order[i] = uint32(i)
	}
	shuffle(order)
	payloadState := uint64(20260910)
	for i := range order {
		nodes[order[i]].next = order[(i+1)%count]
		nodes[order[i]].value = uint32(splitmix64(&payloadState))
	}
	operations := uint64(count) * passes
	start := order[0]
	expected := traverse(nodes, start, operations)
	checksum, elapsed, pmu := measure(func() uint64 {
		return traverse(nodes, start, operations)
	})
	runtime.KeepAlive(raw)
	runtime.KeepAlive(order)
	if checksum != expected || !pmu.available || pmu.timeEnabled == 0 ||
		pmu.timeEnabled != pmu.timeRunning {
		panic("invalid checksum or multiplexed PMU interval")
	}
	fmt.Printf(
		"bytes=%d,passes=%d,operations=%d,elapsed_ns=%d,checksum=%d,expected_checksum=%d,"+
			"pmu_cycles=%d,pmu_instructions=%d,pmu_ref_cycles=%d,"+
			"pmu_time_enabled=%d,pmu_time_running=%d,pmu_available=%t\n",
		bytes, passes, operations, elapsed, checksum, expected, pmu.cycles,
		pmu.instructions, pmu.refCycles, pmu.timeEnabled, pmu.timeRunning,
		pmu.available,
	)
}
