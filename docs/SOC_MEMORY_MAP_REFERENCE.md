# SoC Register / Memory Map Reference (T527 & A733)

Extracted from the vendor manuals and the vendor BSP so the PDFs do not need to be reopened.

| Source | Path |
| --- | --- |
| T527 User Manual V0.92 | `docs/T527/Hardware硬件类文档/芯片手册/T527_User_Manual_V0.92.pdf` |
| A733 User Manual V0.92 | `docs/A733/Hardware硬件类文档/芯片手册/A733_User Manual_V0.92.pdf` |
| Vendor kernel BSP | `/run/media/tcmichals/projects/radxa/linux-aw2501` |
| RemoteProc driver | `linux-aw2501/bsp/drivers/remoteproc/` |
| T527 SoC DT | `linux-aw2501/bsp/configs/linux-5.15/sun55iw3p1.dtsi` |
| A527/T527 board DT | `linux-aw2501/device-a527/configs/demo_linux_aiot/linux-5.15/board.dts` |

The PDFs use CID Type-0 fonts; `pdftotext -layout` yields nothing. Use `-raw`:

```sh
pdftotext -raw "T527_User_Manual_V0.92.pdf" t527_um.txt   # ~2 min, 75k lines
pdftotext -raw "A733_User Manual_V0.92.pdf"  a733_um.txt   # ~1 min, 55k lines
```

> **T527 and A733 are not address-compatible.** They differ in peripheral bases,
> peripheral stride, and core. A single shared `memory_map.h` cannot serve both.

> **The T527 RISC-V core is an E906, not an E907.** The DT node is
> `e906_rproc@7130000`, `compatible = "allwinner,e906-rproc"`, and the driver is
> `sunxi_rproc_e906_boot.c`. `sunxi_rproc_e907_boot.c` exists for a different SoC.

---

## 0. THE CRITICAL TABLE — RISC-V device addresses (what firmware must link against)

From `memory-mappings` in the `e906_rproc` board DT node. **The RISC-V core's view
(DA) is not the same as the Linux/physical view (PA).** Firmware must be linked
against DA. Anything linked at a PA that is not also a DA is unreachable.

| Purpose | RISC-V DA | Length | Linux PA |
| --- | --- | --- | --- |
| DSP RAM | `0x0002_0000` | 128 KB | `0x0002_0000` |
| SRAM A2 | `0x0004_0000` | 144 KB | `0x0004_0000` |
| DDR (low window) | `0x0800_0000` | ~895 MB | `0x0800_0000` |
| **SRAM SPACE 0** (`SRAMA3_0`) | **`0x3FFC_0000`** | 256 KB | `0x0728_0000` |
| **SRAM SPACE 1** (`SRAMA3_1`) | **`0x4000_0000`** | 256 KB | `0x072C_0000` |
| **DRAM** | **`0x4004_0000`** | ~1 GB | `0x4004_0000` |

This matches the UM note: "RISC-V core accesses the DRAM address:
`0x4004_0000`–`0x7FFF_FFFF`" — DRAM starts right after SRAM SPACE 1 ends.

### Reserved DRAM regions (board DT)

| Node | Address | Size | Purpose |
| --- | --- | --- | --- |
| `e906_mem_fw` | `0x5F00_0000` | 10 MB | boot0/uboot0 ELF load address |
| `e906_dram_reserved` | `0x6000_0000` | 10 MB | firmware DRAM working set |
| `rv_vdev0buffer` | `0x4AE0_0000` | 256 KB | rpmsg shared buffer pool |
| `rv_vdev0vring0` | `0x4AE4_0000` | 8 KB | vring 0 |
| `rv_vdev0vring1` | `0x4AE4_2000` | 8 KB | vring 1 |
| `riscvsram0_reserved` | `0x0728_0000` | 256 KB | SRAMA3_0 (PA) |
| `riscvsram1_reserved` | `0x072C_0000` | 256 KB | SRAMA3_1 (PA) |

### Boot / start

RemoteProc writes the ELF entry point to `RV_CFG + 0x0204`
(`E906_STA_ADD_REG`), where `RV_CFG` = `0x0713_0000`, then releases reset.
The DT sets `auto-boot`.

---

## 1. T527 — RISC-V private address space


Only reachable from the RISC-V core. T527 UM section 2.1, "RISCV Related (Only RISC-V access)".

