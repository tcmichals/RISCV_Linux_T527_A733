# Linux 7.1 Device Tree Configuration: Allwinner T527 & A733 RemoteProc & Interconnect

## 1. Overview
The Linux 7.1 RemoteProc subsystem on Allwinner T527 (`sun55i`) and A733 (`sun60i`) boots the secondary RISC-V coprocessor (E907 on T527, E902 on A733) out of reset and configures shared memory regions, hardware mailboxes, and trace buffers.

---

## 2. Memory Topology & Reserved Memory

```dts
/ {
    reserved-memory {
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;

        /* 256 KB Dedicated RISC-V SRAM */
        riscv_sram: sram@7280000 {
            reg = <0x0 0x07280000 0x0 0x00040000>;
            no-map;
        };

        /* 16 KB SPSC TX Ring in Shared SRAM C (E907 -> Linux) */
        vdev0vring0: vring0@7130000 {
            reg = <0x0 0x07130000 0x0 0x00004000>;
            no-map;
        };

        /* 16 KB SPSC RX Ring in Shared SRAM C (Linux -> E907) */
        vdev0vring1: vring1@7134000 {
            reg = <0x0 0x07134000 0x0 0x00004000>;
            no-map;
        };

        /* 32 KB BareCTF / Trace Buffer in SRAM C */
        trace_buffer: trace@7138000 {
            reg = <0x0 0x07138000 0x0 0x00008000>;
            no-map;
        };

        /* 16 MB Shared Bulk DRAM for DMA Payloads & One-Time Init */
        riscv_dram: dram@48000000 {
            reg = <0x0 0x48000000 0x0 0x01000000>;
            no-map;
        };
    };
};
```

---

## 3. RemoteProc & Mailbox Node (`sun55i-a527.dtsi` / `sun60i-a733.dtsi`)

```dts
&soc {
    msgbox: mailbox@3003000 {
        compatible = "allwinner,sun55i-a523-msgbox", "allwinner,sun8i-a83t-msgbox";
        reg = <0x0 0x03003000 0x0 0x1000>;
        clocks = <&ccu CLK_BUS_MSGBOX>;
        resets = <&ccu RST_BUS_MSGBOX>;
        interrupts = <GIC_SPI 147 IRQ_TYPE_LEVEL_HIGH>;
        #mbox-cells = <1>;
    };

    rproc: remoteproc@7102000 {
        compatible = "allwinner,sun55i-a527-rproc", "allwinner,sunxi-rproc";
        reg = <0x0 0x02001000 0x0 0x1000>,   /* Main CCU (parent DSP clock) */
              <0x0 0x07102000 0x0 0x1000>,   /* MCU_PRCM / Clock & Reset Domain */
              <0x0 0x07280000 0x0 0x40000>,  /* Dedicated RISC-V SRAM (256 KB) */
              <0x0 0x07130000 0x0 0x20000>;  /* Shared SRAM C (128 KB) */
        reg-names = "main_ccu", "ccu", "r_sram", "sram";
        clocks = <&ccu CLK_DSP>, <&mcu_ccu CLK_RISCV>;
        clock-names = "dsp_root", "riscv";
        resets = <&mcu_ccu RST_RISCV>;
        mboxes = <&msgbox 0>, <&msgbox 1>;
        mbox-names = "tx", "rx";
        memory-region = <&riscv_sram>, <&vdev0vring0>, <&vdev0vring1>, <&trace_buffer>, <&riscv_dram>;
        status = "okay";
    };
};
```

---

## 4. Peripheral Pinmux and Clocks (SPI & UART DMA)

```dts
&spi0 {
    pinctrl-names = "default";
    pinctrl-0 = <&spi0_pins>;
    status = "okay";
    /* Configured for Dual-IO FPGA TLP streaming */
};

&uart2 {
    pinctrl-names = "default";
    pinctrl-0 = <&uart2_pins>;
    status = "okay";
    /* Circular DMA + Receiver Timeout (RTO) */
};
```

---

## 5. Linux Host Loading Procedure

```bash
# 1. Copy ELF to target firmware repository
cp build/bin/e907_firmware.elf /lib/firmware/sun55i-e907-fw.elf

# 2. Configure RemoteProc firmware target and boot
echo sun55i-e907-fw.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# 3. Read live BareCTF trace stream from debugfs
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0 > live_trace.ctf
```
