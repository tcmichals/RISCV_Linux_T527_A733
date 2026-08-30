# RISC-V Firmware Bring-Up Guide (T527 / E906)

How the firmware boots, how interrupts are routed, how faults are reported, and
how to verify each of those from Linux.

Companion documents:
- [SOC_MEMORY_MAP_REFERENCE.md](SOC_MEMORY_MAP_REFERENCE.md) — addresses, CLIC table, pin plan
- [IO_PROCESSOR_DRIVER_MODEL.md](IO_PROCESSOR_DRIVER_MODEL.md) — split-transaction driver model

---

## 1. Memory layout

Two distinct address spaces exist and must not be confused:

| Address | RISC-V core sees | Linux / CPUX sees |
| --- | --- | --- |
| `0x0713_0000` | **SRAM C** (start of the 320 KB block) | `RV_CFG` registers |

The vendor manual documents only the CPUX view; the RISC-V view is established by
the firmware that runs on hardware. Firmware links against the RISC-V view.

| Region | Base | Size | Contents |
| --- | --- | --- | --- |
| ITCM | `0x0000_0000` | 64 KB | vectors + `.fastcode` trap entry |
| DTCM | `0x0008_0000` | 64 KB | `.data`, `.bss`, 4 KB reserved stack |
| SRAM C low | `0x0713_0000` | 64 KB | IPC + trace (Linux visible) |
| SRAM C code | `0x0714_0000` | 256 KB | `.text`, coroutine frame pool |
| DRAM init | `0x4800_0000` | 1 MB | load image, discarded after relocation |
| DRAM buffers | `0x4810_0000` | 15 MB | DMA payload pool |

### 1.1 SRAM C shared window

| Address | Size | Contents |
| --- | --- | --- |
| `0x0713_0000` | 4 KB | boot / crash status |
| `0x0713_1000` | 4 KB | clock configuration (Linux -> firmware) |
| `0x0713_2000` | 8 KB | IPC TX ring (RISC-V -> Linux) |
| `0x0713_4000` | 8 KB | IPC RX ring (Linux -> RISC-V) |
| `0x0713_6000` | 32 KB | `RSC_TRACE` text buffer |

Ends exactly at the code window. Defined in
[memory_map.h](../hal/include/memory_map.h); the resource table derives its
`da`/`len` from the same macros so the two cannot drift.

Check actual usage with:

```sh
python3 tools/fw_size.py build-rv/hello_world.elf
```

It reports per-region usage, the entry point, LOAD segments, and stack headroom,
and exits non-zero on overflow, >90% use, a missing resource table, or under
4 KB of stack.

## 2. Boot sequence

1. RemoteProc writes the ELF entry point to `RV_CFG + 0x0204`
   (`RISCV_STA_ADD_REG`) and releases reset. The board DT sets `auto-boot`.
2. `_start` ([startup.S](../hal/src/startup.S)) masks interrupts, sets `sp` to
   `_estack` and `gp`, then relocates `.vectors`/`.fastcode` into ITCM, `.text`
   into SRAM C, and `.data` into DTCM, and zeroes `.bss`.
3. `mtvec` is pointed at the ITCM vector table.
4. `fence.i` — mandatory, because the relocation loops wrote instructions as
   data. Without it the core may execute stale prefetched instructions.
5. Optional I-cache enable, gated by `ENABLE_XUANTIE_ICACHE` (**default OFF**;
   the `mhcr`/`mcor` encodings are unverified on this silicon).
6. `__libc_init_array`, then `main`.

## 3. Interrupts

The E906 has **no PLIC**. Everything arrives through the CLIC at `0xE080_0000`.

### 3.1 Trap entry

`_trap_entry` lives in `.fastcode` (ITCM) and saves all 16 caller-saved
registers before calling C++, then restores them and issues `mret`. This is
mandatory: `riscv_trap_handler` is ordinary C++ and will otherwise corrupt
`ra`/`t0-t6`/`a0-a7` of the interrupted coroutine.

### 3.2 Dispatch

[riscv_trap_handler.cpp](../hal/src/riscv_trap_handler.cpp) splits on
`mcause[31]`:

- interrupt, code 7 -> `HardwareTimer::handle_isr()`
- interrupt, other -> `dispatch_external(clic_id)`
- exception -> crash path (§4)

`dispatch_external` routes CLIC IDs to drivers. An unclaimed source is
**disabled** rather than left to storm, and logged to the trace buffer.

### 3.3 Enabling a source

```cpp
hal::Clic::init();
hal::Clic::enable(hal::ClicIrq::GpioL_NS, /*priority=*/3);
```

