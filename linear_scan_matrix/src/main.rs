//! Rust v2 linear scan. Allocation/fill precede the shared PMU scope; the
//! scalar and explicit-AVX2 bodies below are intentionally separate labels.

use std::{env, hint::black_box};

#[repr(C)]
struct PmuScope {
    leader_fd: i32,
    member_fds: [i32; 2],
}

#[repr(C)]
#[derive(Default)]
struct PmuResult {
    cycles: u64,
    instructions: u64,
    ref_cycles: u64,
    time_enabled: u64,
    time_running: u64,
}

extern "C" {
    fn scan_clock_now_ns() -> u64;
    fn scan_pmu_open(scope: *mut PmuScope) -> i32;
    fn scan_pmu_is_available(scope: *const PmuScope) -> i32;
    fn scan_pmu_start(scope: *const PmuScope) -> i32;
    fn scan_pmu_stop(scope: *const PmuScope, result: *mut PmuResult) -> i32;
    fn scan_pmu_close(scope: *mut PmuScope);
}

#[repr(C)]
#[derive(Clone, Copy)]
struct String16 {
    bytes: [u8; 16],
}

fn pmu_error(operation: &str) -> ! {
    panic!("{operation}: {}", std::io::Error::last_os_error());
}

fn measure(scan: impl FnOnce() -> u64) -> (u64, u64, PmuResult, bool) {
    let mut scope = PmuScope {
        leader_fd: -1,
        member_fds: [-1, -1],
    };
    let mut result = PmuResult::default();
    unsafe {
        if scan_pmu_open(&mut scope) != 0 {
            pmu_error("scan_pmu_open");
        }
        if scan_pmu_start(&scope) != 0 {
            scan_pmu_close(&mut scope);
            pmu_error("scan_pmu_start");
        }
    }
    let start = unsafe { scan_clock_now_ns() };
    let checksum = scan();
    let elapsed_ns = unsafe { scan_clock_now_ns() } - start;
    let available = unsafe {
        if scan_pmu_stop(&scope, &mut result) != 0 {
            scan_pmu_close(&mut scope);
            pmu_error("scan_pmu_stop");
        }
        let available = scan_pmu_is_available(&scope) != 0;
        scan_pmu_close(&mut scope);
        available
    };
    (checksum, elapsed_ns, result, available)
}

fn scalar_u16(values: &[u16], passes: usize) -> u64 {
    let mut sum = 0u16;
    for _ in 0..passes {
        for &value in values {
            sum = sum.wrapping_add(value);
        }
    }
    black_box(sum as u64)
}

fn scalar_u32(values: &[u32], passes: usize) -> u64 {
    let mut sum = 0u32;
    for _ in 0..passes {
        for &value in values {
            sum = sum.wrapping_add(value);
        }
    }
    black_box(sum as u64)
}

fn scalar_f32(values: &[f32], passes: usize) -> u64 {
    let mut sum = 0.0f32;
    for _ in 0..passes {
        for &value in values {
            sum += value;
        }
    }
    black_box(sum.to_bits() as u64)
}

fn scalar_f64(values: &[f64], passes: usize) -> u64 {
    let mut sum = 0.0f64;
    for _ in 0..passes {
        for &value in values {
            sum += value;
        }
    }
    black_box(sum.to_bits())
}

fn scalar_s16(values: &[String16], passes: usize) -> u64 {
    let mut sum = 0u64;
    for _ in 0..passes {
        for record in values {
            for &byte in &record.bytes {
                sum += byte as u64;
            }
        }
    }
    black_box(sum)
}

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn avx_u16(values: &[u16], passes: usize) -> u64 {
    use std::arch::x86_64::*;
    let mut total = 0u16;
    for _ in 0..passes {
        let mut lanes = _mm256_setzero_si256();
        let mut index = 0usize;
        while index + 16 <= values.len() {
            lanes = _mm256_add_epi16(lanes, _mm256_loadu_si256(values.as_ptr().add(index) as *const __m256i));
            index += 16;
        }
        let mut reduced = [0u16; 16];
        _mm256_storeu_si256(reduced.as_mut_ptr() as *mut __m256i, lanes);
        for value in reduced { total = total.wrapping_add(value); }
        while index < values.len() { total = total.wrapping_add(values[index]); index += 1; }
    }
    black_box(total as u64)
}

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn avx_u32(values: &[u32], passes: usize) -> u64 {
    use std::arch::x86_64::*;
    let mut total = 0u32;
    for _ in 0..passes {
        let mut lanes = _mm256_setzero_si256();
        let mut index = 0usize;
        while index + 8 <= values.len() {
            lanes = _mm256_add_epi32(lanes, _mm256_loadu_si256(values.as_ptr().add(index) as *const __m256i));
            index += 8;
        }
        let mut reduced = [0u32; 8];
        _mm256_storeu_si256(reduced.as_mut_ptr() as *mut __m256i, lanes);
        for value in reduced { total = total.wrapping_add(value); }
        while index < values.len() { total = total.wrapping_add(values[index]); index += 1; }
    }
    black_box(total as u64)
}

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn avx_f32(values: &[f32], passes: usize) -> u64 {
    use std::arch::x86_64::*;
    let mut total = 0.0f32;
    for _ in 0..passes {
        let mut lanes = _mm256_setzero_ps();
        let mut index = 0usize;
        while index + 8 <= values.len() {
            lanes = _mm256_add_ps(lanes, _mm256_loadu_ps(values.as_ptr().add(index)));
            index += 8;
        }
        let mut reduced = [0.0f32; 8];
        _mm256_storeu_ps(reduced.as_mut_ptr(), lanes);
        for value in reduced { total += value; }
        while index < values.len() { total += values[index]; index += 1; }
    }
    black_box(total.to_bits() as u64)
}

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn avx_f64(values: &[f64], passes: usize) -> u64 {
    use std::arch::x86_64::*;
    let mut total = 0.0f64;
    for _ in 0..passes {
        let mut lanes = _mm256_setzero_pd();
        let mut index = 0usize;
        while index + 4 <= values.len() {
            lanes = _mm256_add_pd(lanes, _mm256_loadu_pd(values.as_ptr().add(index)));
            index += 4;
        }
        let mut reduced = [0.0f64; 4];
        _mm256_storeu_pd(reduced.as_mut_ptr(), lanes);
        for value in reduced { total += value; }
        while index < values.len() { total += values[index]; index += 1; }
    }
    black_box(total.to_bits())
}

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn avx_s16(values: &[String16], passes: usize) -> u64 {
    use std::arch::x86_64::*;
    let mut total = 0u64;
    let zero = _mm256_setzero_si256();
    for _ in 0..passes {
        let mut lanes = _mm256_setzero_si256();
        let mut index = 0usize;
        while index + 2 <= values.len() {
            let bytes = _mm256_loadu_si256(values.as_ptr().add(index) as *const __m256i);
            lanes = _mm256_add_epi64(lanes, _mm256_sad_epu8(bytes, zero));
            index += 2;
        }
        let mut reduced = [0u64; 4];
        _mm256_storeu_si256(reduced.as_mut_ptr() as *mut __m256i, lanes);
        total += reduced.into_iter().sum::<u64>();
        while index < values.len() {
            for &byte in &values[index].bytes { total += byte as u64; }
            index += 1;
        }
    }
    black_box(total)
}