| Module | Address | Size | Notes |
| --- | --- | --- | --- |
| `RISCV_CLINT` | `0xE000_0000`–`0xE000_FFFF` | 64 KB | Machine timer (`mtime`/`mtimecmp`) |
| `RISCV_CLIC` | `0xE080_0000`–`0xE080_4FFF` | 20 KB | Core-Local Interrupt Controller |
| `RISCV_SYSMAP` | `0xEFFF_F000`–`0xEFFF_FFFF` | 4 KB | **Memory attributes (cacheable/bufferable)** |
| DRAM window | `0x4004_0000`–`0x7FFF_FFFF` | ~1 GB | RISC-V view of DRAM |

**There is no PLIC.** External interrupts are delivered through the CLIC.

`RISCV_SYSMAP` is the block to program for marking the IPC ring and DMA payload
regions non-cacheable — it is memory-mapped, not a CSR interface.

## 2. T527 — RISC-V subsystem control block

| Module | Address | Size |
| --- | --- | --- |
| `RISCV_CFG` | `0x0713_0000`–`0x0713_0FFF` | 4 KB |
| `RISCV_WDT` | `0x0713_2000`–`0x0713_2FFF` | 4 KB |
| `RISCV_LCNT` | `0x0713_4000`–`0x0713_4FFF` | 4 KB |
| `RISCV_MSGBOX` | `0x0713_6000`–`0x0713_6FFF` | 4 KB |

## 3. T527 — memories and peripherals used by this project

| Module | Address | Size | Notes |
| --- | --- | --- | --- |
| `SRAMA3_0` | `0x0728_0000`–`0x072B_FFFF` | 256 KB | Cannot be used across the 256 KB boundary |
| `SRAMA3_1` | `0x072C_0000`–`0x072F_FFFF` | 256 KB | Same boundary restriction |
| `SRAMA3_2` | `0x0730_0000`–`0x0737_FFFF` | 512 KB | NPU-shared |
| `SYSCTRL` | `0x0300_0000` | 4 KB | |
| `DMAC` | `0x0300_2000` | 4 KB | System DMA |
| `CPUX_MSGBOX` | `0x0300_3000` | 4 KB | CPUX-side mailbox |
| `SPINLOCK` | `0x0300_5000` | 4 KB | |
| `GIC` | `0x0340_0000` | 960 KB | CPUX interrupt controller |
| `UART0`..`UART7` | `0x0250_0000` + n×`0x400` | 1 KB each | **1 KB stride** |
| `TWI0`..`TWI5` | `0x0250_2000` + n×`0x400` | 1 KB each | |
| `SPI0` | `0x0402_5000` | 4 KB | |
| `SPI1` | `0x0402_6000` | 4 KB | |
| `SPI2` | `0x0402_7000` | 4 KB | |
| `S_INTC` | `0x0702_1000` | 1 KB | GIC→CLIC group forwarding masks |
| `S_GPIO` | `0x0702_2000` | 2 KB | |
| `S_UART0` / `S_UART1` | `0x0708_0000` / `0x0708_0400` | 1 KB | MCU-domain UARTs |
| `S_SPI0` | `0x0709_2000` | 4 KB | MCU-domain SPI |
| `CPUS_MSGBOX` | `0x0709_4000` | 4 KB | |
| `MCU_PRCM` | `0x0710_2000` | 4 KB | RISC-V clock gating / reset |
| `DSP_MSGBOX` | `0x0712_0000` | 4 KB | |
| `MCU_DMAC` | `0x0712_1000` | 4 KB | |
| `MCU_TIMER` | `0x0712_3000` | 1 KB | |

## 4. T527 — CLIC interrupt sources (Table 2-16)

CLIC supports up to 144 sources, 32 priority levels, 4 memory-mapped control
registers per interrupt. Interrupt vector = `0x0000 + 4 × number`.

