//! Rust member of the random-direct-load language control.
//! The two `Vec<u32>` allocations are filled before timing: `queries` is a
//! deterministic permutation and the hot loop is exactly `values[queries[i]]`.

use std::{env, hint::black_box};

#[repr(C)] struct PmuScope { leader_fd: i32, member_fds: [i32; 2] }
#[repr(C)] #[derive(Default)] struct PmuResult { cycles:u64, instructions:u64, ref_cycles:u64, time_enabled:u64, time_running:u64 }
extern "C" { fn scan_clock_now_ns()->u64; fn scan_pmu_open(scope:*mut PmuScope)->i32; fn scan_pmu_start(scope:*const PmuScope)->i32; fn scan_pmu_stop(scope:*const PmuScope,result:*mut PmuResult)->i32; fn scan_pmu_is_available(scope:*const PmuScope)->i32; fn scan_pmu_close(scope:*mut PmuScope); }

// This is byte-for-byte the SplitMix arithmetic used by the C, C++ and Go
// front ends.  Keeping the state generator local makes the article source
// self-contained and makes a saved seed enough to recreate the query order.
fn splitmix64(state: &mut u64) -> u64 {
    *state = state.wrapping_add(0x9e37_79b9_7f4a_7c15);
    let mut x = *state;
    x = (x ^ (x >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
    x = (x ^ (x >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
    x ^ (x >> 31)
}
fn shuffle(queries: &mut [u32]) { let mut state=20_260_909u64 ^ 0x5eed_5eed; for i in (1..queries.len()).rev() { let j=(splitmix64(&mut state) % (i as u64 + 1)) as usize; queries.swap(i,j); } }

// Bounds checks are semantically safe and LLVM can prove the `i < len` check.
// The address of the payload load nevertheless depends on the random query.
#[inline(never)] fn random_sum(values:&[u32], queries:&[u32], passes:u64)->u32 {
    let mut sum=0u32;
    for _ in 0..passes { for &query in queries { sum=sum.wrapping_add(values[query as usize]); } }
    black_box(sum)
}
fn main() {
    let args:Vec<String>=env::args().collect(); if args.len()!=3 { panic!("usage: random_rust footprint_bytes passes"); }
    let bytes:usize=args[1].parse().expect("bytes"); let passes:u64=args[2].parse().expect("passes");
    if bytes<64 || bytes%64 != 0 || passes==0 { panic!("footprint must be a nonzero multiple of 64 bytes"); }
    let words=bytes/4; let mut values=vec![0u32;words]; let mut queries=vec![0u32;words]; let mut state=20_260_909u64; let mut one_pass=0u32;
    for i in 0..words { values[i]=splitmix64(&mut state) as u32; queries[i]=i as u32; one_pass=one_pass.wrapping_add(values[i]); }
    shuffle(&mut queries); let operations=words as u64 * passes;
    let mut scope=PmuScope{leader_fd:-1,member_fds:[-1,-1]}; let mut pmu=PmuResult::default();
    unsafe { if scan_pmu_open(&mut scope)!=0 || scan_pmu_start(&scope)!=0 { panic!("could not start PMU"); } }
    let start=unsafe{scan_clock_now_ns()}; let checksum=random_sum(&values,&queries,passes); let elapsed=unsafe{scan_clock_now_ns()}-start;
    let available=unsafe { if scan_pmu_stop(&scope,&mut pmu)!=0 { panic!("could not read PMU"); } let a=scan_pmu_is_available(&scope)!=0; scan_pmu_close(&mut scope); a };
    let expected=one_pass.wrapping_mul(passes as u32); if checksum!=expected || !available || pmu.time_enabled==0 || pmu.time_enabled!=pmu.time_running { panic!("invalid checksum or multiplexed PMU interval"); }
    println!("bytes={bytes},passes={passes},operations={operations},elapsed_ns={elapsed},checksum={checksum},expected_checksum={expected},pmu_cycles={},pmu_instructions={},pmu_ref_cycles={},pmu_time_enabled={},pmu_time_running={},pmu_available={}",pmu.cycles,pmu.instructions,pmu.ref_cycles,pmu.time_enabled,pmu.time_running,available as u8);
}
