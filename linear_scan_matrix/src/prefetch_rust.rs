//! Rust front end for the same random 64-byte-node late-prefetch control.
//! `_mm_prefetch(..., _MM_HINT_T2)` is explicit: it happens only after the
//! current successor load has supplied the next node's address.
use std::{arch::x86_64::{_mm_prefetch, _MM_HINT_T2}, env, hint::black_box};

#[repr(C)] struct PmuScope { leader_fd: i32, member_fds: [i32; 2] }
#[repr(C)] #[derive(Default)] struct PmuResult { cycles: u64, instructions: u64, ref_cycles: u64, time_enabled: u64, time_running: u64 }
extern "C" {
    fn scan_clock_now_ns() -> u64;
    fn scan_pmu_open(scope: *mut PmuScope) -> i32;
    fn scan_pmu_start(scope: *const PmuScope) -> i32;
    fn scan_pmu_stop(scope: *const PmuScope, result: *mut PmuResult) -> i32;
    fn scan_pmu_is_available(scope: *const PmuScope) -> i32;
    fn scan_pmu_close(scope: *mut PmuScope);
}
#[repr(C, align(64))] #[derive(Clone, Copy)] struct Node { next: u32, value: u32, padding: [u8; 56] }

fn splitmix64(state: &mut u64) -> u64 { *state = state.wrapping_add(0x9e37_79b9_7f4a_7c15); let mut value = *state; value = (value ^ (value >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9); value = (value ^ (value >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb); value ^ (value >> 31) }
fn shuffle(values: &mut [u32]) { let mut state = 20_260_910u64 ^ 0xabcd_dcba; for index in (1..values.len()).rev() { let swap_index = (splitmix64(&mut state) % (index as u64 + 1)) as usize; values.swap(index, swap_index); } }

#[inline(never)] fn chase_with_prefetch(nodes: &[Node], start: u32, operations: u64) -> u64 {
    let (mut index, mut sum) = (start, 0u64);
    for _ in 0..operations {
        let node = nodes[index as usize]; // demand-load current 64-byte node
        let next = node.next;             // the next address becomes known here
        unsafe { _mm_prefetch(nodes.as_ptr().add(next as usize) as *const i8, _MM_HINT_T2); }
        sum = sum.wrapping_add(node.value as u64);
        index = next;
    }
    black_box(sum ^ index as u64)
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 { panic!("usage: prefetch_rust footprint_bytes passes"); }
    let bytes: usize = args[1].parse().unwrap(); let passes: u64 = args[2].parse().unwrap();
    if bytes < 1024 || bytes % 64 != 0 || passes == 0 { panic!("footprint must be a nonzero multiple of 64 bytes"); }
    let count = bytes / 64; let mut nodes = vec![Node { next: 0, value: 0, padding: [0; 56] }; count];
    let mut order: Vec<u32> = (0..count as u32).collect(); shuffle(&mut order);
    let (mut state, mut one_pass_sum) = (20_260_910u64, 0u64);
    for index in 0..count { nodes[order[index] as usize].next = order[(index + 1) % count]; nodes[order[index] as usize].value = splitmix64(&mut state) as u32; one_pass_sum = one_pass_sum.wrapping_add(nodes[order[index] as usize].value as u64); }
    let start = order[0]; let operations = count as u64 * passes; let expected = one_pass_sum.wrapping_mul(passes) ^ start as u64;
    let mut scope = PmuScope { leader_fd: -1, member_fds: [-1, -1] }; let mut pmu = PmuResult::default();
    unsafe { if scan_pmu_open(&mut scope) != 0 || scan_pmu_start(&scope) != 0 { panic!("could not start PMU"); } }
    let begun = unsafe { scan_clock_now_ns() }; let checksum = chase_with_prefetch(&nodes, start, operations); let elapsed = unsafe { scan_clock_now_ns() } - begun;
    let available = unsafe { if scan_pmu_stop(&scope, &mut pmu) != 0 { panic!("could not read PMU"); } let value = scan_pmu_is_available(&scope) != 0; scan_pmu_close(&mut scope); value };
    if checksum != expected || !available || pmu.time_enabled == 0 || pmu.time_enabled != pmu.time_running { panic!("invalid checksum or multiplexed PMU interval"); }
    println!("bytes={bytes},passes={passes},operations={operations},elapsed_ns={elapsed},checksum={checksum},expected_checksum={expected},pmu_cycles={},pmu_instructions={},pmu_ref_cycles={},pmu_time_enabled={},pmu_time_running={},pmu_available={}", pmu.cycles, pmu.instructions, pmu.ref_cycles, pmu.time_enabled, pmu.time_running, available as u8);
}
