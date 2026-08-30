# Automated RemoteProc Test Suite Guide

This document provides complete instructions for executing the automated RemoteProc test suite on the Allwinner T527 / Radxa Cubie A5E board using the [`remoteproc_control.py`](../tools/remoteproc_control.py) utility.

---

## 1. Test Suite Overview

The test suite validates the complete software and hardware lifecycle of the XuanTie E906 RISC-V coprocessor running under the Linux 7.1 RemoteProc subsystem.

### Automated Test Matrix

| # | Test Name | Firmware ELF | What It Validates | Pass Condition |
|---|---|---|---|---|
| 1 | **`timer`** | [`timer_test.elf`](../apps/timer_test/src/main.c) | Hardware CLINT counter register access at `0xE0000000` and plain-text `RSC_TRACE` debugfs piping. | Alternating `timer_test: alive A` / `alive B` trace output observed within timeout. |
| 2 | **`smoke`** | [`platform_smoke_test.elf`](../apps/platform_smoke_test/src/main.cpp) | GPIO pinmux configurations and header pin driving on all 6 RISC-V owned pins (`PL2`, `PL3`, `PL6`, `PL9`, `PL10`, `PL12`). | Trace narrative `platform smoke test starting pin pulse sweep` observed. |
| 3 | **`hello`** | [`hello_world.elf`](../apps/hello_world/src/main.cpp) | AbstractX C++20 coroutine cooperative scheduler, zero-allocation task pool, and MTIME software timer coroutine delays. | Trace output `AbstractX Heartbeat (seq=...)` observed. |
| 4 | **`ipc`** | [`ipc_benchmark.elf`](../apps/ipc_benchmark/src/main.cpp) | SPSC descriptor queues in SRAM C (`0x07130000`), DRAM bulk buffers (`0x48100000`), and 1 kHz mailbox interrupts. | Trace output `IPC Benchmark: starting sender coro` observed. |
| 5 | **`fault`** | [`fault_test.elf`](../apps/fault_test/src/main.cpp) | Hardware exception capture: deliberate trap is intercepted by [`riscv_trap_handler.cpp`](../hal/src/riscv_trap_handler.cpp), logging CSRs (`mcause`, `mepc`, `mtval`), publishing `State::Crashed`, and ringing doorbell `0xA3`. | Trace output `FATAL TRAP:` with decoded CSR dumps observed. |
| 6 | **`ping`** | [`ipc_ping_test.elf`](../apps/ipc_ping_test/src/main.cpp) | Bidirectional host-coprocessor IPC: listens on `g_rx_ring` in SRAM C, attaches firmware timestamps from `HardwareTimer::get_time_us()`, echoes to `g_tx_ring`, and rings mailbox doorbell `0x01`. | Trace output `ipc_ping_test: ready` observed. |

---

## 2. Deploying to the Board

### Step 1: Cross-Compile Firmware Images
On your development workstation:
```bash
# Build all firmware binaries with xPack RISC-V GCC
cmake -S . -B build-rv -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-toolchain.cmake -DBUILD_RISCV_FIRMWARE=ON
cmake --build build-rv -j$(nproc)
```

### Step 2: Transfer Files to the Target Board
```bash
# Copy compiled ELF binaries to the target system's firmware directory
scp build-rv/*.elf root@<target-ip>:/lib/firmware/

# Copy the Python test suite CLI utility
scp tools/remoteproc_control.py root@<target-ip>:/usr/local/bin/
```

---

## 3. Running the Automated Test Suite

### Method 1: Single-Command Automated Run (`run-all`)

Execute all tests in sequence with a single command. The tool will automatically stop the core, switch the firmware ELF, start the core, poll for expected trace patterns, record execution duration, and generate a final summary table:

```bash
python3 /usr/local/bin/remoteproc_control.py run-all
```

