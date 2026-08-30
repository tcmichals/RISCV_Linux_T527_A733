/*
 * Memory Map Definitions for Allwinner T527 (sun55i) & A733 (sun60i)
 * Targets: XuanTie E907 (RV32IMAC) / XuanTie E902 (RV32EMC)
 */

#ifndef HAL_MEMORY_MAP_H
#define HAL_MEMORY_MAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Physical Memory Segments                                                  */
/* ========================================================================= */
#define ITCM_BASE               0x00000000U
#define ITCM_SIZE               0x00010000U /* 64 KB */

#define DTCM_BASE               0x00080000U
#define DTCM_SIZE               0x00010000U /* 64 KB (Stack @ 0x00090000) */

#define SRAM_SHARED_BASE        0x07130000U /* SRAM C low window, RISC-V local view */
#define SRAM_SHARED_SIZE        0x00010000U /* 64 KB reserved for IPC; code starts at 0x07140000 */

/* IPC & diagnostics layout inside the 64 KB SRAM C shared window.
 * Offsets are page aligned so the region ends exactly at the code window. */
#define IPC_BOOT_STATUS_ADDR    0x07130000U /* Firmware -> Linux boot/crash ABI (4 KB) */
#define IPC_BOOT_STATUS_SIZE    0x00001000U
#define IPC_CLOCK_CONFIG_ADDR   0x07131000U /* Linux -> firmware clock ABI (4 KB) */
#define IPC_CLOCK_CONFIG_SIZE   0x00001000U
#define IPC_SPSC_TX_RING_ADDR   0x07132000U /* E906 -> Linux (8 KB) */
#define IPC_SPSC_RX_RING_ADDR   0x07134000U /* Linux -> E906 (8 KB) */
#define IPC_TRACE_BUFFER_ADDR   0x07136000U /* BareCTF trace buffer (32 KB) */
#define IPC_TRACE_BUFFER_SIZE   0x00008000U

#define SRAM_DEDICATED_BASE     0x07280000U /* SRAMA3_0, Linux PA view (riscvsram0) */
#define SRAM_DEDICATED_SIZE     0x00040000U

#define DRAM_RESERVED_BASE      0x48000000U /* Off-chip DRAM reserved by Linux */
#define DRAM_INIT_BASE          0x48000000U /* Discardable init code & constructors */
#define DRAM_INIT_SIZE          0x00100000U /* 1 MB */
#define DRAM_PAYLOAD_BASE       0x48100000U /* Bulk DMA TX/RX & trace payload pools */
#define DRAM_PAYLOAD_SIZE       0x00F00000U /* 15 MB */

/* ========================================================================= */
/* Peripheral Register Base Addresses                                        */
/* ========================================================================= */
/* Clock Control Units */
#define MAIN_CCU_BASE           0x02001000U /* Root clocks (CLK_DSP, etc.) */
#define MCU_PRCM_BASE           0x07102000U /* RISC-V clock gating & reset */

/* UARTs */
#define UART0_BASE              0x02500000U
#define UART1_BASE              0x02500400U
#define UART2_BASE              0x02500800U /* Primary DMA UART */
#define UART3_BASE              0x02500C00U
#define R_UART_BASE             0x07080000U

/* SPIs */
#define SPI0_BASE               0x04025000U /* High-Speed Dual-IO SPI */
#define SPI1_BASE               0x04026000U /* Single-IO IMU SPI */
#define S_SPI0_BASE             0x07092000U /* MCU-domain SPI (CLIC 76) */

/* TWI / I2C, MCU domain: direct CLIC lines 68 / 69 / 60 */
#define S_TWI0_BASE             0x07081400U
#define S_TWI1_BASE             0x07081800U
#define S_TWI2_BASE             0x07081C00U

/* MCU-domain UARTs: direct CLIC lines 66 / 67 */
#define S_UART0_BASE            0x07080000U
#define S_UART1_BASE            0x07080400U

/* GPIO / PIO */
#define PIO_BASE                0x02000000U /* Main GPIO */
#define R_PIO_BASE              0x07022000U /* Low-Power / CPUS GPIO (PL, PM) */
#define S_INTC_BASE             0x07021000U /* GIC -> CLIC group forwarding */

/* DMA Controllers */
#define SYS_DMA_BASE            0x03002000U /* Main System DMA */

/* Hardware Mailbox / Doorbell */
#define MSGBOX_BASE             0x03003000U /* CPUX_MSGBOX: ring the doorbell to Linux */
#define RISCV_MSGBOX_BASE       0x07136000U /* RISCV_MSGBOX, CPUX view (CLIC 17 on receive) */

/* Interrupt Controllers.
 * The E906 has no PLIC: external interrupts arrive through the CLIC. */
#define RISCV_CLINT_BASE        0xE0000000U /* Machine timer, 64 KB */
#define RISCV_MTIMECMP_ADDR     (RISCV_CLINT_BASE + 0x00004000U)
#define RISCV_MTIME_ADDR        (RISCV_CLINT_BASE + 0x0000BFF8U)
#define CLIC_BASE               0xE0800000U /* Core-Local Interrupt Controller, 20 KB */
#define RISCV_SYSMAP_BASE       0xEFFFF000U /* Memory attribute (cacheable) control, 4 KB */
#define RISCV_CFG_BASE          0x07130000U /* CPUX view only: boot address at +0x204 */

#ifdef __cplusplus
}
#endif

#endif /* HAL_MEMORY_MAP_H */
