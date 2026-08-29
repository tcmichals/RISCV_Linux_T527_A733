#!/usr/bin/env python3
"""Linux-side RemoteProc control and shared-ABI utility for T527/A733.

This tool uses only normal file interfaces. To write the clock configuration,
pass a platform-provided kernel driver/device which maps IPC_CLOCK_CONFIG_ADDR;
/dev/mem is intentionally not supported.
"""

from __future__ import annotations

import argparse
import dataclasses
import shutil
import struct
import sys
import time
from pathlib import Path

CLOCK_MAGIC = 0x4B4C4343  # "CCLK" little-endian
BOOT_MAGIC = 0x544F4F42  # "BOOT" little-endian
ABI_VERSION = 1
ABI_SIZE = 64
CLOCK_FORMAT = "<16I"
STATUS_FORMAT = "<16I"
IPC_DESCRIPTOR_FORMAT = "<IIHHI"
GENERATION_OFFSET = 3 * struct.calcsize("<I")

STATE_NAMES = {0: "reset", 1: "initializing", 2: "ready", 3: "init-failed", 4: "crashed"}
FAILURE_NAMES = {
    0: "none",
    1: "clock-configuration-invalid",
    2: "timer-configuration-invalid",
    3: "unexpected-trap",
}
MESSAGE_TYPES = {
    "heartbeat": 0x0001,
    "pigweed-log": 0x0010,
    "barectf-trace": 0x0020,
    "pcie-tlp-stream": 0x0030,
    "imu-burst-data": 0x0040,
}


@dataclasses.dataclass(frozen=True)
class FirmwareTest:
    firmware_name: str
    expected_trace: str
    description: str


FIRMWARE_TESTS = {
    "timer": FirmwareTest(
        firmware_name="timer_test.elf",
        expected_trace="timer_test: alive",
        description="CLINT counter and text-only RemoteProc trace buffer",
    ),
}


@dataclasses.dataclass(frozen=True)
class ClockConfiguration:
    generation: int
    riscv_core_hz: int
    timer_counter_hz: int
    uart_parent_hz: int = 0
    spi0_parent_hz: int = 0
    spi1_parent_hz: int = 0
    flags: int = 0

    def pack(self, commit: bool = True) -> bytes:
        if self.generation <= 0 or self.riscv_core_hz <= 0 or self.timer_counter_hz <= 0:
            raise ValueError("generation, RISC-V core rate, and timer rate must be nonzero")
        return struct.pack(
            CLOCK_FORMAT,
            CLOCK_MAGIC,
            ABI_VERSION,
            ABI_SIZE,
            self.generation if commit else 0,
            self.riscv_core_hz,
            self.timer_counter_hz,
            self.uart_parent_hz,
            self.spi0_parent_hz,
            self.spi1_parent_hz,
            self.flags,
            0,
            0,
            0,
            0,
            0,
            0,
        )


@dataclasses.dataclass(frozen=True)
class BootStatus:
    magic: int
    abi_version: int
    size_bytes: int
    generation: int
    state: int
    failure_reason: int
    clock_configuration_generation: int
    riscv_core_hz: int
    mcause: int
    mepc: int
    mtval: int

    @classmethod
    def unpack(cls, payload: bytes) -> "BootStatus":
        if len(payload) != ABI_SIZE:
            raise ValueError(f"status record must be {ABI_SIZE} bytes, got {len(payload)}")
        fields = struct.unpack(STATUS_FORMAT, payload)
        return cls(*fields[:11])

    def is_valid(self) -> bool:
        return (
            self.magic == BOOT_MAGIC
            and self.abi_version == ABI_VERSION
            and self.size_bytes == ABI_SIZE
            and self.generation != 0
            and self.state in STATE_NAMES
        )


@dataclasses.dataclass(frozen=True)
class IpcDescriptor:
    """Wire-compatible with abstractx::ipc::IpcDescriptor in ipc_protocol.hpp."""

    dram_payload_addr: int
    length: int
    message_type: int
    flags: int
    timestamp_us: int

    def pack(self) -> bytes:
        values = (self.dram_payload_addr, self.length, self.message_type, self.flags, self.timestamp_us)
        limits = (0xFFFFFFFF, 0xFFFFFFFF, 0xFFFF, 0xFFFF, 0xFFFFFFFF)
        if any(value < 0 or value > limit for value, limit in zip(values, limits, strict=True)):
            raise ValueError("IPC descriptor fields exceed their unsigned wire widths")
        return struct.pack(IPC_DESCRIPTOR_FORMAT, *values)


def write_text(path: Path, value: str, dry_run: bool) -> None:
    if dry_run:
        print(f"DRY-RUN write {value!r} to {path}")
        return
    path.write_text(value, encoding="ascii")


def remoteproc_path(root: Path, remoteproc: int) -> Path:
    return root / f"remoteproc{remoteproc}"