| # | Source | Description |
| --- | --- | --- |
| 0–15 | Reserved | Not used |
| 16 | `RISCV_WDT` | RISC-V watchdog |
| 17 | `RISCV_MSGBOX_RISCV` | **RISC-V mailbox read IRQ** |
| 25–27 | `MCU_TIMER0..2` | |
| 30 | `AUDIO CODEC` | |
| 37 / 38 | `MCU_DMAC_NS` / `MCU_DMAC_S` | MCU DMA channel IRQ |
| 39 | `NPU` | |
| 41–43 | `MCU_TIMER3..5` | |
| 44 | `MCU_PWM0` | |
| 52 | `NMI` | STBY NMI |
| 56 | `CPUS_WDT` | |
| 57–59 | `CPUS_TIMER0..2` | |
| 62–65 | `GPIOL_S/NS`, `GPIOM_S/NS` | MCU-domain GPIO only |
| 66 / 67 | `S_UART0` / `S_UART1` | MCU-domain UART |
| 76 | `S_SPI` | MCU-domain SPI |
| 81 | `CPUS_MSGBOX_RISCV` | CPUS mailbox write IRQ for RISC-V |
| 83 | `INT_SCRI[0]` | `CPUX_MSGBOX_IRQ_RISCV` |
| 90 | `INT_SCRI[7]` | **GIC IRQ group 32–39** (mask: `GINTC_CONFIG_REG0[7:0]`) |
| 91 | `INT_SCRI[8]` | GIC IRQ group 40–47 (`GINTC_CONFIG_REG0[15:8]`) |
| 92 | `INT_SCRI[9]` | GIC IRQ group 48–55 (`GINTC_CONFIG_REG0[23:16]`) |
| 93 | `INT_SCRI[10]` | GIC IRQ group 56–63 (`GINTC_CONFIG_REG0[31:24]`) |
| 94 | `INT_SCRI[11]` | GIC IRQ group 64–71 |

### 4.1 Reaching main-domain peripherals from RISC-V

Main-domain UART/SPI/GPIO have **no dedicated CLIC line**. They are forwarded to
the RISC-V core only as *grouped* GIC interrupts through `INT_SCRI[n]`, gated by
`GINTC_CONFIG_REG0` in `S_INTC` (`0x0702_1000`). The ISR must then demux by
reading GIC pending state.

Relevant CPUX/GIC numbers:

| Peripheral | GIC # | Group | CLIC # |
| --- | --- | --- | --- |
| `UART0`..`UART7` | 34–41 | 32–39 / 40–47 | 90 / 91 |
| `UART2` (our console) | 36 | 32–39 | **90** |
| `SPI0` | 48 | 48–55 | **92** |
| `SPI1` | 49 | 48–55 | **92** |
| `SPI2` | 50 | 48–55 | 92 |
| `DMAC_CPUX_NS` | 82 | 80–87 | `INT_SCRI[12]` |
| `SPINLOCK` | 86 | 80–87 | `INT_SCRI[12]` |
| `GPIOA_NS` | 99 | 96–103 | `INT_SCRI[14]` |

MCU-domain peripherals (`S_UART0/1`, `S_SPI`, `MCU_TIMER`, `MCU_DMAC`, `GPIOL/M`)
have direct CLIC lines and are far simpler to service from RISC-V.

## 5. A733 differences — NOT the same addresses

A733 peripherals use a **4 KB stride** where T527 uses 1 KB, and live at different bases.

| Module | T527 | A733 |
| --- | --- | --- |
| `UART0` | `0x0250_0000` (1 KB stride) | `0x0250_0000` (**4 KB stride**) |
| `UART2` | `0x0250_0800` | **`0x0250_2000`** |
| `SPI0` | `0x0402_5000` | **`0x0254_0000`** |
| `SPI1` | `0x0402_6000` | **`0x0254_1000`** |
| `CPUX_MSGBOX` | `0x0300_3000` | **`0x0300_4000`** |
| `S_UART0` | `0x0708_0000` | `0x0708_0000` (4 KB) |
| `S_SPI` | `0x0709_2000` | `0x0709_2000` |
| `S_MBOX` / `CPUS_MSGBOX` | `0x0709_4000` | `0x0709_4000` |
| `S_INTC` | `0x0702_1000` | `0x0702_4000` |
| `S_GPIO` | `0x0702_2000` | `0x0702_5000` |

A733's companion core is an **E902 (CPUS)** with `CPUS_E902_CFG` at `0x0703_2000`.
Its address view differs fundamentally:

| Region | Address | Size |
| --- | --- | --- |
| `SHARED_SRAM` | `0x0007_4000`–`0x000F_3FFF` | 512 KB (requires NPU mapping, NPU must stay powered) |
| `L4_TOP_CONN` | `0x0008_0000`–`0x06FF_FFFF` | CPUX subsystem peripherals |
| `CPUS_CFG` | `0x0700_0000`–`0x0709_FFFF` | CPUS subsystem peripherals |
| SRAM space | `0x4000_0000`–`0x4003_3FFF` | 208 KB (mapped to sramA2) |
| DRAM space | `0x4003_4000`–`0x7FFF_FFFF` | ~1 GB |