`CLIC_INT_REGn` is at `0x1000 + n*4` with `IP@0`, `IE@8`, vector@16,
`TRIG@18:17`, `MODE@23:22` (always machine mode), `PRIO@31:27`.

### 3.4 Main-domain peripherals

`SPI0`, `UART2` and the main PIO banks have **no dedicated CLIC line**. They
reach the core only as grouped GIC interrupts via `S_INTC`:

```cpp
hal::SharedGicGroup::enable_for_gic_irq(48);                        // SPI0
hal::Clic::enable(hal::SharedGicGroup::clic_irq_for_gic_irq(48));   // -> CLIC 92
```

Group *n* covers 8 GIC sources; the ISR must demux by reading GIC pending state.
MCU-domain peripherals (`S_UART0` 66, `S_TWI2` 60, `S_SPI` 76, GPIOL/M 62-65)
have direct lines and are strongly preferred.

### 3.5 GPIO

A CLIC interrupt identifies the **bank**, not the pin, so the entry point is
`handle_bank_isr(port)`. It acknowledges only bits this core enabled:

```cpp
uint32_t pending = int_regs->STATUS & int_regs->CTL;
int_regs->STATUS = pending;   // W1C, leaves other owners' bits intact
```

That discipline is what allows a bank to be shared with Linux. Two ways to
consume an edge:

```cpp
hal::Gpio::register_pin_isr(PORT_L, 6, &on_drdy, ctx, IntTrigger::RISING_EDGE);
co_await hal::Gpio::wait_for_edge(PORT_L, 6);
```

## 4. Fault reporting

On any exception the handler:

1. Writes a decoded report to the `RSC_TRACE` buffer — exception name plus
   `mcause`, `mepc`, `mtval`, `mstatus`.
2. Publishes a `hal::boot::Status` record with the same CSRs.
3. Rings the crash doorbell so the remoteproc driver calls
   `rproc_report_crash(rproc, RPROC_FATAL_ERROR)`.
4. Halts in `wfi`.

Read it from Linux:

```sh
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
dmesg | grep -i rproc
```

Example output:

```
[riscv] FATAL TRAP: store/AMO access fault
[riscv]   mcause  = 0x00000007
[riscv]   mepc    = 0x07141a2c
[riscv]   mtval   = 0x02000010
[riscv]   mstatus = 0x00001880
[riscv] halted, notifying host
```

## 5. Trace buffer

`RSC_TRACE` is mainline (`rproc_handle_trace` exports it to debugfs). It is the
cheapest way to prove the firmware is alive, and needs no mailbox or ring setup.

```cpp
hal::Trace::init();                        // clears buffer, writes banner
hal::Trace::puts("[riscv] clocks ok\n");
hal::Trace::put_hex32("[riscv] core_hz = ", freq);
hal::Trace::put_dec("[riscv] ticks = ", count);
```

ISR-safe (masks interrupts), no `printf` dependency, wraps at 32 KB, always
null-terminated so `cat` is well behaved. Intended for boot progress and faults
only — high-rate structured tracing belongs in BareCTF.

## 6. Verifying on hardware

```sh
# load / start / stop
python3 tools/remoteproc_control.py --help

# is it alive?
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0

# state and crash reports
cat /sys/class/remoteproc/remoteproc0/state
dmesg | grep -i rproc
```

`platform_smoke_test.elf` walks every RISC-V-owned pin with an identifying pulse
burst, so the 40-pin header can be mapped with a scope while the trace buffer
narrates which pin is active.

## 7. Known gaps

| Item | Status |
| --- | --- |
| I-cache / D-cache enable | Not done. `mhcr`/`mcor` encodings unverified. |
| PMA / `RISCV_SYSMAP` (`0xEFFF_F000`) | Not programmed. Required before D-cache: the IPC rings and DMA buffers must be non-cacheable. |
| CLIC hardware vectoring | Not used; direct `mtvec` mode only. |
| Core clock | `600 MHz` is a bring-up fallback. Real value must come from the clock handshake or be measured via `mcycle`/`mtime`. |
| UART baud divisor | Hardcoded 24 MHz APB assumption. |
| `SPI0` per-transfer mode | `TransferRequest` has no mode field; needed to alternate dual-IO (FPGA) and single-IO (IMU). |
| 40-pin header routing | `gpioinfo` proves which pins are unused, not which are routed. Needs the Radxa pinout. |
| A733 | No RISC-V remoteproc node exists in the BSP; only `a55_rproc`. |
