# Implementation TODO

## Coroutine domain / I/O (ISR) domain

- [x] **Single queue abstraction**: `hal::DriverRequestQueue<T, N>` is
  `etl::queue_spsc_isr` with the `abstractx::InterruptLock` access policy. No
  hand-rolled rings remain in `hal/` or in the AbstractX dispatcher.
- [x] **Direction contract**: coroutine domain uses the locked calls
  (`push` / `pop`), the driver ISR uses the unlocked calls
  (`push_from_isr` / `pop_from_isr`).
- [x] **Driver migration**: GPIO, UART DMA, SPI DMA, and Msgbox enqueue requests
  from the coroutine domain and complete them from their ISR by popping the
  queue. SPI has one queue per bus instance.
- [ ] **UART TX queue**: TX still completes through a single
  `tx_awaiting_handle_`; move it onto a request queue once DMA TX is real.
- [ ] **Multi-core / thread policy**: add an `InterruptLock` sibling that uses a
  real mutex for the Linux-side and core-to-core queues.
- [ ] **CppUTest configure fix**: the fetched CppUTest 4.0 fails on modern CMake
  (`cmake_minimum_required` < 3.5). Pin a newer tag or set
  `CMAKE_POLICY_VERSION_MINIMUM=3.5` so `host/test_domain_queue.cpp` runs in CI.
- [ ] **RISC-V cross-build check**: rebuild the firmware apps with the xPack
  toolchain and confirm the ETL queues stay in SRAM.

## RemoteProc boot and timing

- [ ] **Linux clock producer**: Read the resolved CCU rates and write a valid
  `hal::clocking::ClockConfiguration` to `IPC_CLOCK_CONFIG_ADDR` before starting
  RemoteProc. Commit `generation` last.
- [ ] **Firmware clock startup**: Call
  `hal::HardwareTimer::init_from_shared_clock_configuration()` before enabling
  time-dependent peripherals. Publish `InitFailed` and notify Linux if
  validation fails.
- [ ] **Firmware-ready handshake**: After timer and required peripherals are
  initialized, publish `hal::boot::State::Ready` and ring
  `hal::boot::kFirmwareReadyDoorbell`.
- [ ] **Linux startup timeout**: Wait for ready/failed notification with a
  bounded timeout; on timeout, collect the shared boot status and trace data.
- [x] **Python ABI/message utility**: Generate validated 16-byte IPC descriptors,
  write clock records through a controlled kernel device, decode boot status,
  and manage the text-only RemoteProc lifecycle.
- [ ] **Peripheral-rate use**: Implement verified SoC-specific UART and SPI
  divider programming from their handed-off parent clock rates. Do not use the
  RISC-V core frequency for peripheral dividers.

## Crash reporting

- [ ] **Default trap handler**: Distinguish machine timer interrupts from
  exceptions. On an exception, capture `mcause`, `mepc`, and `mtval`; publish
  `State::Crashed`; ring `hal::boot::kFirmwareCrashDoorbell`; then halt/reset.
- [ ] **Linux crash handling**: On the crash doorbell or a heartbeat timeout,
  copy the `hal::boot::Status` record before resetting RemoteProc.
- [ ] **Controlled fault test**: Boot firmware, trigger a known trap, and verify
  the Linux-side crash report matches the captured RISC-V CSRs.

## BareCTF scheduler and CPU accounting

- [ ] **Install BareCTF 3.1+** on the development host.
- [ ] **Generate tracer artifacts** from
  `third_party/barectf/abstractx_scheduler.yaml`; keep generated code separate
  from the legacy handwritten `third_party/barectf/barectf.c` until migration.
- [ ] **Create `hal::trace` wrapper**: Initialize the generated tracer at
  `IPC_TRACE_BUFFER_ADDR`, expose boot and scheduler-event APIs, and finalize
  packets before Linux capture.
- [ ] **Task registry**: Assign stable coroutine IDs plus priority and scheduler
  lane at launch. Do not use coroutine frame addresses as IDs.
- [ ] **Central resume wrapper**: Emit BareCTF begin/end events around every
  scheduler-owned continuation resume, including dispatcher, timer, I/O,
  `Event`, `Semaphore`, `AsyncQueue`, and combinator paths.
- [ ] **Trace safety**: Do not write BareCTF from ISR context; record the wake
  reason and emit when the bottom-half scheduler performs the resume.
- [ ] **CPU-accounting validation**: Capture metadata plus a trace stream and
  verify each coroutine's CPU usage as $100 \sum(t_{end} - t_{begin}) / W$.

## Verification

- [x] Host ABI tests for clock configuration and boot/crash status.
- [x] CTest host suite passes.
- [x] BareCTF trace buffer address aligned across memory map, linker script, and
  RemoteProc resource table.
- [x] **Bare-C CLINT/debugfs timer test**: `timer_test.elf` cross-builds and
  places its text trace buffer in `.trace_ctf_buffer`; it polls the documented
  T527/A733 CLINT aperture at `0xE0000000`.
- [ ] **Validate interrupt-driven timer HAL**: Confirm standard CLINT
  `mtimecmp` offsets on hardware or with the vendor E907 driver before relying
  on `HardwareTimer` machine-timer interrupts.
- [ ] Cross-compile the C++ firmware apps and inspect their map files for SRAM
  layout after interrupt-driven timer validation.
- [ ] Validate the complete boot, ready, crash, and trace flow on T527 and A733
  hardware.

## Vendor reference source

- [x] **Locate public Tina AIOT manifest**: Radxa documents the GitLab manifest
  at `https://gitlab.com/tina5.0_aiot/manifest` with release file
  `tina_aiot-linux-v1.4.6.xml`. It includes `rtos/lichee/rtos`,
  `rtos/lichee/rtos-hal`, `rtos/lichee/rtos-components`, and a T527 RISC-V
  board project. Sync it before adopting vendor timer/interrupt code.
- [ ] **Download full official SDK archive**: Radxa's Baidu share is
  `https://pan.baidu.com/s/1zcVq4l-rij7RPmJ92nccZg` (extraction code `547b`).
  The unlocked share contains `tina_sdk_v1.4.6.tar.gz` (18.87 GB). It must be
  downloaded through Baidu's interactive account/client flow; inspect its
  E907/Melis/FreeRTOS timer and RemoteProc sources after extraction.