#### Example Output:
```text
================================================================================
Starting Automated RemoteProc Test Suite on /sys/class/remoteproc/remoteproc0
Firmware Directory: /lib/firmware
Trace Source:       /sys/kernel/debug/remoteproc/remoteproc0/trace0
================================================================================

--- Running Test: [TIMER] (timer_test.elf) ---
Description: CLINT counter and text-only RemoteProc trace buffer
timer_test: alive A (mtime=0x016e3600)
[PASS] timer (1.02s)

--- Running Test: [SMOKE] (platform_smoke_test.elf) ---
Description: GPIO pulse burst walking test with trace narrative
[riscv] platform smoke test starting pin pulse sweep
[PASS] smoke (0.85s)

--- Running Test: [HELLO] (hello_world.elf) ---
Description: C++20 coroutine scheduler, MTIME timer service, and trace log
[riscv] hello_world: AbstractX Heartbeat (seq=1)
[PASS] hello (1.10s)

--- Running Test: [IPC] (ipc_benchmark.elf) ---
Description: SRAM ring buffer and DRAM payload streaming benchmark
[riscv] ipc_benchmark: IPC Benchmark starting sender coro
[PASS] ipc (0.90s)

--- Running Test: [FAULT] (fault_test.elf) ---
Description: Controlled exception trap, CSR capture, and crash reporting
[riscv] fault_test: boot successful, trace initialized
[riscv] FATAL TRAP: store/AMO address misaligned
[PASS] fault (0.50s)

--- Running Test: [PING] (ipc_ping_test.elf) ---
Description: Bidirectional IPC ping-pong test over SRAM rings
[riscv] ipc_ping_test: ready, starting ping responder
[PASS] ping (0.80s)

================================================================================
                      REMOTEPROC TEST SUITE SUMMARY REPORT                      
================================================================================
TEST NAME    | FIRMWARE                   | STATUS   | DURATION   | NOTES
--------------------------------------------------------------------------------
timer        | timer_test.elf             | PASS     |    1.02s   | Expected trace matched
smoke        | platform_smoke_test.elf    | PASS     |    0.85s   | Expected trace matched
hello        | hello_world.elf            | PASS     |    1.10s   | Expected trace matched
ipc          | ipc_benchmark.elf          | PASS     |    0.90s   | Expected trace matched
fault        | fault_test.elf             | PASS     |    0.50s   | Expected trace matched
ping         | ipc_ping_test.elf          | PASS     |    0.80s   | Expected trace matched
================================================================================
>> ALL TESTS PASSED SUCCESSFULLY! <<
```

---

## 4. Running Individual Diagnostic Tests

### Running a Specific Test Target
```bash
python3 /usr/local/bin/remoteproc_control.py run-test \
    --test timer \
    --firmware-dir /lib/firmware \
    --trace-source /sys/kernel/debug/remoteproc/remoteproc0/trace0 \
    --timeout 10
```

### Measuring Hardware Timer Counter Accuracy & Drift
Measures elapsed RISC-V `mtime` ticks against the host Linux monotonic clock over multiple 1-second sample intervals:
```bash
# 1. Start the timer firmware
python3 /usr/local/bin/remoteproc_control.py load --firmware timer_test.elf

# 2. Run the drift measurement tool
python3 /usr/local/bin/remoteproc_control.py test-timer-accuracy \
    --trace-source /sys/kernel/debug/remoteproc/remoteproc0/trace0 \
    --samples 10 \
    --interval 1.0

# 3. Stop firmware
python3 /usr/local/bin/remoteproc_control.py stop
```

### Inspecting Crash Status & CSRs After a Fault
After running `fault_test.elf`, decode the 64-byte boot status record published to shared SRAM (`0x07130000`):
```bash
python3 /usr/local/bin/remoteproc_control.py read-status --device /dev/riscv-boot-status
```

---

## 5. Command Reference

| Command | Arguments | Description |
|---|---|---|
| `run-all` | `[--firmware-dir DIR] [--trace-source PATH] [--timeout SEC]` | Sequentially runs all registered firmware tests and prints a consolidated summary report. |
| `run-test` | `--test NAME --firmware-dir DIR --trace-source PATH` | Runs a single test and reports pass/fail. |
| `list-tests` | *(none)* | Lists all available test targets and descriptions. |
| `test-timer-accuracy` | `--trace-source PATH [--samples N] [--interval SEC]` | Measures `mtime` counter frequency and clock drift against the host clock. |
| `read-status` | `--device PATH` | Decodes the 64-byte `BootStatus` structure (`state`, `failure_reason`, `mcause`, `mepc`, `mtval`). |
| `wait-ready` | `--device PATH [--trace-source PATH] [--timeout SEC]` | Polls the shared status record until the core reports `Ready` state or times out. |
| `watch-trace` | `--source PATH [--interval SEC] [--count N]` | Continuously tails changes to the `trace0` debugfs buffer. |
| `write-clock` | `--device PATH --generation N --riscv-core-hz HZ --timer-counter-hz HZ` | Writes and commits dynamic clock configurations before boot. |
| `self-test` | *(none)* | Validates Python struct packing against C++ ABI memory layout. |
