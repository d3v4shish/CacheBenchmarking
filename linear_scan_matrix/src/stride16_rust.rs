//! Rust member of the executed stride-16 language matrix.
//! `index += 16` advances by sixteen 4-byte words: one 64-byte Haswell line.

use std::{env, hint::black_box};

#[repr(C)]
struct PmuScope { leader_fd: i32, member_fds: [i32; 2] }

#[repr(C)]
#[derive(Default)]
struct PmuResult {
    cycles: u64, instructions: u64, ref_cycles: u64,
    time_enabled: u64, time_running: u64,
}

extern "C" {
    fn scan_clock_now_ns() -> u64;
    fn scan_pmu_open(scope: *mut PmuScope) -> i32;
    fn scan_pmu_start(scope: *const PmuScope) -> i32;
    fn scan_pmu_stop(scope: *const PmuScope, result: *mut PmuResult) -> i32;
    fn scan_pmu_is_available(scope: *const PmuScope) -> i32;
    fn scan_pmu_close(scope: *mut PmuScope);
}

// A slice bounds check remains semantically visible, but LLVM may prove it
// from the `index < len` loop condition. The load itself is one word per line.
fn stride16_sum(values: &[u32], passes: u64) -> u32 {
    let mut sum = 0u32;
    for _ in 0..passes {
        let mut index = 0usize;
        while index < values.len() {
            sum = sum.wrapping_add(values[index]); // 4 useful bytes of 64 fetched
            index += 16;
        }
    }
    black_box(sum)
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 { panic!("usage: stride16_rust footprint_bytes passes"); }
    let bytes: usize = args[1].parse().expect("bytes must be an integer");
    let passes: u64 = args[2].parse().expect("passes must be an integer");
    if bytes < 64 || bytes % 64 != 0 || passes == 0 {
        panic!("footprint must be a nonzero multiple of 64 bytes");
    }

    // Fill before timing so page faults and initialization are excluded.
    let values = vec![0x0101_0101u32; bytes / std::mem::size_of::<u32>()];
    let operations = (values.len() as u64 / 16) * passes;
    let mut scope = PmuScope { leader_fd: -1, member_fds: [-1, -1] };
    let mut pmu = PmuResult::default();
    unsafe {
        if scan_pmu_open(&mut scope) != 0 || scan_pmu_start(&scope) != 0 {
            panic!("could not start in-scan PMU group");
        }
    }
    let start_ns = unsafe { scan_clock_now_ns() };
    let checksum = stride16_sum(&values, passes);
    let elapsed_ns = unsafe { scan_clock_now_ns() } - start_ns;
    let pmu_available = unsafe {
        if scan_pmu_stop(&scope, &mut pmu) != 0 { panic!("could not read PMU"); }
        let available = scan_pmu_is_available(&scope) != 0;
        scan_pmu_close(&mut scope);
        available
    };
    let expected = operations.wrapping_mul(0x0101_0101) as u32;
    if checksum != expected || !pmu_available || pmu.time_enabled == 0 ||
       pmu.time_enabled != pmu.time_running {
        panic!("invalid checksum or multiplexed PMU interval");
    }
    println!("bytes={bytes},passes={passes},operations={operations},elapsed_ns={elapsed_ns},checksum={checksum},expected_checksum={expected},pmu_cycles={},pmu_instructions={},pmu_ref_cycles={},pmu_time_enabled={},pmu_time_running={},pmu_available={}",
        pmu.cycles, pmu.instructions, pmu.ref_cycles, pmu.time_enabled,
        pmu.time_running, pmu_available as u8);
}
