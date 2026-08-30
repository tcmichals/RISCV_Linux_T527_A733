/*
 * Linker Script for the Allwinner T527 RISC-V (E906) Coprocessor
 *
 * Regions match the layout validated on T527 hardware by the cubie-a5e
 * ioProcessor firmware: SRAM C is 320 KB at 0x07130000 in the RISC-V local
 * view, split into a 64 KB IPC window and a 256 KB code window at 0x07140000.
 * The CPUX/Linux view of 0x07130000 is the RV_CFG register block; do not
 * confuse the two address spaces.
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

STACK_SIZE = 4K;

MEMORY
{
    /* Zero-Wait Tightly Coupled Memory */
    ITCM        (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM        (rwx) : ORIGIN = 0x00080000, LENGTH = 64K

    /* SRAM C low window: SPSC control words, counters, doorbells, trace buffer. */
    SRAM_SHARED (rw)  : ORIGIN = 0x07130000, LENGTH = 64K

    /* SRAM C code window: runtime text and the coroutine frame pool. */
    SRAM_CODE   (rwx) : ORIGIN = 0x07140000, LENGTH = 256K

    /* DRAM regions:
     *  - DRAM_INIT: one-time init / relocation image
     *  - DRAM_BUF: DMA payload pool and large buffer workspace
     *
     * The payload window is intentionally kept in a cache-inhibited / uncached DRAM range so
     * DMA and mailbox traffic does not alias stale CPU cache lines. The actual memory-attribute
     * programming for the SoC bus/CCU must be done in the platform setup; the linker script only
     * reserves the address window for the payload pool.
     */
    DRAM_INIT   (rx)  : ORIGIN = 0x48000000, LENGTH = 1M
    DRAM_BUF    (rw)  : ORIGIN = 0x48100000, LENGTH = 15M
}

SECTIONS
{
    /* Linux RemoteProc Resource Table (Parsed at ELF load) */
    .resource_table : {
        KEEP(*(.resource_table))
    } > SRAM_CODE

    /* 1. Vector table relocated into ITCM for zero-wait interrupt latency */
    .vectors : {
        _svectors = .;
        KEEP(*(.vectors))
        KEEP(*(.fastcode*))
        _evectors = .;
    } > ITCM AT > DRAM_INIT
    _la_vectors = LOADADDR(.vectors);

    /* 2. One-Time Initialization Code (Stays in DRAM, never wastes SRAM) */
    .init_code : {
        *(.init)
        *(.init.*)
        *(.text.startup*)
        *(.text.*init*)
        *(.init_array*)
    } > DRAM_INIT

    /* 3. Main Runtime Code & Coroutines */
    .text : {
        _stext = .;
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        _etext = .;
    } > SRAM_CODE AT > DRAM_INIT
    _la_text = LOADADDR(.text);

    /* 4. Initialized Data */
    .data : {
        _sdata = .;
        __global_pointer$ = . + 0x800;
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        _edata = .;
    } > DTCM AT > DRAM_INIT
    _la_data = LOADADDR(.data);

    /* 5. Zero-Initialized BSS */
    .bss (NOLOAD) : {
        _sbss = .;
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        _ebss = .;
        end = .;
        _end = .;
    } > DTCM

    /* 5b. Explicitly reserved stack, so overflow is a link error rather than a
     * silent collision with BSS at run time.
     */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        _sstack = .;
        . = . + STACK_SIZE;
        . = ALIGN(16);
        _estack = .;
    } > DTCM

    /* 5c. Coroutine frames live in the SRAM C code window, not DTCM. */
    .coro_frame_pool (NOLOAD) : {
        . = ALIGN(64);
        _scoro_pool = .;
        *(.coro_frame_pool)
        *(.coro_frame_pool.*)
        _ecoro_pool = .;
    } > SRAM_CODE

    /* 6. SPSC ring control / counters / descriptors remain in SRAM_SHARED.
     * These counters must stay hot, single-copy, and cache-stable for lock-free traffic.
     */
    .spsc_descriptors (NOLOAD) : {
        *(.spsc_ipc_control)
        *(.spsc_descriptors)
    } > SRAM_SHARED

    /* 7. BareCTF uses the SRAM buffer exported by the RemoteProc trace resource.
     * Keep this address in sync with IPC_TRACE_BUFFER_ADDR (0x07136000) and resource_table.c.
     */
    .trace_ctf_buffer 0x07136000 (NOLOAD) : {
        *(.trace_ctf_buffer)
    } > SRAM_SHARED

    /* 8. Large DMA payloads live in the DRAM payload window.
     * This region is intentionally mapped to the uncached DRAM space to avoid cache incoherence
     * with the DMA engine and MMIO mailbox paths.
     */
    .dram_buffers (NOLOAD) : {
        *(.dma_rx_pool)
        *(.dma_tx_pool)
        *(.dram_payloads)
    } > DRAM_BUF

    /* Pigweed Tokenizer String Database (Metadata only, stripped at runtime) */
    .pw_tokenizer.entries 0 (INFO) : {
        KEEP(*(.pw_tokenizer.entries))
    }
}
