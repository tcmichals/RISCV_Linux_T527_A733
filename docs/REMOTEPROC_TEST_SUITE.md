# RemoteProc & RISC-V Test Suite Checkout Guide

This guide details the standalone test firmware applications, host verification tools, and step-by-step checkout procedures for testing the XuanTie E906 RISC-V coprocessor and Linux 7.1 RemoteProc subsystem on Allwinner T527 (`sun55iw3`) and Radxa Cubie A5E hardware.

---

## 1. Test Suite Matrix

| Test Target | Firmware Binary | Focus Area | Expected Result / Pass Condition |
|---|---|---|---|
| **`timer`** | [`timer_test.elf`](../apps/timer_test/src/main.c) | CLINT `mtime` read & trace stream | Alternates `timer_test: alive A` / `alive B` once per second in `trace0`. |
| **`timer-accuracy`** | [`timer_test.elf`](../apps/timer_test/src/main.c) | Clock counter accuracy & drift | Calculates exact counter frequency (e.g. 24 MHz) and host clock drift (ppm). |
| **`fault`** | [`fault_test.elf`](../apps/fault_test/src/main.cpp) | Exception & crash reporting | Trap handler captures exception; prints `mcause`/`mepc`/`mtval` to `trace0`; publishes `State::Crashed`; rings doorbell `0xA3`. |
| **`ping`** | [`ipc_ping_test.elf`](../apps/ipc_ping_test/src/main.cpp) | Bidirectional SRAM IPC | Echoes packets from `g_rx_ring` (`0x07134000`) to `g_tx_ring` (`0x07132000`) with microsecond timestamps; rings doorbell `0x01`. |
| **`smoke`** | [`platform_smoke_test.elf`](../apps/platform_smoke_test/src/main.cpp) | GPIO pinmux & header routing | Emits identifying pulse bursts on each RISC-V pin while narrating pin names to `trace0`. |
| **`hello`** | [`hello_world.elf`](../apps/hello_world/src/main.cpp) | C++20 coroutines & MTIME timer | Runs 1 Hz heartbeat and 100 Hz sensor coroutine loops simultaneously without dynamic memory. |
| **`ipc`** | [`ipc_benchmark.elf`](../apps/ipc_benchmark/src/main.cpp) | SRAM ring + DRAM payload stream | Streams 256-byte bulk payload descriptors from SRAM into DRAM at 1 kHz. |

---

## 2. Host Build & Verification

Run these commands on the development host to cross-compile all test targets and verify memory layout budgets:

