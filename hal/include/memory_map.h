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

#define SRAM_SHARED_BASE        0x07130000U /* Shared SRAM C (320 KB) */
#define SRAM_SHARED_SIZE        0x00050000U

/* SPSC Ring Buffers in Shared SRAM C */
#define IPC_SPSC_TX_RING_ADDR   0x07130000U /* E907/E902 -> Linux (16 KB) */
#define IPC_SPSC_RX_RING_ADDR   0x07134000U /* Linux -> E907/E902 (16 KB) */
#define IPC_TRACE_BUFFER_ADDR   0x07138000U /* BareCTF Trace Buffer (32 KB) */
#define IPC_CLOCK_CONFIG_ADDR   0x07140000U /* Linux -> firmware clock ABI (4 KB) */
#define IPC_CLOCK_CONFIG_SIZE   0x00001000U
#define IPC_BOOT_STATUS_ADDR    0x07141000U /* Firmware -> Linux boot/crash ABI (4 KB) */
#define IPC_BOOT_STATUS_SIZE    0x00001000U

#define SRAM_DEDICATED_BASE     0x07280000U /* Dedicated RISC-V SRAM (256 KB) */
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

/* GPIO / PIO */
#define PIO_BASE                0x02000000U /* Main GPIO */
#define R_PIO_BASE              0x07022000U /* Low-Power / CPUS GPIO */

/* DMA Controllers */
#define SYS_DMA_BASE            0x03002000U /* Main System DMA */

/* Hardware Mailbox / Doorbell */
#define MSGBOX_BASE             0x03003000U /* Hardware Mailbox */

/* Interrupt Controllers */
#define PLIC_BASE               0x10000000U /* E907 Platform-Level Interrupt Controller */
#define RISCV_CLINT_BASE        0xE0000000U /* T527/A733 RISC-V CLINT/TCIP aperture */
#define RISCV_MTIMECMP_ADDR     (RISCV_CLINT_BASE + 0x00004000U) /* Hart 0, standard CLINT layout */
#define RISCV_MTIME_ADDR        (RISCV_CLINT_BASE + 0x0000BFF8U) /* Standard CLINT layout */
#define CLIC_BASE               0xE0800000U /* T527/A733 RISC-V Core-Local Interrupt Controller */

#ifdef __cplusplus
}
#endif

#endif /* HAL_MEMORY_MAP_H */
