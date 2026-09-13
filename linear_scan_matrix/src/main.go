// Go v2 linear scan. cgo is used only to call the same in-process PMU scope
// as the other binaries; the scanned data and scalar reduction remain Go.
package main

/*
#cgo CFLAGS: -I${SRCDIR}
#include "pmu_scope.h"
*/
import "C"

import (
	"fmt"
	"math"
	"os"
	"runtime"
	"strconv"
)

type string16 [16]byte

var sink uint64

type pmuResult struct {
	cycles       uint64
	instructions uint64
	refCycles    uint64
	timeEnabled  uint64
	timeRunning  uint64
	available    bool
}

func measure(scan func() uint64) (uint64, uint64, pmuResult) {
	var scope C.scan_pmu_scope
	var result C.scan_pmu_result
	if C.scan_pmu_open(&scope) != 0 {
		panic("scan_pmu_open failed")
	}
	defer C.scan_pmu_close(&scope)
	if C.scan_pmu_start(&scope) != 0 {
		panic("scan_pmu_start failed")
	}
	start := uint64(C.scan_clock_now_ns())
	checksum := scan()
	elapsed := uint64(C.scan_clock_now_ns()) - start
	if C.scan_pmu_stop(&scope, &result) != 0 {
		panic("scan_pmu_stop failed")
	}
	available := C.scan_pmu_is_available(&scope) != 0
	return checksum, elapsed, pmuResult{
		cycles:       uint64(result.cycles),
		instructions: uint64(result.instructions),
		refCycles:    uint64(result.ref_cycles),
		timeEnabled:  uint64(result.time_enabled),
		timeRunning:  uint64(result.time_running),
		available:    available,
	}
}

func expectedChecksum(kind string, count, passes uint64) uint64 {
	operations := count * passes
	switch kind {
	case "short":
		return uint64(uint16(operations * 0x0101))
	case "int":
		return uint64(uint32(operations * 0x01010101))
	case "float", "double":
		return 0
	case "string16":
		return operations * 16
	default:
		panic("unknown type")
	}
}

func main() {
	if len(os.Args) != 5 || os.Args[2] != "scalar" {
		panic("usage: linear_scan_go type scalar bytes passes")
	}
	kind := os.Args[1]
	bytes, err := strconv.Atoi(os.Args[3])
	if err != nil || bytes <= 0 {
		panic("bytes must be a positive integer")
	}
	passes, err := strconv.Atoi(os.Args[4])
	if err != nil || passes <= 0 {
		panic("passes must be a positive integer")
	}

	var checksum, expected, elapsed uint64
	var pmu pmuResult
	switch kind {
	case "short":
		if bytes%2 != 0 { panic("short footprint must be divisible by 2") }
		values := make([]uint16, bytes/2)
		for index := range values { values[index] = 0x0101 }
		expected = expectedChecksum(kind, uint64(len(values)), uint64(passes))
		checksum, elapsed, pmu = measure(func() uint64 {
			var sum uint16
			for pass := 0; pass < passes; pass++ {
				for _, value := range values { sum += value }
			}
			return uint64(sum)
		})
		runtime.KeepAlive(values)
	case "int":
		if bytes%4 != 0 { panic("int footprint must be divisible by 4") }
		values := make([]uint32, bytes/4)
		for index := range values { values[index] = 0x01010101 }
		expected = expectedChecksum(kind, uint64(len(values)), uint64(passes))
		checksum, elapsed, pmu = measure(func() uint64 {
			var sum uint32
			for pass := 0; pass < passes; pass++ {
				for _, value := range values { sum += value }
			}
			return uint64(sum)
		})
		runtime.KeepAlive(values)
	case "float":
		if bytes%4 != 0 { panic("float footprint must be divisible by 4") }
		values := make([]float32, bytes/4)
		for index := range values {
			if index&1 == 0 { values[index] = -1 } else { values[index] = 1 }
		}
		expected = expectedChecksum(kind, uint64(len(values)), uint64(passes))
		checksum, elapsed, pmu = measure(func() uint64 {
			var sum float32
			for pass := 0; pass < passes; pass++ {
				for _, value := range values { sum += value }
			}
			return uint64(math.Float32bits(sum))
		})
		runtime.KeepAlive(values)
	case "double":
		if bytes%8 != 0 { panic("double footprint must be divisible by 8") }
		values := make([]float64, bytes/8)
		for index := range values {
			if index&1 == 0 { values[index] = -1 } else { values[index] = 1 }
		}
		expected = expectedChecksum(kind, uint64(len(values)), uint64(passes))
		checksum, elapsed, pmu = measure(func() uint64 {
			var sum float64
			for pass := 0; pass < passes; pass++ {
				for _, value := range values { sum += value }
			}
			return math.Float64bits(sum)
		})
		runtime.KeepAlive(values)
	case "string16":
		if bytes%16 != 0 { panic("string16 footprint must be divisible by 16") }
		values := make([]string16, bytes/16)
		for index := range values { values[index] = string16{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1} }
		expected = expectedChecksum(kind, uint64(len(values)), uint64(passes))
		checksum, elapsed, pmu = measure(func() uint64 {
			var sum uint64
			for pass := 0; pass < passes; pass++ {
				for _, record := range values {
					for _, byteValue := range record { sum += uint64(byteValue) }
				}
			}
			return sum
		})
		runtime.KeepAlive(values)
	default:
		panic("unknown type")
	}

	sink = checksum
	if checksum != expected || (pmu.available && (pmu.timeEnabled == 0 || pmu.timeRunning != pmu.timeEnabled)) {
		panic("invalid checksum or multiplexed PMU group")
	}
	fmt.Printf("bytes=%d,passes=%d,elapsed_ns=%d,checksum=%d,expected_checksum=%d,pmu_cycles=%d,pmu_instructions=%d,pmu_ref_cycles=%d,pmu_time_enabled=%d,pmu_time_running=%d,pmu_available=%t\n",
		bytes, passes, elapsed, checksum, expected, pmu.cycles, pmu.instructions,
		pmu.refCycles, pmu.timeEnabled, pmu.timeRunning, pmu.available)
}
