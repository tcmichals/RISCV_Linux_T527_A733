# Engineering Log: AbstractX & HAL for Allwinner T527 & A733 RISC-V Cores

## Project Identification
- **Workspace:** `RISCV_Linux_T527_A733`
- **Target SoC:** Allwinner T527 / A527 / A523 (`sun55iw3`)
  - A733 (`sun60iw2`) exposes no RISC-V remoteproc node in this BSP; see the
    2026-08-30 entry.
- **Target Core:** T-Head XuanTie **E906** RV32IMAC, **CLIC** at `0xE0800000`
  (there is no PLIC). Core clock unverified; 600 MHz is a bring-up fallback.
- **Host OS:** Linux 7.1+ with RemoteProc (`sunxi_rproc`), Mailbox (`sunxi_msgbox`), and debugfs trace
- **Frameworks:** AbstractX (C++20 stackless coroutines), Pigweed (tokenized logging), BareCTF (Common Trace Format binary trace), ETL (Embedded Template Library)
- **Compiler:** xPack RISC-V GCC (`riscv-none-elf-gcc`)

---

## Log Entries

### [2026-08-28] Initial Architecture & Project Scaffolding
- **Objective:** Extract and formalize a standalone, out-of-tree project for the RISC-V coprocessor firmware on Allwinner T527 and A733, removing coupling with buildroot build trees.
- **Hardware Analysis:**
  - Verified chip manuals: `T527_User_Manual_V0.92.pdf` and `A733_UserManual_V0.92.pdf`.
  - Identified core differences: T527 uses XuanTie E907 with PLIC; A733 uses XuanTie E902 with CLIC v0.8 and TIMERSTAMP/TIMER_GRAYDEC.
  - Confirmed UART RX DMA vs RTO (Receiver Timeout / Character Timeout): UART triggers Character Timeout interrupt (IIR ID `0b1100`) after 4 character times of wire idle with unread bytes in FIFO.
- **Memory Topology Decision:**
  - **ITCM (64 KB @ 0x00000000)**: Exception vector table, fast interrupt service routines (`.fastcode`).
  - **DTCM (64 KB @ 0x00080000)**: Stack pointer (`_estack` = 0x00090000), static atomic coroutine frame pools.
  - **SRAM C (Shared @ 0x07130000)**: SPSC ring buffer headers and descriptors, mailbox doorbells (1-cycle low-latency synchronization).
  - **Dedicated RISC-V SRAM (256 KB @ 0x07280000)**: Active C++20 coroutine execution engine (`.text.hot`).
  - **Off-Chip DRAM (Shared @ 0x48000000)**: One-time discardable initialization routines (`.init`), static C++ constructors, bulk DMA RX/TX buffers, and BareCTF/Pigweed trace storage.
- **Tooling & Build System:**
  - Added CMake toolchain configuration targeting `riscv-none-elf` with automatic xPack toolchain detection and download fallback.
  - Added visual memory region analysis tool (`tools/analyze_elf_regions.py`) and map analyzer (`tools/analyze_map.py`).

---

### [2026-08-30] Hardware Fact-Check, Domain Queue Migration, and Interrupt Bring-Up

**Objective:** Replace assumptions carried in the 2026-08-28 entry with facts from
the vendor manuals, the vendor BSP, and firmware known to run on T527 hardware.

#### Corrections to the 2026-08-28 entry

| Claim on 08-28 | Verified finding |
| --- | --- |
| T527 core is **E907 with PLIC** | **E906, and there is no PLIC.** UM 2.3 block diagram names the IP `openE906`; DT node is `e906_rproc@7130000`, driver `sunxi_rproc_e906_boot.c`. All external interrupts arrive via the CLIC at `0xE080_0000`. |
| A733 is a second RISC-V target | **A733 (sun60iw2) has no RISC-V remoteproc node** — only `a55_rproc`. No `device-a733` board dir. Its E902 is the always-on CPUS core. T527 is the only viable AMP target in this BSP. |
| Code executes from `0x0728_0000` | Firmware proven on hardware links `.text` at **`0x0714_0000`**. `0x0728_0000` (`SRAMA3_0`) was never validated. |
| `0x0713_0000` is plain shared SRAM | **Two address spaces.** RISC-V sees SRAM C there; CPUX sees the `RV_CFG` registers, including the boot-address register at `+0x204`. The manual documents only the CPUX view. |
| A733 shares the T527 address map | **Not address-compatible.** A733 uses 4 KB peripheral stride vs T527's 1 KB; `SPI0` is `0x0254_0000` vs `0x0402_5000`. |

Reference material extracted to [docs/SOC_MEMORY_MAP_REFERENCE.md](docs/SOC_MEMORY_MAP_REFERENCE.md)
so the PDFs need not be reopened. Note: these manuals use CID Type-0 fonts —
`pdftotext -layout` yields nothing, `pdftotext -raw` works.

#### Execution domain model

Formalized the coroutine domain / I/O (ISR) domain split on ETL rather than
hand-rolled rings:

- `hal::DriverRequestQueue<T,N>` is now `etl::queue_spsc_isr<T,N,IrqLock>`.
- `abstractx::InterruptLock` provides the ETL `lock()`/`unlock()` policy with
  nesting-aware interrupt save/restore.
- Direction is explicit at the call site: coroutine domain uses the locked
  `push()`/`pop()`; ISRs use `push_from_isr()`/`pop_from_isr()`.
- Fixed a real defect: the dispatcher drained with the *unlocked* `pop_from_isr()`
  from the main loop while an ISR could concurrently push.
- All drivers (GPIO, UART, SPI, Msgbox) previously filled their queues but never
  drained them — the ISR resumed a shadow handle instead. Now each ISR pops its
  request and completes it.