fn require_avx2() {
    if !std::is_x86_feature_detected!("avx2") {
        panic!("the explicit AVX2 variant requires AVX2");
    }
}

fn expected_checksum(kind: &str, count: usize, passes: usize) -> u64 {
    let operations = (count as u64) * (passes as u64);
    match kind {
        "short" => (operations.wrapping_mul(0x0101) as u16) as u64,
        "int" => operations.wrapping_mul(0x01010101) as u32 as u64,
        "float" | "double" => 0,
        "string16" => operations * 16,
        _ => unreachable!(),
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 5 {
        panic!("usage: linear_scan_rust type scalar|avx2 bytes passes");
    }
    let kind = args[1].as_str();
    let implementation = args[2].as_str();
    let bytes: usize = args[3].parse().expect("bytes must be an integer");
    let passes: usize = args[4].parse().expect("passes must be an integer");
    if passes == 0 || !matches!(implementation, "scalar" | "avx2") {
        panic!("bad arguments");
    }
    if implementation == "avx2" { require_avx2(); }

    let (checksum, elapsed_ns, pmu, pmu_available, expected) = match kind {
        "short" => {
            let values = vec![0x0101u16; bytes / 2];
            let expected = expected_checksum(kind, values.len(), passes);
            let result = measure(|| if implementation == "avx2" {
                unsafe { avx_u16(&values, passes) }
            } else { scalar_u16(&values, passes) });
            (result.0, result.1, result.2, result.3, expected)
        }
        "int" => {
            let values = vec![0x01010101u32; bytes / 4];
            let expected = expected_checksum(kind, values.len(), passes);
            let result = measure(|| if implementation == "avx2" {
                unsafe { avx_u32(&values, passes) }
            } else { scalar_u32(&values, passes) });
            (result.0, result.1, result.2, result.3, expected)
        }
        "float" => {
            let values: Vec<f32> = (0..bytes / 4).map(|i| if i & 1 == 0 { -1.0 } else { 1.0 }).collect();
            let expected = expected_checksum(kind, values.len(), passes);
            let result = measure(|| if implementation == "avx2" {
                unsafe { avx_f32(&values, passes) }
            } else { scalar_f32(&values, passes) });
            (result.0, result.1, result.2, result.3, expected)
        }
        "double" => {
            let values: Vec<f64> = (0..bytes / 8).map(|i| if i & 1 == 0 { -1.0 } else { 1.0 }).collect();
            let expected = expected_checksum(kind, values.len(), passes);
            let result = measure(|| if implementation == "avx2" {
                unsafe { avx_f64(&values, passes) }
            } else { scalar_f64(&values, passes) });
            (result.0, result.1, result.2, result.3, expected)
        }
        "string16" => {
            let values = vec![String16 { bytes: [1; 16] }; bytes / 16];
            let expected = expected_checksum(kind, values.len(), passes);
            let result = measure(|| if implementation == "avx2" {
                unsafe { avx_s16(&values, passes) }
            } else { scalar_s16(&values, passes) });
            (result.0, result.1, result.2, result.3, expected)
        }
        _ => panic!("unknown type"),
    };
    if checksum != expected || (pmu_available && (pmu.time_enabled == 0 || pmu.time_running != pmu.time_enabled)) {
        panic!("invalid result: checksum={checksum} expected={expected} time_enabled={} time_running={}", pmu.time_enabled, pmu.time_running);
    }
    println!(
        "bytes={bytes},passes={passes},elapsed_ns={elapsed_ns},checksum={checksum},expected_checksum={expected},pmu_cycles={},pmu_instructions={},pmu_ref_cycles={},pmu_time_enabled={},pmu_time_running={},pmu_available={}",
        pmu.cycles, pmu.instructions, pmu.ref_cycles, pmu.time_enabled, pmu.time_running, pmu_available as u8
    );
}