```bash
# 1. Build all firmware targets using xPack RISC-V GCC
cmake -S . -B build-rv -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-toolchain.cmake -DBUILD_CPPUTEST_TESTS=OFF -DBUILD_RISCV_FIRMWARE=ON
cmake --build build-rv -j$(nproc)

# 2. Verify memory segment placement and stack headroom (DTCM stack >= 4 KB)
python3 tools/fw_size.py build-rv/timer_test.elf
python3 tools/fw_size.py build-rv/fault_test.elf
python3 tools/fw_size.py build-rv/ipc_ping_test.elf
python3 tools/fw_size.py build-rv/platform_smoke_test.elf
python3 tools/fw_size.py build-rv/hello_world.elf
python3 tools/fw_size.py build-rv/ipc_benchmark.elf

# 3. Run host simulation unit test suite
cmake -B build-host -DBUILD_RISCV_FIRMWARE=OFF -DBUILD_CPPUTEST_TESTS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

---

## 3. Board Deployment (Radxa Cubie A5E)

Copy the compiled firmware binaries and the Python test utility to the target board:

```bash
# Copy all test firmware ELFs to the board's firmware repository
scp build-rv/*.elf root@<board-ip>:/lib/firmware/

# Copy the control and test runner script
scp tools/remoteproc_control.py root@<board-ip>:/usr/local/bin/
```

---

## 4. Test Checkout Procedures

Execute these tests directly on the board terminal (or via SSH):

### Test 1: CLINT Counter & 1 Hz Trace Stream (`timer`)

Tests basic ELF loading, CLINT `mtime` read, and the `RSC_TRACE` debugfs pipe:

```bash
python3 /usr/local/bin/remoteproc_control.py --remoteproc 0 run-test \
    --test timer \
    --firmware-dir /lib/firmware \
    --trace-source /sys/kernel/debug/remoteproc/remoteproc0/trace0 \
    --timeout 10
```

> **Expected Output:**
> ```text
> timer_test: alive A (mtime=0x016e3600)
> timer_test: alive B (mtime=0x02dc6c00)
> PASS: timer
> ```

---

### Test 2: Timer Counter Frequency & Drift Measurement (`timer-accuracy`)

Start `timer_test.elf` and compute the actual hardware counter rate against the Linux host monotonic clock:

```bash
# Start timer_test.elf
python3 /usr/local/bin/remoteproc_control.py --remoteproc 0 load --firmware timer_test.elf

# Measure accuracy over 10 consecutive 1-second samples
python3 /usr/local/bin/remoteproc_control.py test-timer-accuracy \
    --trace-source /sys/kernel/debug/remoteproc/remoteproc0/trace0 \
    --samples 10 \
    --interval 1.0

# Stop after test
python3 /usr/local/bin/remoteproc_control.py --remoteproc 0 stop
```

> **Expected Output:**
> ```text
> Measuring timer counter accuracy over 10 samples (interval 1.0s)...
>   [Sample 1/10] host=1024.102s, mtime=24000100
>   [Sample 2/10] host=1025.103s, mtime=48000210
>   ...
> --- Timer Accuracy Result ---
> Elapsed host time: 9.0082 s
> Elapsed mtime:     216001000 ticks
> Measured frequency: 24,000,012.45 Hz
> ```

---

### Test 3: Controlled Fault & Crash Reporting (`fault`)

Verifies that hardware exceptions trigger full CSR capture, `trace0` error dumps, and mailbox notifications rather than silent core lockups:

```bash
# Run fault test and leave running for inspection
python3 /usr/local/bin/remoteproc_control.py --remoteproc 0 run-test \
    --test fault \
    --firmware-dir /lib/firmware \
    --trace-source /sys/kernel/debug/remoteproc/remoteproc0/trace0 \
    --keep-running

# Read decoded 64-byte status record from shared SRAM
python3 /usr/local/bin/remoteproc_control.py read-status --device /dev/riscv-boot-status

# Stop core
python3 /usr/local/bin/remoteproc_control.py --remoteproc 0 stop
```

> **Expected Output:**
> ```text
> [riscv] fault_test: boot successful, trace initialized
> [riscv] fault_test: triggering controlled exception (unaligned store)
> 
> [riscv] FATAL TRAP: store/AMO address misaligned
> [riscv]   mcause  = 0x00000006
> [riscv]   mepc    = 0x07140084
> [riscv]   mtval   = 0x00000001
> [riscv]   mstatus = 0x00001880
> [riscv] halted, notifying host
> PASS: fault
> ```

---

### Test 4: Bidirectional IPC Ping-Pong (`ping`)

Tests full round-trip IPC across SRAM C descriptor rings:

```bash
python3 /usr/local/bin/remoteproc_control.py --remoteproc 0 run-test \
    --test ping \
    --firmware-dir /lib/firmware \
    --trace-source /sys/kernel/debug/remoteproc/remoteproc0/trace0 \
    --timeout 10
```

> **Expected Output:**
> ```text
> [riscv] ipc_ping_test: booting and initializing clocks
> [riscv] ipc_ping_test: ready, starting ping responder
> PASS: ping
> ```

---

### Test 5: GPIO Header Pin Routing (`smoke`)

Walks each RISC-V pin with pulse trains $(N+1)$ while logging to `trace0`:

```bash
python3 /usr/local/bin/remoteproc_control.py --remoteproc 0 load --firmware platform_smoke_test.elf
python3 /usr/local/bin/remoteproc_control.py watch-trace --source /sys/kernel/debug/remoteproc/remoteproc0/trace0 --interval 1
```

> **Expected Output:**
> ```text
> [riscv] platform smoke test starting pin pulse sweep
> [riscv] pulsing pin: PL2  S_UART0_TX
> [riscv] pulsing pin: PL3  S_UART0_RX
> [riscv] pulsing pin: PL6  IMU_DRDY
> [riscv] pulsing pin: PL9  FPGA_IRQ
> [riscv] pulsing pin: PL10 DEBUG0
> [riscv] pulsing pin: PL12 DEBUG1
> ...
> ```

---

## 5. Summary & Troubleshooting

* **Missing `/sys/kernel/debug/remoteproc/remoteproc0/trace0`**:
  Ensure debugfs is mounted: `mount -t debugfs none /sys/kernel/debug`.
* **State stays `offline` or fails to start**:
  Check `dmesg | grep -i rproc` for ELF loading or reserved memory mapping errors.
* **Clock Mismatch / Division Errors**:
  Use `remoteproc_control.py write-clock` to supply resolved CCU rates prior to starting the core.
