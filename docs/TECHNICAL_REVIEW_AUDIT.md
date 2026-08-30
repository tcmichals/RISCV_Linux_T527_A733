# RISC-V (E906) & RemoteProc Technical Architecture Review

**Date:** August 30, 2026  
**Scope:** Full codebase audit covering memory maps, linker scripts, HAL drivers, AbstractX coroutine domain bridge, interrupt safety, race conditions, and documentation alignment for the Allwinner T527 / Radxa Cubie A5E platform.

---

## 1. Executive Summary

A comprehensive architectural audit was conducted across the RISC-V firmware codebase, HAL drivers, AbstractX framework, linker scripts, and technical documentation.

### Summary of Findings
| Severity | Category | Issue | Impact |
|---|---|---|---|
| **CRITICAL** | Linker / Memory Map | **Trace Buffer Address Discrepancy** (`0x07136000` vs `0x07138000`) | `timer_test.elf` writes to `0x07138000` while Linux RemoteProc debugfs reads `0x07136000`, causing empty/silent trace output on hardware. |
| **HIGH** | Interrupt Dispatch | **Missing Peripheral CLIC Dispatchers** in `riscv_trap_handler.cpp` | `SUart0` (CLIC 66), `SSpi` (CLIC 76), `STwi2` (CLIC 60), and `SPI0` (CLIC 92) fall into `default: hal::Clic::disable()` when IRQs are enabled. |
| **HIGH** | Concurrency / Driver | **Unsynchronized Multi-Transfer on SPI** (`hal_spi_dma.hpp`) | Multiple coroutines calling `async_transfer()` on the same SPI bus overwrite hardware registers (`MBC`, `TCR`, `TXD`) without mutual exclusion. |
| **MEDIUM** | Race Condition | **Unprotected Slot Allocation in `GenericTimerService`** (`abstractx/timer.hpp`) | `await_suspend()` iterates and mutates `slot_active_` without `InterruptLock`, racing with `HardwareTimer::handle_isr()` calling `tick()`. |
| **MEDIUM** | Timing / Accuracy | **Cumulative Drift in `HardwareTimer`** (`hal_timer.hpp`) | `arm_next_tick()` computes `target = read_mtime() + period` rather than advancing `mtimecmp_target_ += period`, causing phase slip under interrupt delay. |
| **MEDIUM** | Cache Coherency | **D-Cache vs Shared Memory Coherence** (PMA CSRs / `RISCV_SYSMAP`) | Enabling E906 D-cache without programming non-cacheable attributes on SRAM C (`0x07130000`) will cause silent IPC data corruption. |
| **LOW** | Doc Alignment | **Outdated Notes in Memory Map and Device Tree Docs** | `SOC_MEMORY_MAP_REFERENCE.md` marks `0x07130000` as "wrong", predating the dual-address-space discovery; DT guide references `0x07138000`. |

---

## 2. Deep Dive Technical Findings

### 2.1 [CRITICAL] Trace Buffer Base Address Collision

