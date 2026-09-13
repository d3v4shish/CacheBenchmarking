// Go member of the executed stride-16 language matrix. cgo only enters the
// shared in-process PMU helper; allocation and the measured reduction are Go.
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
	cycles, instructions, refCycles uint64
	timeEnabled, timeRunning uint64
	available bool
}

// stride16Sum visits 0,16,32...: 16 uint32 values = one 64-byte cache line.
// The other fifteen words are deliberately not consumed by this workload.
func stride16Sum(values []uint32, passes uint64) uint32 {
	var sum uint32
	for pass := uint64(0); pass < passes; pass++ {
		for index := 0; index < len(values); index += 16 {
			sum += values[index] // one useful 4-byte word from this 64-byte line
		}
	}
	return sum
}

func measure(scan func() uint32) (uint32, uint64, pmuResult) {
	var scope C.scan_pmu_scope
	var result C.scan_pmu_result
	if C.scan_pmu_open(&scope) != 0 || C.scan_pmu_start(&scope) != 0 {
		panic("could not start in-scan PMU group")
	}
	start := uint64(C.scan_clock_now_ns())
	checksum := scan()
	elapsed := uint64(C.scan_clock_now_ns()) - start
	if C.scan_pmu_stop(&scope, &result) != 0 { panic("could not read PMU") }
	available := C.scan_pmu_is_available(&scope) != 0
	C.scan_pmu_close(&scope)
	return checksum, elapsed, pmuResult{
		cycles: uint64(result.cycles), instructions: uint64(result.instructions),
		refCycles: uint64(result.ref_cycles), timeEnabled: uint64(result.time_enabled),
		timeRunning: uint64(result.time_running), available: available,
	}
}

func main() {
	if len(os.Args) != 3 { panic("usage: stride16_go footprint_bytes passes") }
	bytes, err := strconv.Atoi(os.Args[1])
	if err != nil || bytes < 64 || bytes%64 != 0 { panic("bytes must be a multiple of 64") }
	passes, err := strconv.ParseUint(os.Args[2], 10, 64)
	if err != nil || passes == 0 { panic("passes must be positive") }

	// Allocation/fill are before measure: timed work begins at the first load.
	values := make([]uint32, bytes/4)
	for index := range values { values[index] = 0x01010101 }
	operations := uint64(len(values)/16) * passes
	checksum, elapsed, pmu := measure(func() uint32 { return stride16Sum(values, passes) })
	runtime.KeepAlive(values) // keeps the backing allocation live through the scan
	expected := uint32(operations * 0x01010101)
	if checksum != expected || !pmu.available || pmu.timeEnabled == 0 || pmu.timeEnabled != pmu.timeRunning {
		panic("invalid checksum or multiplexed PMU interval")
	}
	fmt.Printf("bytes=%d,passes=%d,operations=%d,elapsed_ns=%d,checksum=%d,expected_checksum=%d,pmu_cycles=%d,pmu_instructions=%d,pmu_ref_cycles=%d,pmu_time_enabled=%d,pmu_time_running=%d,pmu_available=%t\n",
		bytes, passes, operations, elapsed, checksum, expected, pmu.cycles,
		pmu.instructions, pmu.refCycles, pmu.timeEnabled, pmu.timeRunning, pmu.available)
}
