# Engineering Log: AbstractX & HAL for Allwinner T527 & A733 RISC-V Cores

## Project Identification
- **Workspace:** `RISCV_Linux_T527_A733`
- **Target SoCs:** Allwinner T527 / A527 / A523 (`sun55i`) and Allwinner A733 (`sun60i`)
- **Target Cores:** 
  - T527 / A527: T-Head XuanTie E907 RV32IMAC @ 600 MHz with PLIC
  - A733: T-Head XuanTie E902 RV32EMC/RV32IMC with CLIC v0.8
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
