// Go member of the random-direct-load language control.  cgo only calls the
// common PMU helper.  Both the value/query slices are prepared before timing;
// the closure contains the exact randomized indirect payload read.
package main
/*
#cgo CFLAGS: -I${SRCDIR}
#include "pmu_scope.h"
*/
import "C"
import ("fmt"; "os"; "runtime"; "strconv")
type pmuResult struct { cycles,instructions,refCycles,timeEnabled,timeRunning uint64; available bool }
func splitmix64(state *uint64) uint64 { *state += 0x9e3779b97f4a7c15; x:=*state; x=(x^(x>>30))*0xbf58476d1ce4e5b9; x=(x^(x>>27))*0x94d049bb133111eb; return x^(x>>31) }
// Fisher-Yates uses the same seed and state progression as the other front ends.
func shuffle(q []uint32) { state:=uint64(20260909)^0x5eed5eed; for i:=len(q);i>1;i-- { j:=int(splitmix64(&state)%uint64(i)); q[i-1],q[j]=q[j],q[i-1] } }
// `query` determines the payload address.  Unlike a linear scan, the next
// cache line cannot be inferred from the current one by a fixed increment.
func randomSum(values,queries []uint32, passes uint64) uint32 { var sum uint32; for pass:=uint64(0);pass<passes;pass++ { for _,query:=range queries { sum += values[query] } }; return sum }
func measure(scan func()uint32)(uint32,uint64,pmuResult) { var scope C.scan_pmu_scope; var r C.scan_pmu_result; if C.scan_pmu_open(&scope)!=0 || C.scan_pmu_start(&scope)!=0 { panic("could not start PMU") }; start:=uint64(C.scan_clock_now_ns()); checksum:=scan(); elapsed:=uint64(C.scan_clock_now_ns())-start; if C.scan_pmu_stop(&scope,&r)!=0 { panic("could not read PMU") }; available:=C.scan_pmu_is_available(&scope)!=0; C.scan_pmu_close(&scope); return checksum,elapsed,pmuResult{uint64(r.cycles),uint64(r.instructions),uint64(r.ref_cycles),uint64(r.time_enabled),uint64(r.time_running),available} }
func main() {
 if len(os.Args)!=3 { panic("usage: random_go footprint_bytes passes") }; bytes,err:=strconv.Atoi(os.Args[1]); if err!=nil||bytes<64||bytes%64!=0 { panic("bytes must be a multiple of 64") }; passes,err:=strconv.ParseUint(os.Args[2],10,64); if err!=nil||passes==0 { panic("passes must be positive") }
 words:=bytes/4; values:=make([]uint32,words); queries:=make([]uint32,words); state:=uint64(20260909); var onePass uint32; for i:=range values { values[i]=uint32(splitmix64(&state)); queries[i]=uint32(i); onePass += values[i] }; shuffle(queries)
 operations:=uint64(words)*passes; checksum,elapsed,pmu:=measure(func()uint32{return randomSum(values,queries,passes)}); runtime.KeepAlive(values); runtime.KeepAlive(queries); expected:=onePass*uint32(passes)
 if checksum!=expected||!pmu.available||pmu.timeEnabled==0||pmu.timeEnabled!=pmu.timeRunning { panic("invalid checksum or multiplexed PMU interval") }
 fmt.Printf("bytes=%d,passes=%d,operations=%d,elapsed_ns=%d,checksum=%d,expected_checksum=%d,pmu_cycles=%d,pmu_instructions=%d,pmu_ref_cycles=%d,pmu_time_enabled=%d,pmu_time_running=%d,pmu_available=%t\n",bytes,passes,operations,elapsed,checksum,expected,pmu.cycles,pmu.instructions,pmu.refCycles,pmu.timeEnabled,pmu.timeRunning,pmu.available)
}