def write_clock_configuration(path: Path, config: ClockConfiguration, dry_run: bool) -> None:
    staged = config.pack(commit=False)
    committed_generation = struct.pack("<I", config.generation)
    if dry_run:
        print(f"DRY-RUN write 64-byte clock config to {path}, then commit generation {config.generation}")
        return
    with path.open("r+b", buffering=0) as device:
        device.seek(0)
        device.write(staged)
        device.flush()
        device.seek(GENERATION_OFFSET)
        device.write(committed_generation)
        device.flush()


def read_status(path: Path) -> BootStatus:
    with path.open("rb") as device:
        payload = device.read(ABI_SIZE)
    return BootStatus.unpack(payload)


def watch_trace(path: Path, interval_seconds: float, count: int | None) -> int:
    if interval_seconds <= 0:
        raise ValueError("trace polling interval must be greater than zero")
    previous = None
    samples = 0
    while count is None or samples < count:
        current = path.read_text(encoding="utf-8", errors="replace").rstrip("\0")
        if current != previous:
            print(current, end="" if current.endswith("\n") else "\n", flush=True)
            previous = current
        samples += 1
        if count is None or samples < count:
            time.sleep(interval_seconds)
    return 0


def wait_for_trace(path: Path, expected_text: str, timeout_seconds: float, interval_seconds: float) -> bool:
    if timeout_seconds <= 0 or interval_seconds <= 0:
        raise ValueError("trace timeout and polling interval must be greater than zero")
    deadline = time.monotonic() + timeout_seconds
    previous = None
    while time.monotonic() < deadline:
        current = path.read_text(encoding="utf-8", errors="replace").rstrip("\0")
        if current != previous:
            print(current, end="" if current.endswith("\n") else "\n", flush=True)
            previous = current
        if expected_text in current:
            return True
        time.sleep(interval_seconds)
    return False


def print_status(status: BootStatus) -> int:
    print(f"valid: {status.is_valid()}")
    print(f"generation: {status.generation}")
    print(f"state: {STATE_NAMES.get(status.state, 'unknown')} ({status.state})")
    print(f"failure: {FAILURE_NAMES.get(status.failure_reason, 'unknown')} ({status.failure_reason})")
    print(f"clock configuration generation: {status.clock_configuration_generation}")
    print(f"RISC-V core clock: {status.riscv_core_hz} Hz")
    print(f"mcause: 0x{status.mcause:08x}")
    print(f"mepc:   0x{status.mepc:08x}")
    print(f"mtval:  0x{status.mtval:08x}")
    return 0 if status.is_valid() else 2