E902 accesses DRAM only in `0x4000_0000`–`0x7FFF_FFFF`.

---

## 9. Pin plan — Radxa Cubie A5E (T527), RISC-V I/O processor

Target payload: barometer + magnetometer + ADC on I2C, IMU + FPGA on SPI
(dual-IO, two chip selects), UART console, a few debug pins. **PCIe is retained.**

Ownership verified on hardware with `gpioinfo`:
`gpiochip0` = R_PIO (PL = lines 0–31, PM = lines 32–63);
`gpiochip1` = main PIO (PA=0, PB=32, PC=64, PD=96, PE=128, PF=160, PG=192,
PH=224, PI=256, PJ=288, PK=320).

### 9.1 MCU/CPUS domain — R_PIO (`S_GPIO` @ `0x0702_2000`)

| Pin | gpiochip0 line | Mux function | Assignment | Owner | CLIC |
| --- | --- | --- | --- | --- | --- |
| PL0 | 0 | `S-TWI0-SCK` | AXP PMIC I2C | **Linux — do not touch** | — |
| PL1 | 1 | `S-TWI0-SDA` | AXP PMIC I2C | **Linux — do not touch** | — |
| PL2 | 2 | `S-UART0-TX` | **RISC-V console TX** | RISC-V | 66 |
| PL3 | 3 | `S-UART0-RX` | **RISC-V console RX** | RISC-V | 66 |
| PL4 | 4 | GPIO | green power LED | Linux | — |
| PL5 | 5 | GPIO | blue activity LED | Linux | — |
| PL6 | 6 | GPIO / `PL-EINT6` | **IMU data-ready IRQ** | RISC-V | 63 |
| PL7 | 7 | GPIO | `vcc-3v3` regulator | Linux | — |
| PL8 | 8 | GPIO | USB1 VBUS | Linux | — |
| PL9 | 9 | GPIO / `PL-EINT9` | **FPGA IRQ (spare)** | RISC-V | 63 |
| PL10 | 10 | GPIO | **debug / scope 0** | RISC-V | — |
| PL11 | 11 | GPIO | **`pcie-3v3` — RESERVED** | Linux (PCIe) | — |
| PL12 | 12 | GPIO | **debug / scope 1** | RISC-V | — |
| PL13 | 13 | GPIO | spare | — | — |
| PM0 | 32 | `S-UART1-TX` | spare | — | 67 |
| PM1 | 33 | GPIO | `wifi-en` | Linux | — |
| PM2 | 34 | `S-TWI1-SCK` | spare I2C | — | 69 |
| PM3 | 35 | `S-TWI1-SDA` | spare I2C | — | 69 |
| PM4 | 36 | `S-TWI2-SCK` | **I2C SCL** (baro/mag/ADC) | RISC-V | 60 |
| PM5 | 37 | `S-TWI2-SDA` | **I2C SDA** (baro/mag/ADC) | RISC-V | 60 |

> `S-SPI0-CLK` is muxed **only** on PL11, which is the PCIe 3V3 enable.
> Keeping PCIe therefore rules out the MCU-domain SPI entirely; SPI must come
> from the main domain (§9.2).

### 9.2 Main domain — `SPI0` @ `0x0402_5000` (CLIC 92 via `INT_SCRI[9]`)

Two candidate pin sets. Both are free in software; PC is shared with the
on-board boot SPI-NOR (`SPIF-*`), so **PJ is the safer choice**.

| Signal | PC option | line | PJ option | line | Notes |
| --- | --- | --- | --- | --- | --- |
| `SPI0-CS0` | PC3 | 67 | PJ20 | 308 | IMU (single-IO) |
| `SPI0-CLK` | PC12 | 76 | PJ21 | 309 | |
| `SPI0-MOSI` | PC2 | 66 | PJ22 | 310 | data 0 in dual-IO |
| `SPI0-MISO` | PC4 | 68 | PJ23 | 311 | data 1 in dual-IO |
| `SPI0-CS1` | PC7 | 71 | PJ24 | 312 | FPGA (dual-IO) |
| `SPI0-WP` | PC15 | 79 | PJ25 | 313 | quad-IO only |
| `SPI0-HOLD` | PC16 | 80 | PJ26 | 314 | quad-IO only |

