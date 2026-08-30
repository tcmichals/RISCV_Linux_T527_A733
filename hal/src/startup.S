/*
 * Startup Code and Boot Trampoline for Allwinner T527 / A733 RISC-V Cores
 * Relocates vectors/fastcode to ITCM and data to DTCM. Init code runs in DRAM.
 */

    .section .vectors, "ax"
    .global _start_vector
    .align 4
_start_vector:
    /* Trap / Interrupt Vector Table in ITCM (0x00000000) */
    j _trap_entry           /* 0: Instruction address misaligned */
    j _trap_entry           /* 1: Instruction access fault */
    j _trap_entry           /* 2: Illegal instruction */
    j _trap_entry           /* 3: Breakpoint */
    j _trap_entry           /* 4: Load address misaligned */
    j _trap_entry           /* 5: Load access fault */
    j _trap_entry           /* 6: Store/AMO address misaligned */
    j _trap_entry           /* 7: Store/AMO access fault */
    j _trap_entry           /* 8: Environment call from U-mode */
    j _trap_entry           /* 9: Environment call from S-mode */
    j _trap_entry           /* 10: Reserved */
    j _trap_entry           /* 11: Environment call from M-mode */
    j _trap_entry           /* 12: Instruction page fault */
    j _trap_entry           /* 13: Load page fault */
    j _trap_entry           /* 14: Reserved */
    j _trap_entry           /* 15: Store page fault */

    .section .init_code, "ax"
    .global _start
    .type _start, @function
_start:
    /* Disable all global interrupts */
    csrci mstatus, 8

    /* Set stack pointer to top of DTCM (0x00090000) */
    la sp, _estack

    /* Set Global Pointer */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* Relocate Vector Table & Fastcode to ITCM (0x00000000) */
    la a0, _svectors
    la a1, _evectors
    la a2, _la_vectors
1:
    bgeu a0, a1, 2f
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j 1b

2:
    /* Relocate Text / Coroutine Engine to SRAM C */
    la a0, _stext
    la a1, _etext
    la a2, _la_text
3:
    bgeu a0, a1, 4f
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j 3b

4:
    /* Relocate Initialized Data to DTCM */
    la a0, _sdata
    la a1, _edata
    la a2, _la_data
5:
    bgeu a0, a1, 6f
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j 5b

6:
    /* Clear BSS */
    la a0, _sbss
    la a1, _ebss
7:
    bgeu a0, a1, 8f
    sw zero, 0(a0)
    addi a0, a0, 4
    j 7b

8:
    /* Point MTVEC to ITCM Vector Table */
    la t0, _start_vector
    csrw mtvec, t0

    /* The relocation loops above wrote code as data, so the instruction stream
     * for ITCM/SRAM_CODE must be re-fetched before any of it is executed.
     */
#ifdef ENABLE_XUANTIE_ICACHE
    /* XuanTie mcor (0x7C2): invalidate I-cache. mhcr (0x7C1) bit 0: I-cache enable.
     * Verify both encodings against the E907 manual before trusting on silicon.
     */
    li t0, 0x11
    csrw 0x7c2, t0
    csrsi 0x7c1, 1
#endif
    fence.i

    /* Call C++ Static Constructors */
    la t0, __libc_init_array
    beqz t0, 9f
    jalr t0

9:
    /* Jump into Main Application */
    call main

    /* Loop forever if main returns */
_infinite_loop:
    wfi
    j _infinite_loop

    .section .fastcode, "ax"
    .align 6
_trap_entry:
    /* The handler is ordinary C++, so every caller-saved register must be
     * preserved here or the interrupted coroutine resumes with corrupt state.
     */
    addi sp, sp, -64
    sw   ra,  0(sp)
    sw   t0,  4(sp)
    sw   t1,  8(sp)
    sw   t2, 12(sp)
    sw   a0, 16(sp)
    sw   a1, 20(sp)
    sw   a2, 24(sp)
    sw   a3, 28(sp)
    sw   a4, 32(sp)
    sw   a5, 36(sp)
    sw   a6, 40(sp)
    sw   a7, 44(sp)
    sw   t3, 48(sp)
    sw   t4, 52(sp)
    sw   t5, 56(sp)
    sw   t6, 60(sp)

    call riscv_trap_handler

    lw   ra,  0(sp)
    lw   t0,  4(sp)
    lw   t1,  8(sp)
    lw   t2, 12(sp)
    lw   a0, 16(sp)
    lw   a1, 20(sp)
    lw   a2, 24(sp)
    lw   a3, 28(sp)
    lw   a4, 32(sp)
    lw   a5, 36(sp)
    lw   a6, 40(sp)
    lw   a7, 44(sp)
    lw   t3, 48(sp)
    lw   t4, 52(sp)
    lw   t5, 56(sp)
    lw   t6, 60(sp)
    addi sp, sp, 64
    mret
