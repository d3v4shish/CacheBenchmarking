//! Diagnostic-only Rust scalar FP loops. Each body keeps one accumulator and
//! exact addition order; manual unrolling changes only loop-control work.

use std::{env, hint::black_box};

#[repr(C)]
struct PmuScope { leader_fd: i32, member_fds: [i32; 2] }
#[repr(C)]
#[derive(Default)]
struct PmuResult { cycles: u64, instructions: u64, ref_cycles: u64, time_enabled: u64, time_running: u64 }
extern "C" {
    fn scan_clock_now_ns() -> u64;
    fn scan_pmu_open(scope: *mut PmuScope) -> i32;
    fn scan_pmu_is_available(scope: *const PmuScope) -> i32;
    fn scan_pmu_start(scope: *const PmuScope) -> i32;
    fn scan_pmu_stop(scope: *const PmuScope, result: *mut PmuResult) -> i32;
    fn scan_pmu_close(scope: *mut PmuScope);
}

fn bits_f32(value: f32) -> u64 { value.to_bits() as u64 }
fn bits_f64(value: f64) -> u64 { value.to_bits() }

fn u1_f32(values: &[f32], passes: usize) -> u64 {
    let mut sum = 0.0;
    for _ in 0..passes { for &value in values { sum += value; } }
    black_box(bits_f32(sum))
}
fn u2_f32(values: &[f32], passes: usize) -> u64 {
    let mut sum = 0.0;
    for _ in 0..passes { let mut i = 0; while i + 2 <= values.len() { sum += values[i]; sum += values[i + 1]; i += 2; } while i < values.len() { sum += values[i]; i += 1; } }
    black_box(bits_f32(sum))
}
fn u8_f32(values: &[f32], passes: usize) -> u64 {
    let mut sum = 0.0;
    for _ in 0..passes { let mut i = 0; while i + 8 <= values.len() { sum += values[i]; sum += values[i+1]; sum += values[i+2]; sum += values[i+3]; sum += values[i+4]; sum += values[i+5]; sum += values[i+6]; sum += values[i+7]; i += 8; } while i < values.len() { sum += values[i]; i += 1; } }
    black_box(bits_f32(sum))
}
fn u1_f64(values: &[f64], passes: usize) -> u64 {
    let mut sum = 0.0;
    for _ in 0..passes { for &value in values { sum += value; } }
    black_box(bits_f64(sum))
}
fn u2_f64(values: &[f64], passes: usize) -> u64 {
    let mut sum = 0.0;
    for _ in 0..passes { let mut i = 0; while i + 2 <= values.len() { sum += values[i]; sum += values[i + 1]; i += 2; } while i < values.len() { sum += values[i]; i += 1; } }
    black_box(bits_f64(sum))
}
fn u8_f64(values: &[f64], passes: usize) -> u64 {
    let mut sum = 0.0;
    for _ in 0..passes { let mut i = 0; while i + 8 <= values.len() { sum += values[i]; sum += values[i+1]; sum += values[i+2]; sum += values[i+3]; sum += values[i+4]; sum += values[i+5]; sum += values[i+6]; sum += values[i+7]; i += 8; } while i < values.len() { sum += values[i]; i += 1; } }
    black_box(bits_f64(sum))
}

fn run(scan: impl FnOnce() -> u64, bytes: usize, passes: usize) {
    let mut scope = PmuScope { leader_fd: -1, member_fds: [-1, -1] };
    let mut pmu = PmuResult::default();
    unsafe {
        if scan_pmu_open(&mut scope) != 0 || scan_pmu_start(&scope) != 0 { panic!("PMU start failed"); }
    }
    let begin = unsafe { scan_clock_now_ns() };
    let checksum = scan();
    let elapsed = unsafe { scan_clock_now_ns() } - begin;
    let pmu_available = unsafe {
        if scan_pmu_stop(&scope, &mut pmu) != 0 { scan_pmu_close(&mut scope); panic!("PMU stop failed"); }
        let available = scan_pmu_is_available(&scope) != 0;
        scan_pmu_close(&mut scope);
        available
    };
    if checksum != 0 || (pmu_available && (pmu.time_enabled == 0 || pmu.time_running != pmu.time_enabled)) { panic!("invalid result"); }
    println!("bytes={bytes},passes={passes},elapsed_ns={elapsed},checksum={checksum},expected_checksum=0,pmu_cycles={},pmu_instructions={},pmu_ref_cycles={},pmu_time_enabled={},pmu_time_running={},pmu_available={}", pmu.cycles, pmu.instructions, pmu.ref_cycles, pmu.time_enabled, pmu.time_running, pmu_available as u8);
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 5 { panic!("usage: rust_unroll float|double u1|u2|u8 bytes passes"); }
    let bytes: usize = args[3].parse().unwrap();
    let passes: usize = args[4].parse().unwrap();
    match (args[1].as_str(), args[2].as_str()) {
        ("float", body @ ("u1" | "u2" | "u8")) if bytes % 4 == 0 => {
            let values: Vec<f32> = (0..bytes / 4).map(|i| if i & 1 == 0 { -1.0 } else { 1.0 }).collect();
            match body { "u1" => run(|| u1_f32(&values, passes), bytes, passes), "u2" => run(|| u2_f32(&values, passes), bytes, passes), _ => run(|| u8_f32(&values, passes), bytes, passes) }
        }
        ("double", body @ ("u1" | "u2" | "u8")) if bytes % 8 == 0 => {
            let values: Vec<f64> = (0..bytes / 8).map(|i| if i & 1 == 0 { -1.0 } else { 1.0 }).collect();
            match body { "u1" => run(|| u1_f64(&values, passes), bytes, passes), "u2" => run(|| u2_f64(&values, passes), bytes, passes), _ => run(|| u8_f64(&values, passes), bytes, passes) }
        }
        _ => panic!("bad arguments"),
    }
}