PC2/3/4/12 also carry `SPIF-MOSI/CS0/MISO/CLK` for the on-board boot SPI flash;
if the flash is populated those pins are physically committed. PJ0–PJ16 are used
by GMAC1, but PJ17–PJ31 are unused.

### 9.3 Bus assignment summary

| Bus | Controller | Base | Devices | Interrupt |
| --- | --- | --- | --- | --- |
| Console | `S_UART0` | `0x0708_0000` | TX/RX only, no flow control | CLIC 66 |
| I2C | `S_TWI2` | `0x0708_1C00` | barometer, magnetometer, ADC | CLIC 60 |
| SPI CS0, single-IO | `SPI0` | `0x0402_5000` | IMU | CLIC 92 (grouped) |
| SPI CS1, dual-IO | `SPI0` | `0x0402_5000` | FPGA (TLP streaming) | CLIC 92 (grouped) |
| GPIO | `R_PIO` | `0x0702_2000` | IMU DRDY, FPGA IRQ, 2 debug | CLIC 63 |

SPI0 supports Dual and Quad I/O (UM figures 8-82/8-83/8-84), so dual-IO for the
FPGA and single-IO for the IMU is valid — but the mode lives in `TCR` and is
per-transfer, so the driver must reprogram it on every chip-select switch.

### 9.4 Device tree changes required on the Linux side

```dts
&uart8 { status = "disabled"; };   /* S_UART0 (PL2/PL3) - already disabled */
&s_twi2 { status = "disabled"; };  /* PM4/PM5 */
&spi0   { status = "disabled"; };  /* hand the whole controller to RISC-V */
&r_pio  { status = "disabled"; };  /* only if Linux gives up the LEDs/regulators */
```

`&r_pio` cannot simply be disabled here: Linux owns the LEDs (PL4/PL5), the
`vcc-3v3` (PL7), USB VBUS (PL8), PCIe 3V3 (PL11), and `wifi-en` (PM1) on that
same controller. Use `ignore-interrupts` on the R_PIO node instead so Linux keeps
its GPIO outputs while the RISC-V owns the PL/PM interrupt path:

```dts
&r_pio { ignore-interrupts = <191>; };  /* GPIOL hwirq; 193 = GPIOM */
```

### 9.5 Open items

1. Confirm which pins physically reach the 40-pin header — `gpioinfo` proves what
   is *unused*, not what is *routed*. Needs the Radxa pinout or schematic.
2. Confirm whether the boot SPI-NOR populates PC2/3/4/12; if so, use the PJ set.
3. `SPI0` interrupts require `S_INTC` `GINTC_CONFIG_REG0[23:16]` group forwarding
   plus GIC pending demux in the ISR.
4. No I2C/TWI driver exists in `hal/` yet — this is new code, not a port.
5. `TransferRequest` in `hal_spi_dma.hpp` has no per-transfer mode field; needed
   to alternate dual-IO (FPGA) and single-IO (IMU) on one controller.

---

## 10. Discrepancies against `hal/include/memory_map.h`


| Constant | Current value | Manual | Verdict |
| --- | --- | --- | --- |
| `SRAM_SHARED_BASE` | `0x0713_0000` | `RISCV_CFG` register block | **WRONG — collides with registers, not SRAM** |
| `IPC_TRACE_BUFFER_ADDR` | `0x0713_8000` | unassigned in RISCV_SYS | **Suspect** |
| `IPC_CLOCK_CONFIG_ADDR` | `0x0714_0000` | outside RISCV_SYS | **Suspect** |
| `IPC_BOOT_STATUS_ADDR` | `0x0714_1000` | outside RISCV_SYS | **Suspect** |
| `PLIC_BASE` | `0x1000_0000` | no PLIC exists | **WRONG — remove, use CLIC** |
| `MSGBOX_BASE` | `0x0300_3000` | `CPUX_MSGBOX` | Wrong owner; RISC-V should use `RISCV_MSGBOX` `0x0713_6000` |
| `RISCV_CLINT_BASE` | `0xE000_0000` | `0xE000_0000` | Correct |
| `CLIC_BASE` | `0xE080_0000` | `0xE080_0000` | Correct |
| `SRAM_DEDICATED_BASE` | `0x0728_0000` | `SRAMA3_0` | Correct (256 KB boundary limit applies) |
| `UART2_BASE` | `0x0250_0800` | `0x0250_0800` | Correct for T527, wrong for A733 |
| `SPI0_BASE` / `SPI1_BASE` | `0x0402_5000` / `0x0402_6000` | same | Correct for T527, wrong for A733 |
| `PIO_BASE` | `0x0200_0000` | — | Unverified |
| `SYS_DMA_BASE` | `0x0300_2000` | `DMAC` | Correct |
| `MCU_PRCM_BASE` | `0x0710_2000` | `MCU_PRCM` | Correct |
| `DRAM_RESERVED_BASE` | `0x4800_0000` | within `0x4004_0000`–`0x7FFF_FFFF` | Valid |

