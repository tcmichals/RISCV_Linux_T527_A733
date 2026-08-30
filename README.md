# RISCV_Linux_T527_A733: AbstractX & HAL for Allwinner T527 & A733

A standalone, out-of-tree firmware ecosystem and Hardware Abstraction Layer (HAL) for the **XuanTie E907 / E902** RISC-V coprocessors inside Allwinner T527 (`sun55i`) and A733 (`sun60i`) SoCs, managed via Linux 7.1 RemoteProc.

---

## 1. Key Features

- **AbstractX Native**: Stackless C++20 coroutines (`co_await`) with zero dynamic memory allocation and sub-20ns resumption latency.
- **Optimized Memory Architecture**:
  - **ITCM (64 KB)**: Zero-wait vector table & fast interrupt handlers.
  - **DTCM (64 KB)**: Stack & atomic coroutine frame pools.
  - **Shared SRAM C (64 KB)**: Lock-free SPSC ring control headers and descriptors for 1-cycle CPU synchronization.
  - **Dedicated SRAM (256 KB)**: Main active coroutine engine code.
  - **Off-Chip DRAM (16 MB)**: One-time discardable initialization code, Pigweed token strings, and bulk DMA transfer buffers.
- **Zero-Copy Peripheral Drivers**:
  - **UART DMA + Character Timeout (RTO)**: Non-blocking variable-length serial streaming.
  - **SPI DMA (Dual-IO & Single-IO)**: High-speed FPGA PCIe TLP transactions and IMU sensor bursts.
  - **ISR-Safe GPIO**: Atomic pin manipulation (`amoxor.w`) for oscilloscope timing and edge-triggered IMU `DRDY` coroutine dispatch.
  - **Driver Request Queues**: ISR-safe request buffers for driver calls from coroutines and interrupt paths, with multiple pending requests supported in a single queue.
  - **Hardware Mailbox**: Doorbell IPI interrupts to Linux host.
- **Diagnostics**:
  - **Google Pigweed**: 4-byte tokenized compile-time string logging.
  - **BareCTF**: LTTng/CTF binary trace streaming mapped directly to Linux debugfs (`cat /sys/kernel/debug/remoteproc/remoteproc0/trace0`).

---

## 2. Directory Structure

```
RISCV_Linux_T527_A733/
├── CMakeLists.txt                 # Top-level standalone CMake
├── cmake/
│   ├── riscv-toolchain.cmake      # xPack GCC cross-compilation toolchain
│   └── fetch_xpack.cmake          # Automated toolchain download fallback
├── hal/
│   ├── include/
│   │   ├── memory_map.h           # T527 & A733 peripheral base addresses
│   │   ├── hal_timer.hpp          # CSR MTIME & AbstractX Timer Service
│   │   ├── hal_uart_dma.hpp       # Circular DMA + RTO idle detection
│   │   ├── hal_spi_dma.hpp        # Dual/Single-IO SPI DMA
│   │   ├── hal_gpio.hpp           # ISR-safe atomic pin toggling & DRDY awaiter
│   │   ├── hal_msgbox.hpp         # Hardware Mailbox doorbell driver
│   │   └── ipc_protocol.hpp       # 16B SPSC descriptors & message definitions
│   └── src/
│       ├── startup.S              # Trampoline, TCM vector relocation, CRT0
│       ├── riscv_memory_map.ld    # Generic linker script (TCM, SRAM, DRAM partitioning)
│       └── resource_table.c       # RemoteProc ELF metadata & trace resource
├── AbstractX/                     # C++20 Coroutine Engine & SPSC Rings
├── apps/
│   ├── hello_world/               # Heartbeat, tracefs output & pin measurement
│   ├── ipc_benchmark/             # SRAM SPSC ring + DRAM payload streaming
│   ├── fault_test/                # Controlled exception & crash reporting verification
│   ├── ipc_ping_test/             # Bidirectional IPC ping-pong & RTT latency test
│   ├── platform_smoke_test/       # GPIO pin walking & routing test with trace
│   ├── timer_test/                # CLINT mtime counter & 1 Hz trace heartbeat
│   └── linux_imu_demo/            # Linux-side IPC example for IMU request/response flow
├── third_party/
│   ├── etl/                       # Embedded Template Library
│   ├── pigweed/                   # pw_tokenizer, pw_span, pw_status
│   └── barectf/                   # Common Trace Format generator
└── docs/
    ├── REMOTEPROC_TEST_SUITE.md   # Step-by-step test suite & checkout guide
    ├── LINUX_DEVICE_TREE_GUIDE.md # Linux 7.1 RemoteProc, clocks & memory DT setup
    ├── FIRMWARE_BRINGUP.md        # Memory layout, vectors, interrupt routing
    └── engineering_log.md         # Detailed architectural and engineering log
```

---

## 3. Building

```bash
# Build firmware ELF binaries using xPack RISC-V GCC
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-toolchain.cmake
cmake --build build
```

---

## 4. Loading via Linux 7.1 RemoteProc

```bash
# Copy binary to target
scp build/hello_world.elf root@target:/lib/firmware/sun55i-e907-fw.elf

# Boot coprocessor
echo sun55i-e907-fw.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Read live trace stream
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```