def self_test() -> int:
    config = ClockConfiguration(7, 600_000_000, 24_000_000, 24_000_000, 200_000_000, 200_000_000)
    staged = config.pack(commit=False)
    committed = config.pack()
    assert len(staged) == ABI_SIZE
    assert struct.unpack_from("<I", staged, GENERATION_OFFSET)[0] == 0
    assert struct.unpack_from("<I", committed, GENERATION_OFFSET)[0] == 7
    descriptor = IpcDescriptor(0x48100000, 256, MESSAGE_TYPES["imu-burst-data"], 0, 123456)
    assert descriptor.pack() == struct.pack("<IIHHI", 0x48100000, 256, 0x0040, 0, 123456)
    status = BootStatus.unpack(struct.pack(STATUS_FORMAT, BOOT_MAGIC, ABI_VERSION, ABI_SIZE, 7, 2, 0, 7,
                                           600_000_000, 0, 0, 0, 0, 0, 0, 0, 0))
    assert status.is_valid()
    print("ABI self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--remoteproc-root", type=Path, default=Path("/sys/class/remoteproc"))
    parser.add_argument("--remoteproc", type=int, default=0)
    parser.add_argument("--dry-run", action="store_true")
    subparsers = parser.add_subparsers(dest="command", required=True)

    load = subparsers.add_parser("load", help="select firmware and start RemoteProc")
    load.add_argument("--firmware", required=True)

    subparsers.add_parser("stop", help="stop RemoteProc")
    subparsers.add_parser("state", help="read RemoteProc state")

    clock = subparsers.add_parser("write-clock", help="write the shared clock ABI through a kernel device")
    clock.add_argument("--device", type=Path, required=True, help="platform driver mapping IPC_CLOCK_CONFIG_ADDR")
    clock.add_argument("--generation", type=int, required=True)
    clock.add_argument("--riscv-core-hz", type=int, required=True)
    clock.add_argument("--timer-counter-hz", type=int, required=True)
    clock.add_argument("--uart-parent-hz", type=int, default=0)
    clock.add_argument("--spi0-parent-hz", type=int, default=0)
    clock.add_argument("--spi1-parent-hz", type=int, default=0)
    clock.add_argument("--flags", type=int, default=0)

    status = subparsers.add_parser("read-status", help="decode a 64-byte firmware boot/crash record")
    status.add_argument("--device", type=Path, required=True, help="platform driver mapping IPC_BOOT_STATUS_ADDR")

    descriptor = subparsers.add_parser("build-ipc", help="build a 16-byte IPC descriptor for a platform IPC driver")
    descriptor.add_argument("--type", choices=MESSAGE_TYPES, required=True)
    descriptor.add_argument("--payload-addr", type=lambda value: int(value, 0), required=True)
    descriptor.add_argument("--length", type=int, required=True)
    descriptor.add_argument("--flags", type=lambda value: int(value, 0), default=0)
    descriptor.add_argument("--timestamp-us", type=int, required=True)
    descriptor.add_argument("--output", type=Path, help="optional file to receive the binary 16-byte descriptor")

    capture = subparsers.add_parser("capture-trace", help="copy the RemoteProc debugfs trace stream")
    capture.add_argument("--source", type=Path, required=True, help="for example /sys/kernel/debug/remoteproc/remoteproc0/trace0")
    capture.add_argument("--output", type=Path, required=True)

    watch = subparsers.add_parser("watch-trace", help="poll a RemoteProc debugfs text trace and print changes")
    watch.add_argument("--source", type=Path, required=True, help="for example /sys/kernel/debug/remoteproc/remoteproc0/trace0")
    watch.add_argument("--interval", type=float, default=1.0, help="polling interval in seconds (default: 1)")
    watch.add_argument("--count", type=int, help="number of reads; omit to watch until interrupted")

    subparsers.add_parser("list-tests", help="list focused firmware tests available to run")
    run_test = subparsers.add_parser("run-test", help="load a firmware test and wait for its trace0 pass text")
    run_test.add_argument("--test", choices=FIRMWARE_TESTS, required=True)
    run_test.add_argument("--firmware-dir", type=Path, required=True)
    run_test.add_argument("--trace-source", type=Path, required=True)
    run_test.add_argument("--timeout", type=float, default=10.0)
    run_test.add_argument("--interval", type=float, default=0.25)
    run_test.add_argument("--keep-running", action="store_true")

    subparsers.add_parser("self-test", help="verify Python packing against the C++ ABI")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.command == "self-test":
        return self_test()
    if args.command == "list-tests":
        for name, test in FIRMWARE_TESTS.items():
            print(f"{name}: {test.firmware_name} — {test.description}")
        return 0
    if args.command == "write-clock":
        config = ClockConfiguration(args.generation, args.riscv_core_hz, args.timer_counter_hz,
                                    args.uart_parent_hz, args.spi0_parent_hz, args.spi1_parent_hz, args.flags)
        write_clock_configuration(args.device, config, args.dry_run)
        return 0
    if args.command == "read-status":
        return print_status(read_status(args.device))
    if args.command == "build-ipc":
        descriptor = IpcDescriptor(args.payload_addr, args.length, MESSAGE_TYPES[args.type],
                                   args.flags, args.timestamp_us)
        payload = descriptor.pack()
        print(payload.hex())
        if args.output:
            args.output.write_bytes(payload)
        return 0
    if args.command == "capture-trace":
        if args.dry_run:
            print(f"DRY-RUN copy {args.source} to {args.output}")
        else:
            shutil.copyfile(args.source, args.output)
        return 0
    if args.command == "watch-trace":
        if args.count is not None and args.count <= 0:
            raise ValueError("trace read count must be greater than zero")
        return watch_trace(args.source, args.interval, args.count)

    rproc = remoteproc_path(args.remoteproc_root, args.remoteproc)
    if args.command == "run-test":
        test = FIRMWARE_TESTS[args.test]
        firmware = args.firmware_dir / test.firmware_name
        if args.dry_run:
            print(f"DRY-RUN stop {rproc}, load {firmware}, start {rproc}")
            print(f"DRY-RUN wait up to {args.timeout:g}s for trace text: {test.expected_trace!r}")
            return 0
        write_text(rproc / "state", "stop", False)
        write_text(rproc / "firmware", firmware.name, False)
        write_text(rproc / "state", "start", False)
        passed = wait_for_trace(args.trace_source, test.expected_trace, args.timeout, args.interval)
        if not args.keep_running:
            write_text(rproc / "state", "stop", False)
        if passed:
            print(f"PASS: {args.test}")
            return 0
        print(f"FAIL: {args.test}; expected trace text was not observed within {args.timeout:g}s", file=sys.stderr)
        return 2
    if args.command == "load":
        write_text(rproc / "firmware", args.firmware, args.dry_run)
        write_text(rproc / "state", "start", args.dry_run)
    elif args.command == "stop":
        write_text(rproc / "state", "stop", args.dry_run)
    elif args.command == "state":
        print((rproc / "state").read_text(encoding="ascii").strip())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