### Highest-priority consequences

1. **The linker script links against physical addresses the RISC-V core cannot reach.**
   `SRAM_CODE` is at `0x0728_0000` (the *Linux* PA). The RISC-V core sees that SRAM at
   DA `0x3FFC_0000`. Likewise `SRAMA3_1` is DA `0x4000_0000`, not `0x072C_0000`.
   Firmware text/data must be linked at DA. Only DRAM is identity-mapped
   (`0x4004_0000`+), which is why `DRAM_RESERVED_BASE = 0x4800_0000` happens to be valid.
2. **`SRAM_SHARED_BASE` overlaps the `RISCV_CFG`/`RISCV_WDT`/`RISCV_LCNT`/`RISCV_MSGBOX`
   register block.** The linker maps 64 KB of `SRAM_SHARED` at `0x0713_0000`, placing
   `.spsc_descriptors` on top of live control registers — including the very register
   (`RV_CFG + 0x204`) RemoteProc uses to set the boot address. Move the IPC rings into
   SRAM SPACE 1 (DA `0x4000_0000`).
3. **No PLIC.** External interrupt dispatch must be built on the CLIC at `0xE080_0000`.
4. **Main-domain UART2/SPI0/SPI1 cannot interrupt the RISC-V core directly** — they
   arrive only as grouped GIC interrupts via `S_INTC`. The MCU-domain `S_UART0`
   (CLIC 66) and `S_SPI` (CLIC 76) have dedicated lines and are the simpler path.
5. **`RISCV_SYSMAP` (`0xEFFF_F000`)** is the mechanism for non-cacheable regions,
   not PMP CSRs.
6. **ITCM `0x0000_0000` / DTCM `0x0008_0000` are not in the mapping table.** The DA
   list has DSP RAM at `0x0002_0000` and SRAM A2 at `0x0004_0000` instead. The TCM
   assumption in `memory_map.h` and the linker script needs verification against the
   E906 integration before it can be trusted.

## 7. Vendor shared-interrupt mechanism

`linux-aw2501/bsp/drivers/remoteproc/sunxi_share_interrupt/` implements GIC IRQ
sharing between Linux and the remote core. The table in `sun55iw3-share-irq.h`
covers **GPIO only** (GPIOB–GPIOM, GIC 101–193), for e906 and hifi4:

```c
#define ARM_IRQn(_x)  (_x - 32)
{1, ARM_IRQn(101)},  /* GPIOB */ ... {12, ARM_IRQn(193)},  /* GPIOM */
```

API: `sunxi_arch_interrupt_save()`, `sunxi_arch_interrupt_restore()`,
`sunxi_rproc_get_gpio_mask()`. There is no vendor sharing path for SPI or UART —
those must use either MCU-domain instances or the `S_INTC` group forwarding.

## 8. Answering "is there SPI for the RISC-V?"

Yes, two routes:

| Route | Instance | Base | Interrupt | Notes |
| --- | --- | --- | --- | --- |
| **Preferred** | `S_SPI0` | `0x0709_2000` | CLIC 76 (direct) | MCU/CPUS-domain SPI, designed for this core |
| Alternative | `SPI0`/`SPI1` | `0x0402_5000` / `0x0402_6000` | CLIC 92 via `INT_SCRI[9]` | Group GIC IRQ 48–55, needs `GINTC_CONFIG_REG0[23:16]` mask + pending demux, and must be contended with Linux |

The current driver targets `SPI0`/`SPI1` (main domain) and assumes a direct ISR,
which does not exist. Either move to `S_SPI0` or implement group forwarding.
Check board pinmux to confirm which instance reaches the intended pins.

