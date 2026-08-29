# Clock, Boot, and Crash Handoff

## Shared-memory contracts

Linux controls CCU clocks and writes `ClockConfiguration` at
`IPC_CLOCK_CONFIG_ADDR` before starting RemoteProc. Firmware reads it before
arming time-dependent hardware. Firmware writes `hal::boot::Status` at
`IPC_BOOT_STATUS_ADDR` before notifying Linux with the hardware mailbox.

Both records are 64-byte, versioned structures. The producer writes the
nonzero `generation` field last; the consumer accepts a record only after its
magic, ABI version, size, and generation validate.

## Startup protocol

1. Linux configures/enables CCU clocks and obtains their resolved rates through
   the Linux clock framework.
2. Linux writes clock fields, then commits `ClockConfiguration::generation`.
3. Linux starts RemoteProc.
4. Firmware validates the configuration and calls
   `HardwareTimer::init_from_shared_clock_configuration()`.
5. Firmware publishes `State::Ready` and rings mailbox value
   `kFirmwareReadyDoorbell` (`0xA1`).
6. Linux waits for that explicit notification with a bounded timeout.

A clock-validation failure publishes `State::InitFailed`, a precise
`FailureReason`, and rings `kFirmwareInitFailedDoorbell` (`0xA2`). Debugfs is
for evidence, not for this handshake.

## Crash reporting

When a recoverable RISC-V trap is dispatched, the trap handler should publish
`State::Crashed` with `FailureReason::UnexpectedTrap`, `mcause`, `mepc`, and
`mtval`, then execute a memory fence and ring `kFirmwareCrashDoorbell` (`0xA3`).
Linux reads the record after receiving that doorbell and can save it before
resetting RemoteProc.

This is best effort only. A hard lockup, reset, corrupted SRAM, or bus fault
can prevent firmware from sending the crash notification. Linux must still use
startup and heartbeat timeouts, then inspect the last shared status and the
BareCTF debugfs trace after a timeout.

## Peripheral rates

Use `riscv_core_hz` only to convert `mcycle` for time/BareCTF timestamps. UART
and SPI dividers must use their own `uart_parent_hz`, `spi0_parent_hz`, and
`spi1_parent_hz` fields. The current drivers still need their SoC-specific
CCU/peripheral divider encodings implemented before consuming those values.
