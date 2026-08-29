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
    .align 4
_trap_entry:
    /* Direct PLIC/CLIC Dispatcher Trampoline */
    call riscv_trap_handler
    mret