#### The Bug:
There is an 8 KB address mismatch across the linker script, resource table, and C header:
1. In [`hal/include/memory_map.h:35`](../hal/include/memory_map.h#L35):
   ```c
   #define IPC_TRACE_BUFFER_ADDR   0x07136000U /* BareCTF trace buffer (32 KB) */
   #define IPC_TRACE_BUFFER_SIZE   0x00008000U
   ```
2. In [`hal/src/resource_table.c:42`](../hal/src/resource_table.c#L42):
   ```c
   .da = IPC_TRACE_BUFFER_ADDR, /* 0x07136000 */
   ```
3. In [`hal/src/riscv_memory_map.ld:136`](../hal/src/riscv_memory_map.ld#L136):
   ```ld
   .trace_ctf_buffer 0x07138000 (NOLOAD) : {
       *(.trace_ctf_buffer)
   } > SRAM_SHARED
   ```
4. In [`apps/timer_test/src/main.c:15`](../apps/timer_test/src/main.c#L15):
   ```c
   __attribute__((section(".trace_ctf_buffer")))
   volatile char g_trace_buffer[128];
   ```

#### Root Cause & Impact:
* RemoteProc initializes `/sys/kernel/debug/remoteproc/remoteproc0/trace0` from the resource table `da` (`0x07136000`).
* `timer_test.elf` links `g_trace_buffer` at `0x07138000`.
* When `timer_test` executes `write_trace()`, it writes to `0x07138000`. When Linux reads `trace0`, it reads `0x07136000` (which remains `0x00`).
* **Fix:** Align `riscv_memory_map.ld` to `.trace_ctf_buffer 0x07136000 (NOLOAD) :` and update documentation references.

---

### 2.2 [HIGH] Missing Peripheral CLIC Dispatch in Default Trap Handler

#### The Bug:
In [`hal/src/riscv_trap_handler.cpp:79-99`](../hal/src/riscv_trap_handler.cpp#L79-L99):
```cpp
void dispatch_external(uint32_t clic_id) noexcept {
    switch (static_cast<hal::ClicIrq>(clic_id)) {
        case hal::ClicIrq::GpioL_S:
        case hal::ClicIrq::GpioL_NS:
            hal::Gpio::handle_bank_isr(hal::Gpio::PORT_L);
            break;
        case hal::ClicIrq::GpioM_S:
        case hal::ClicIrq::GpioM_NS:
            hal::Gpio::handle_bank_isr(hal::Gpio::PORT_M);
            break;
        case hal::ClicIrq::RiscvMsgbox:
        case hal::ClicIrq::CpusMsgboxRiscv:
            hal::MsgboxDriver::handle_isr();
            break;
        default:
            hal::Clic::disable(clic_id); // Disables UART, SPI, TWI interrupts!
            hal::Trace::put_dec("[riscv] unhandled CLIC irq ", clic_id);
            break;
    }
}
```

#### Impact:
* If interrupts are enabled for `SUart0` (CLIC 66), `SSpi` (CLIC 76), `STwi2` (CLIC 60), or `SPI0` (CLIC 92 via GIC group), the trap handler treats them as unhandled, immediately disables them in the CLIC, and logs an error.
* **Fix:** Add switch cases for `SUart0`, `SSpi`, `STwi0..2`, and `GicGroup48_55` routing to their respective HAL driver ISR entry points.

---

### 2.3 [HIGH] Unsynchronized Concurrent Transfers in `SpiDmaDriver`

#### The Bug:
In [`hal/include/hal_spi_dma.hpp:152-156`](../hal/include/hal_spi_dma.hpp#L152-L156):
```cpp
void await_suspend(std::coroutine_handle<> h) noexcept {
    handle = h;
    queue_transfer_request(spi_base, h, tx_data, rx_data);
    start_transfer(spi_base, tx_data, rx_data.size()); // Immediately touches MMIO!
}
```

#### Impact:
* `queue_transfer_request` correctly places the request onto `pending_spi0_requests_`.
* However, `start_transfer` directly configures `regs->MBC`, `regs->TXD`, and `regs->TCR |= (1 << 31)` immediately.
* If Coroutine A initiates an SPI transfer, and Coroutine B initiates an SPI transfer before A completes, B corrupts the active hardware transmission.
* Furthermore, `complete_transfer_from_isr` does not check the queue to pop and trigger the next transfer.
* **Fix:** Either:
  1. `start_transfer` should only be called if the queue was previously empty (hardware idle); in `complete_transfer_from_isr`, after popping the completed request, peek and start the next queued request.
  2. Or enforce an asynchronous mutex (`abstractx::AsyncMutex`) around SPI transactions.

---

### 2.4 [MEDIUM] Race Condition in `GenericTimerService::await_suspend`

#### The Bug:
In [`third_party/AbstractX/include/abstractx/timer.hpp:52-67`](../third_party/AbstractX/include/abstractx/timer.hpp#L52-L67):
```cpp
void await_suspend(std::coroutine_handle<> h) noexcept {
    handle = h;
    for (size_t i = 0; i < MaxTimers; ++i) {
        if (!slot_active_[i]) {
            slot_active_[i] = true;
            slot_handles_[i] = handle;
            allocated_slot = static_cast<int8_t>(i);
            auto cb = get_callback(i);
            timer_id = timer_backend_.register_timer(cb, period_ms, etl::timer::mode::SINGLE_SHOT);
            if (timer_id != etl::timer::id::NO_TIMER) {
                timer_backend_.start(timer_id);
            }
            break;
        }
    }
}
```

#### Impact:
* `slot_active_` and `slot_handles_` are plain static arrays.
* The hardware timer ISR calls `GenericTimerService::tick()`, which calls `timer_expired_slot()`, which executes `slot_active_[Slot] = false;`.
* If a coroutine enters `await_suspend` and is preempted by the timer ISR mid-iteration, concurrent mutation of `slot_active_` and `timer_backend_.register_timer()` occurs.
* **Fix:** Wrap the body of `await_suspend` in `abstractx::InterruptGuard guard{}`.

---

### 2.5 [MEDIUM] Cumulative Phase Drift in `HardwareTimer::arm_next_tick`

#### The Bug:
In [`hal/include/hal_timer.hpp:101-106`](../hal/include/hal_timer.hpp#L101-L106):
```cpp
static inline void arm_next_tick() noexcept {
#if defined(__riscv)
    const uint64_t target = read_mtime() + period_cycles_;
    write_mtimecmp(target);
#endif
}
```

#### Impact:
* In standard timer loops, computing `target = read_mtime() + period_cycles_` resets the period relative to the *instant of ISR execution* rather than the *scheduled tick deadline*.
* Any interrupt latency or temporary interrupt masking introduces non-recoverable cumulative time loss (clock drift).
* **Fix:** Maintain a static `uint64_t next_tick_mtime_` deadline, and increment `next_tick_mtime_ += period_cycles_` on each tick.

---

### 2.6 [MEDIUM] Memory Coherency & Cache Allocation (PMA / SYSMAP)

#### Analysis:
* The XuanTie E906 implements a Harvard cache architecture (I-cache + D-cache).
* **SRAM C (`0x07130000`)** contains the lock-free SPSC rings (`g_tx_ring`, `g_rx_ring`), boot status, clock configuration, and `RSC_TRACE`. Linux directly writes/reads this memory via PCIe/AXI without passing through the RISC-V D-cache.
* **DRAM Payload Pool (`0x48100000`)** is targeted by DMA.
* **Current Status:** D-cache is currently left disabled in `startup.S` (`mcor`/`mhcr`).
* **Requirement for Cache Enablement:**
  Before enabling D-cache, the memory regions `0x07130000..0x0713FFFF` (SRAM C) and `0x48100000..0x48FFFFFF` (DRAM payloads) MUST be configured as **Cache-Inhibited / Bufferable** in the XuanTie Physical Memory Attribute (PMA) / `RISCV_SYSMAP` (`0xEFFFF000`) registers.

---

### 2.7 [LOW] Documentation vs Code Alignment Matrix

| Document | Stated Property | Actual Implementation / Verified Behavior | Action Needed |
|---|---|---|---|
| `docs/SOC_MEMORY_MAP_REFERENCE.md:314` | `0x0713_0000` is "WRONG — collides with registers" | Dual address space verified: CPUX sees `RV_CFG`, RISC-V sees SRAM C. | Update document to reflect dual-address space discovery. |
| `docs/LINUX_DEVICE_TREE_GUIDE.md:37` | Trace reserved memory: `0x07138000 0x8000` | Code uses `IPC_TRACE_BUFFER_ADDR` (`0x07136000`). | Update device tree guide to `0x07136000 0x8000`. |
| `docs/BARECTF_SCHEDULER_TRACING.md:78` | Trace buffer: `0x07138000` | Code uses `0x07136000`. | Update to `0x07136000`. |
| `hal/include/hal_uart_dma.hpp:185` | `get_regs()` hardcodes `UART2_BASE` (`0x02500800`) | Main-domain UART2; `S_UART0` is `0x07080000`. | Parameterize base address in driver. |

---

## 3. Interrupt Safety & ISR Reentrancy Audit

1. **Global Interrupt Disabling (`mstatus.MIE`)**:
   * RISC-V hardware automatically clears `mstatus.MIE` upon taking any trap or interrupt.
   * Interrupts remain disabled throughout `_trap_entry` and `riscv_trap_handler()`, preventing nested ISR corruption.
   * `mret` restores `mstatus.MIE` from `mstatus.MPIE`.

2. **`abstractx::InterruptLock` Contract**:
   * Evaluated `disable_interrupts_save_flags()`: uses `csrrc %0, mstatus, 8` with a compiler barrier (`"memory"`).
   * Outer lock saves previous flags; matching unlock restores flags when `depth_ == 0`.
   * **Result: Verified single-core reentrant and deterministic.**

3. **Domain Queue Access Separation**:
   * **Coroutine Domain:** calls `push()` / `pop()` (masks interrupts via `InterruptLock`).
   * **ISR Domain:** calls `push_from_isr()` / `pop_from_isr()` (unlocked, assumes interrupts already masked).
   * **Result: 100% compliant with AbstractX Domain Rules.**

4. **Stack Headroom**:
   * Stack size is explicitly bounded to 4.0 KB in DTCM.
   * Trap entry reserves 64 bytes for register context. Total ISR depth is < 256 bytes, leaving > 3.7 KB headroom.

---

## 4. Remediation Checklist & Recommendations

- [ ] **1. Fix Linker Script Trace Offset:**
  Change `hal/src/riscv_memory_map.ld` line 136 from `0x07138000` to `0x07136000`.
- [ ] **2. Complete CLIC External Dispatch:**
  Add `SUart0`, `SSpi`, `STwi2`, and `GicGroup48_55` cases to `dispatch_external()` in `hal/src/riscv_trap_handler.cpp`.
- [ ] **3. Guard Timer Service Slot Allocation:**
  Add `InterruptGuard` inside `GenericTimerService::AsyncSleepAwaiter::await_suspend()` in `third_party/AbstractX/include/abstractx/timer.hpp`.
- [ ] **4. Eliminate Timer Phase Drift:**
  Refactor `HardwareTimer::arm_next_tick()` to accumulate deadlines against a persistent `next_tick_mtime_` variable.
- [ ] **5. Serialize SPI Driver Requests:**
  Gate hardware transfer start in `hal_spi_dma.hpp` so pipelined requests await the previous transfer's TC interrupt.
- [ ] **6. Update Documentation Cross-References:**
  Sync `LINUX_DEVICE_TREE_GUIDE.md`, `BARECTF_SCHEDULER_TRACING.md`, and `SOC_MEMORY_MAP_REFERENCE.md` with current verified memory offsets.