Committed to AbstractX as `9fa6d5c`; submodule relocated to `third_party/AbstractX`.

#### Register-level defects found and fixed

- **`_trap_entry` saved no registers.** Any interrupt corrupted `ra`/`t0-t6`/`a0-a7`
  of the interrupted coroutine. Now saves and restores all 16 caller-saved regs.
- **No `fence.i` after relocation.** `startup.S` copies code as data then jumps to
  it; without `fence.i` the core may execute stale instructions.
- **SPI register struct was contiguous.** `GCR` landed at `+0x00` instead of
  `+0x04`, `TXD`/`RXD` at `+0x2C`/`+0x30` instead of `0x200`/`0x300` — every access
  wrong. Rebuilt with explicit padding and `static_assert`s.
- **MSGBOX layout was invented.** Real layout is per-direction banks (`N*0x100`)
  with four queues each. RX now uses `RISCV_MSGBOX` `0x0713_6000` (CLIC 17).
- **GPIO `PULL` at wrong offset.** Struct had `DRV[2]`; hardware has `DRV0-3`, so
  `PULL` writes hit `DRV2`.
- **Coroutine pool consumed DTCM.** 60 KB of the 64 KB DTCM, leaving 2.4 KB of
  stack. Moved to `.coro_frame_pool` in SRAM C; DTCM now 9.2% with a 4 KB
  reserved `.stack` section.
- **Frame allocator wrapped on exhaustion**, silently handing a live coroutine's
  frame to a new one. Now returns `nullptr` so the allocation-failure path runs.
- **Stale resource table.** `da` was hardcoded `0x0713_8000` after the map moved;
  now derived from `memory_map.h`.
- **`linux_imu_demo` was cross-compiled** for bare metal inside the firmware block.

#### New components

- `hal_clic.hpp` — CLIC driver (`CLIC_INT_REGn @ 0x1000+n*4`), plus
  `SharedGicGroup` for main-domain peripherals that only reach the core as
  grouped GIC interrupts through `S_INTC`.
- `hal_twi.hpp` — I2C master for `S_TWI0/1/2` (CLIC 68/69/60), queued requests
  completed from the ISR.
- `hal_trace.hpp` — plain-text `RSC_TRACE` logger, readable at
  `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.
- `tools/fw_size.py` — per-region ELF usage, entry point, LOAD segments, stack
  headroom; exits non-zero on overflow or under 4 KB stack.
- Trap handler now decodes faults to the trace buffer, publishes boot status, and
  rings the crash doorbell so Linux calls `rproc_report_crash()`.

#### Peripheral / pin plan

`gpioinfo` on hardware settled ownership. PL0/PL1 are the **AXP PMIC bus** and
PL11 is **PCIe 3V3** — and `S-SPI0-CLK` is muxed *only* on PL11, so keeping PCIe
rules out MCU-domain SPI entirely.

| Function | Pins | Controller | CLIC |
| --- | --- | --- | --- |
| Console TX/RX | PL2, PL3 | `S_UART0` | 66 |
| I2C (baro/mag/ADC) | PM4, PM5 | `S_TWI2` | 60 |
| SPI CS0 single-IO (IMU) | PJ20-23 | `SPI0` | 92 (grouped) |
| SPI CS1 dual-IO (FPGA) | PJ24 | `SPI0` | 92 (grouped) |
| IMU data-ready | PL6 | `R_PIO` | 63 |
| Debug / scope | PL10, PL12 | `R_PIO` | — |

Linux keeps console `uart0` (PB9/PB10), PMIC, LEDs, regulators, PCIe, WiFi,
Ethernet, microSD. `&uart8` (`S_UART0`) is already `status = "disabled"`.

For GPIO the mainline-clean lever is `ignore-interrupts` on the `r_pio` node —
the vendor `sunxi_share_interrupt` per-pin table is out-of-tree and not
upstreamable (cross-subsystem `EXPORT_SYMBOL` into pinctrl, `BUG_ON` in an IRQ
handler, undocumented bindings).

#### State

All four firmware ELFs cross-compile and link cleanly with xPack GCC 15.2.0.
ITCM 0.4%, DTCM 8.7–9.2%, SRAM_CODE <= 32.9%, 4 KB reserved stack headroom maintained.
Host CppUTest simulation test suite 100% passing (17 tests).

- **UART TX Queue:** Migrated from single handle pointer to `hal::DriverRequestQueue<WriteRequest, 8>` drained by `handle_tx_dma_isr()`.
- **Dynamic Clock Calculations:** Peripheral clock rate divisors implemented and verified in `UartDmaDriver::init()`, `SpiDmaDriver::set_frequency()`, and `TwiDriver::compute_ccr()` using parent clocks from `ClockConfiguration`.
- **RemoteProc Control CLI:** Added `wait-ready` command and registered all test images (`timer`, `smoke`, `hello`, `ipc`) into `remoteproc_control.py`.
- **Test Harness Compatibility:** Configured CMake policies and `CPPUTEST_USE_MEM_LEAK_DETECTION=0` to eliminate placement-new macro collisions with ETL.

**Not yet on hardware.** Blocking items before first boot attempt:
1. PMA / `RISCV_SYSMAP` (`0xEFFF_F000`) unprogrammed — required before enabling
   D-cache, or the IPC rings break silently.
2. Core clock unverified; 600 MHz is a fallback constant.
3. 40-pin header routing unconfirmed — `gpioinfo` proves what is unused, not what
   is routed.
4. `SPI0` needs a per-transfer mode field to alternate dual-IO and single-IO.

See [docs/FIRMWARE_BRINGUP.md](docs/FIRMWARE_BRINGUP.md) for boot sequence,
interrupt routing, fault reporting, and hardware verification commands.
