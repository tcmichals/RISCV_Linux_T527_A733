# RISC-V Debugging Strategy

## Current conclusion

The T527/A733 RemoteProc integration provides Linux control of the RISC-V core
and access to selected memories, but it is not a hardware CPU debugger.

The local RemoteProc driver maps:

- RISC-V configuration registers for boot address programming;
- instruction and data TCM windows;
- dedicated RISC-V SRAM;
- shared SRAM, including the `trace0` RemoteProc trace buffer.

It can enable clocks, assert/deassert reset, load an ELF, start/stop the core,
and process mailbox/RemoteProc events.

It does **not** expose a documented RISC-V Debug Module (DMI), MDB, or DMEM
register interface that OpenOCD can use to halt, single-step, read arbitrary
CSRs, or set hardware breakpoints.

The T527 User Manual documents the RISC-V CLINT aperture at `0xE0000000` and
CLIC at `0xE0800000`, but no Linux-visible debug-module binding/register block
was found in the available manual, Device Tree, or local RemoteProc sources.

## What works now

Use small firmware apps and text-only Linux observation. Each app validates one
hardware assumption and reports through the shared RemoteProc trace buffer.

The initial app is `timer_test.elf`:

```text
bare-C firmware -> CLINT mtime read -> shared trace buffer -> debugfs trace0 -> Python poller
```

`tools/remoteproc_control.py` can load the test, poll `trace0` for its expected
pass text, return pass/fail, and stop RemoteProc. See
[`REMOTE_PROCESSOR_CONTROL.md`](REMOTE_PROCESSOR_CONTROL.md) for build and load
instructions.

For faults, firmware should publish `mcause`, `mepc`, and `mtval` in the shared
boot/crash status record before sending a mailbox notification. Linux can then
save the record and trace output before resetting RemoteProc.

## OpenOCD and GDB

OpenOCD needs a transport to the RISC-V Debug Module, normally external JTAG.
RemoteProc memory loading/reset does not provide that transport by itself.

An internal OpenOCD solution is possible only if a verified, Linux-accessible
DMI/debug-module transport is found and an appropriate OpenOCD adapter/driver
is implemented. Do not assume that a reference to MDB or DMEM is such an
interface until its address map and access semantics are verified.

## JTAG-free GDB option

A practical later option is a small firmware-side GDB Remote Serial Protocol
stub. GDB on Linux would connect to the stub over a bidirectional mailbox/shared
SRAM or RPMsg transport. This supports source-level inspection without external
JTAG pins, but requires its own command transport, register-save/restore logic,
and a safe halt/resume design.

It is intentionally deferred until the focused timer, mailbox, shared-SRAM, and
crash-reporting tests are validated on hardware.
