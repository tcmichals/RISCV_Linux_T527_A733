# Linux RemoteProc Control Tool

[`tools/remoteproc_control.py`](../tools/remoteproc_control.py) is a
stdlib-only, text-only Python utility for RemoteProc lifecycle operations and
the shared clock, status, and IPC descriptor ABIs.

## Safety model

The tool does **not** write `/dev/mem`. `write-clock` and `read-status` require
a platform-provided kernel driver device mapping only the relevant 4 KB shared
SRAM region. The kernel RemoteProc/IPC integration must provide that controlled
interface.

Run `self-test` before deployment. It verifies Python packing matches the C++
clock/status structures (64 bytes) and IPC descriptor (16 bytes).

## Generate IPC messages

`build-ipc` creates the binary wire image of
`abstractx::ipc::IpcDescriptor`: 32-bit payload address, 32-bit length, 16-bit
message type, 16-bit flags, and 32-bit timestamp. It prints hexadecimal text by
default and can write a binary 16-byte descriptor for a platform IPC driver.

```text
remoteproc_control.py self-test
remoteproc_control.py build-ipc --type imu-burst-data --payload-addr 0x48100000 --length 256 --timestamp-us 123456
remoteproc_control.py build-ipc --type heartbeat --payload-addr 0 --length 0 --timestamp-us 123456 --output heartbeat.desc
```

Supported message-type names are `heartbeat`, `pigweed-log`, `barectf-trace`,
`pcie-tlp-stream`, and `imu-burst-data`.

Creating a descriptor is not the same as enqueueing it: the platform kernel IPC
driver must validate it, write it to the Linux-to-firmware ring, advance the
producer index, and ring the mailbox. This tool deliberately does not access
that raw ring directly.

## Text-only lifecycle

```text
remoteproc_control.py --dry-run --remoteproc 0 load --firmware sun55i-e907-fw.elf
remoteproc_control.py write-clock --device /dev/riscv-clock-config --generation 1 --riscv-core-hz 600000000 --timer-counter-hz 24000000
remoteproc_control.py --remoteproc 0 load --firmware sun55i-e907-fw.elf
remoteproc_control.py read-status --device /dev/riscv-boot-status
remoteproc_control.py capture-trace --source /sys/kernel/debug/remoteproc/remoteproc0/trace0 --output remoteproc-trace.txt
remoteproc_control.py watch-trace --source /sys/kernel/debug/remoteproc/remoteproc0/trace0 --interval 1
remoteproc_control.py --remoteproc 0 stop
```

`capture-trace` saves debugfs output verbatim for inspection with normal text
tools. `watch-trace` polls a debugfs text trace and prints it only when the
firmware changes it. No GUI trace viewer is required.

## Automated focused firmware tests

Each test firmware image validates one layer only. The Python runner stops the
selected RemoteProc, loads the test image, starts it, polls `trace0` for the
test's pass text, and stops it again. It returns zero only on a match.

```text
remoteproc_control.py list-tests
remoteproc_control.py --dry-run --remoteproc 0 run-test --test timer --firmware-dir build-rv --trace-source /sys/kernel/debug/remoteproc/remoteproc0/trace0
remoteproc_control.py --remoteproc 0 run-test --test timer --firmware-dir build-rv --trace-source /sys/kernel/debug/remoteproc/remoteproc0/trace0 --timeout 10
```

Use `--keep-running` only when manual inspection is required after a pass. Add
new test apps as independent registry entries with one expected trace string;
do not combine unvalidated peripherals with a lower-level platform test.

## Bare-C timer test

`timer_test.elf` is the first hardware test. It requires no C++ runtime,
coroutines, IPC messages, clock handoff, or timer interrupts. It polls the
RISC-V CLINT time counter and alternates `alive A`/`alive B` text in the
RemoteProc trace buffer once per configured counter-second. The alternating text
makes each heartbeat visible to the Python poller.

### Build

The configured CMake toolchain searches `~/.tools` for xPack RISC-V GCC first.
If it is absent, CMake downloads xPack 15.2.0-1 to
`~/.tools/xpack-riscv-none-elf-gcc-15.2.0-1`; an internet connection is required
only for that first configuration. Build this independent target from the
repository root:

```text
cmake -S . -B build-rv-timer -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-toolchain.cmake -DBUILD_CPPUTEST_TESTS=OFF -DBUILD_RISCV_FIRMWARE=ON
cmake --build build-rv-timer --target timer_test.elf -j2
```

The loadable artifact is `build-rv-timer/timer_test.elf`. Its map file is
`build-rv-timer/timer_test.map`; it must show `.trace_ctf_buffer` at
`0x07138000`.

### Install and run on the board

The RemoteProc firmware loader selects a **file name**, not a host build path.
Copy the ELF to the board's configured firmware directory (typically
`/lib/firmware`) using the same name:

```text
scp build-rv-timer/timer_test.elf root@<board>:/lib/firmware/timer_test.elf
```

On the board, confirm that `remoteproc0` is the RISC-V instance and that its
Device Tree/resource table exports the trace buffer as `trace0`. Debugfs must be
mounted before the trace path exists. Then run the automated text-only test from
the board (or use an equivalent board-side Python installation):

```text
python3 tools/remoteproc_control.py --remoteproc 0 run-test --test timer --firmware-dir /lib/firmware --trace-source /sys/kernel/debug/remoteproc/remoteproc0/trace0 --timeout 10
```

The runner stops `remoteproc0`, selects `timer_test.elf`, starts it, waits for
the expected text, and stops it after the result. For manual inspection, start
the image and leave it running:

```text
python3 tools/remoteproc_control.py --remoteproc 0 load --firmware timer_test.elf
python3 tools/remoteproc_control.py watch-trace --source /sys/kernel/debug/remoteproc/remoteproc0/trace0 --interval 1
```

A pass proves only this path:

```text
bare-C firmware -> CLINT mtime read -> shared trace buffer -> debugfs trace0 -> Python poller
```

It does not validate machine-timer interrupts, coroutines, IPC rings, or
peripheral clocks; add each as its own focused firmware test.
