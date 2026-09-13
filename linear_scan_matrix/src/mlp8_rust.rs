//! Rust front end for the MLP8 cycle.
//!
//! lane0 through lane7 are eight independent data-dependent address streams.
//! Copying a Node retrieves both its payload and successor; each successor
//! becomes the following address only for that one lane.
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
    fn scan_pmu_start(scope: *const PmuScope) -> i32;
    fn scan_pmu_stop(scope: *const PmuScope, result: *mut PmuResult) -> i32;
    fn scan_pmu_is_available(scope: *const PmuScope) -> i32;
    fn scan_pmu_close(scope: *mut PmuScope);
}

#[repr(C, align(64))]
#[derive(Clone, Copy)]
struct Node {
    next: u32,
    value: u32,
    padding: [u8; 56],
}

fn splitmix64(state: &mut u64) -> u64 {
    *state = state.wrapping_add(0x9e37_79b9_7f4a_7c15);
    let mut value = *state;
    value = (value ^ (value >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
    value = (value ^ (value >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
    value ^ (value >> 31)
}

fn shuffle(order: &mut [u32]) {
    let mut state = 20_260_910u64 ^ 0xabcd_dcba;
    for i in (1..order.len()).rev() {
        let other = (splitmix64(&mut state) % (i as u64 + 1)) as usize;
        order.swap(i, other);
    }
}

#[inline(never)]
fn traverse(nodes: &[Node], start: u32, operations: u64) -> u64 {
    assert_eq!(operations % 8, 0);
    let mut lane0 = start;
    let first0 = nodes[lane0 as usize];
    let mut lane1 = first0.next;
    let first1 = nodes[lane1 as usize];
    let mut lane2 = first1.next;
    let first2 = nodes[lane2 as usize];
    let mut lane3 = first2.next;
    let first3 = nodes[lane3 as usize];
    let mut lane4 = first3.next;
    let first4 = nodes[lane4 as usize];
    let mut lane5 = first4.next;
    let first5 = nodes[lane5 as usize];
    let mut lane6 = first5.next;
    let first6 = nodes[lane6 as usize];
    let mut lane7 = first6.next;
    let mut sum = 0u64;

    for _ in 0..operations / 8 {
        // These eight reads are independent loads.  Haswell has no useful
        // SIMD gather here; every lane still has a strict load-to-use chain.
        let node0 = nodes[lane0 as usize];
        let node1 = nodes[lane1 as usize];
        let node2 = nodes[lane2 as usize];
        let node3 = nodes[lane3 as usize];
        let node4 = nodes[lane4 as usize];
        let node5 = nodes[lane5 as usize];
        let node6 = nodes[lane6 as usize];
        let node7 = nodes[lane7 as usize];
        sum = sum.wrapping_add(node0.value as u64);
        sum = sum.wrapping_add(node1.value as u64);
        sum = sum.wrapping_add(node2.value as u64);
        sum = sum.wrapping_add(node3.value as u64);
        sum = sum.wrapping_add(node4.value as u64);
        sum = sum.wrapping_add(node5.value as u64);
        sum = sum.wrapping_add(node6.value as u64);
        sum = sum.wrapping_add(node7.value as u64);
        lane0 = node0.next;
        lane1 = node1.next;
        lane2 = node2.next;
        lane3 = node3.next;
        lane4 = node4.next;
        lane5 = node5.next;
        lane6 = node6.next;
        lane7 = node7.next;
    }
    black_box(sum ^ lane0 as u64 ^ lane1 as u64 ^ lane2 as u64 ^ lane3 as u64 ^
              lane4 as u64 ^ lane5 as u64 ^ lane6 as u64 ^ lane7 as u64)
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 {
        panic!("usage: mlp8_rust footprint_bytes passes");
    }
    let bytes: usize = args[1].parse().unwrap();
    let passes: u64 = args[2].parse().unwrap();
    if bytes < 1024 || bytes % 64 != 0 || passes == 0 {
        panic!("footprint must be a nonzero multiple of 64 bytes");
    }
    let count = bytes / 64;
    if count % 8 != 0 {
        panic!("node count must divide eight lanes");
    }
    let mut nodes = vec![Node { next: 0, value: 0, padding: [0; 56] }; count];
    let mut order: Vec<u32> = (0..count as u32).collect();
    shuffle(&mut order);
    let mut payload_state = 20_260_910u64;
    for i in 0..count {
        nodes[order[i] as usize].next = order[(i + 1) % count];
        nodes[order[i] as usize].value = splitmix64(&mut payload_state) as u32;
    }
    let operations = count as u64 * passes;
    let start = order[0];
    // Run once before PMU to obtain a checksum for this exact seeded cycle.
    let expected = traverse(&nodes, start, operations);

    let mut scope = PmuScope { leader_fd: -1, member_fds: [-1, -1] };
    let mut pmu = PmuResult::default();
    unsafe {
        if scan_pmu_open(&mut scope) != 0 || scan_pmu_start(&scope) != 0 {
            panic!("could not start PMU");
        }
    }
    let begun = unsafe { scan_clock_now_ns() };
    let checksum = traverse(&nodes, start, operations);
    let elapsed = unsafe { scan_clock_now_ns() } - begun;
    let available = unsafe {
        if scan_pmu_stop(&scope, &mut pmu) != 0 {
            panic!("could not read PMU");
        }
        let result = scan_pmu_is_available(&scope) != 0;
        scan_pmu_close(&mut scope);
        result
    };
    if checksum != expected || !available || pmu.time_enabled == 0 ||
       pmu.time_enabled != pmu.time_running {
        panic!("invalid checksum or multiplexed PMU interval");
    }
    println!(
        "bytes={bytes},passes={passes},operations={operations},elapsed_ns={elapsed},\
         checksum={checksum},expected_checksum={expected},pmu_cycles={},\
         pmu_instructions={},pmu_ref_cycles={},pmu_time_enabled={},\
         pmu_time_running={},pmu_available={}",
        pmu.cycles, pmu.instructions, pmu.ref_cycles, pmu.time_enabled,
        pmu.time_running, available as u8
    );
}
