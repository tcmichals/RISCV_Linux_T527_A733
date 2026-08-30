#!/usr/bin/env python3
"""Report RISC-V firmware memory usage per T527/A733 region, plus the RemoteProc
load/start information (entry point and LOAD segments).

Usage:
    tools/fw_size.py build-rv/hello_world.elf [more.elf ...]
    tools/fw_size.py            # defaults to build-rv/*.elf
    tools/fw_size.py --json build-rv/hello_world.elf
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import struct
import sys

# Regions must stay in sync with hal/include/memory_map.h and
# hal/src/riscv_memory_map.ld.
REGIONS = [
    ("ITCM",        0x00000000, 64 * 1024,        "Vectors + fastcode (zero-wait)"),
    ("DTCM",        0x00080000, 64 * 1024,        "Data, BSS, stack"),
    ("SRAM_SHARED", 0x07130000, 64 * 1024,        "IPC rings, trace buffer (Linux visible)"),
    ("SRAM_CODE",   0x07140000, 256 * 1024,       "Runtime text + coroutine pool"),
    ("DRAM_INIT",   0x48000000, 1 * 1024 * 1024,  "Init code + load images"),
    ("DRAM_BUF",    0x48100000, 15 * 1024 * 1024, "DMA payload pool"),
]

SHT_NOBITS = 8
SHF_ALLOC = 0x2
PT_LOAD = 1


class ElfError(Exception):
    pass


class Elf32:
    def __init__(self, path: str) -> None:
        with open(path, "rb") as handle:
            self.blob = handle.read()
        self.path = path
        self._parse_header()
        self._parse_sections()
        self._parse_segments()
        self._parse_symbols()

    def _parse_header(self) -> None:
        if self.blob[:4] != b"\x7fELF":
            raise ElfError(f"{self.path}: not an ELF file")
        if self.blob[4] != 1:
            raise ElfError(f"{self.path}: only 32-bit ELF is supported")
        if self.blob[5] != 1:
            raise ElfError(f"{self.path}: only little-endian ELF is supported")

        (self.e_entry, self.e_phoff, self.e_shoff) = struct.unpack_from("<III", self.blob, 24)
        (self.e_phentsize, self.e_phnum, self.e_shentsize, self.e_shnum,
         self.e_shstrndx) = struct.unpack_from("<HHHHH", self.blob, 42)

    def _cstr(self, table_offset: int, index: int) -> str:
        start = table_offset + index
        end = self.blob.index(b"\0", start)
        return self.blob[start:end].decode("utf-8", "replace")

    def _parse_sections(self) -> None:
        raw = []
        for i in range(self.e_shnum):
            base = self.e_shoff + i * self.e_shentsize
            fields = struct.unpack_from("<IIIIIIIIII", self.blob, base)
            raw.append(fields)

        shstr_offset = raw[self.e_shstrndx][4]
        self.sections = []
        for name_idx, sh_type, flags, addr, offset, size, link, info, align, entsize in raw:
            self.sections.append({
                "name": self._cstr(shstr_offset, name_idx),
                "type": sh_type,
                "flags": flags,
                "addr": addr,
                "offset": offset,
                "size": size,
                "link": link,
                "entsize": entsize,
            })

    def _parse_segments(self) -> None:
        self.segments = []
        for i in range(self.e_phnum):
            base = self.e_phoff + i * self.e_phentsize
            (p_type, p_offset, p_vaddr, p_paddr, p_filesz,
             p_memsz, p_flags, p_align) = struct.unpack_from("<IIIIIIII", self.blob, base)
            self.segments.append({
                "type": p_type,
                "offset": p_offset,
                "vaddr": p_vaddr,
                "paddr": p_paddr,
                "filesz": p_filesz,
                "memsz": p_memsz,
                "flags": p_flags,
            })

    def _parse_symbols(self) -> None:
        self.symbols = {}
        for section in self.sections:
            if section["name"] != ".symtab":
                continue
            strtab = self.sections[section["link"]]["offset"]
            count = section["size"] // 16
            for i in range(count):
                base = section["offset"] + i * 16
                st_name, st_value, _st_size, _info, _other, _shndx = struct.unpack_from(
                    "<IIIBBH", self.blob, base)
                if st_name:
                    self.symbols[self._cstr(strtab, st_name)] = st_value


def region_of(addr: int, size: int = 0):
    for name, origin, length, _desc in REGIONS:
        if origin <= addr < origin + length:
            return name
    return None


def human(n: int) -> str:
    if n >= 1024 * 1024:
        return f"{n / (1024 * 1024):.1f} MB"
    if n >= 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n} B"


def bar(fraction: float, width: int = 28) -> str:
    fraction = max(0.0, min(1.0, fraction))
    filled = int(round(fraction * width))
    return "#" * filled + "." * (width - filled)


def analyze(elf: Elf32) -> dict:
    alloc_sections = [
        s for s in elf.sections
        if (s["flags"] & SHF_ALLOC) and s["size"] > 0
    ]

    usage = {name: {"used": 0, "sections": []} for name, _o, _l, _d in REGIONS}
    orphans = []
    for section in alloc_sections:
        name = region_of(section["addr"])
        if name is None:
            orphans.append(section)
            continue
        usage[name]["used"] += section["size"]
        usage[name]["sections"].append(section)

    return {"usage": usage, "orphans": orphans}


def report(elf: Elf32) -> int:
    analysis = analyze(elf)
    usage = analysis["usage"]
    status = 0

    print("=" * 78)
    print(f"{os.path.basename(elf.path)}")
    print("=" * 78)

    entry_region = region_of(elf.e_entry) or "UNKNOWN"
    entry_syms = sorted(n for n, v in elf.symbols.items() if v == elf.e_entry)
    print("\nRemoteProc load/start")
    print("-" * 78)
    print(f"  entry point : 0x{elf.e_entry:08X}  ({entry_region})"
          f"{'  ' + ', '.join(entry_syms) if entry_syms else ''}")

    rsrc = next((s for s in elf.sections if s["name"] == ".resource_table"), None)
    if rsrc:
        print(f"  resource tbl: 0x{rsrc['addr']:08X}  ({region_of(rsrc['addr'])}), "
              f"{rsrc['size']} bytes")
    else:
        print("  resource tbl: MISSING - RemoteProc cannot load this image")
        status = 1

    print("\n  LOAD segments (what RemoteProc copies):")
    print(f"    {'vaddr':>10} {'paddr':>10} {'filesz':>9} {'memsz':>9}  region")
    loads = [s for s in elf.segments if s["type"] == PT_LOAD]
    if not loads:
        print("    (none)")
    for seg in loads:
        print(f"    0x{seg['vaddr']:08X} 0x{seg['paddr']:08X} "
              f"{seg['filesz']:>9} {seg['memsz']:>9}  "
              f"{region_of(seg['paddr']) or '?'}")

    print("\nRegion usage")
    print("-" * 78)
    for name, origin, length, desc in REGIONS:
        used = usage[name]["used"]
        frac = used / length if length else 0.0
        flag = ""
        if used > length:
            flag = "  *** OVERFLOW ***"
            status = 1
        elif frac > 0.90:
            flag = "  ** >90% **"
        print(f"  {name:<12} 0x{origin:08X} {human(length):>8}  "
              f"[{bar(frac)}] {frac * 100:5.1f}%  used {human(used):>8}  "
              f"free {human(max(0, length - used)):>8}{flag}")
        print(f"  {'':<12} {desc}")

    print("\nSections by region")
    print("-" * 78)
    for name, _origin, _length, _desc in REGIONS:
        entries = usage[name]["sections"]
        if not entries:
            continue
        print(f"  {name}")
        for section in sorted(entries, key=lambda s: s["addr"]):
            kind = "NOBITS" if section["type"] == SHT_NOBITS else "PROGBITS"
            print(f"    0x{section['addr']:08X}  {section['size']:>8}  "
                  f"{section['name']:<22} {kind}")

    if analysis["orphans"]:
        status = 1
        print("\n  Sections outside every known region:")
        for section in analysis["orphans"]:
            print(f"    0x{section['addr']:08X}  {section['size']:>8}  {section['name']}")

    estack = elf.symbols.get("_estack")
    ebss = elf.symbols.get("_ebss")
    if estack and ebss:
        headroom = estack - ebss
        print("\nStack headroom (DTCM)")
        print("-" * 78)
        print(f"  _ebss   0x{ebss:08X}")
        print(f"  _estack 0x{estack:08X}")
        note = ""
        if headroom < 4096:
            note = "  *** under 4 KB - raise DTCM headroom ***"
            status = 1
        print(f"  free    {human(headroom)}{note}")

    print()
    return status


def as_dict(elf: Elf32) -> dict:
    analysis = analyze(elf)
    return {
        "file": elf.path,
        "entry": elf.e_entry,
        "regions": {
            name: {
                "origin": origin,
                "length": length,
                "used": analysis["usage"][name]["used"],
                "free": length - analysis["usage"][name]["used"],
            }
            for name, origin, length, _desc in REGIONS
        },
        "load_segments": [
            {"vaddr": s["vaddr"], "paddr": s["paddr"],
             "filesz": s["filesz"], "memsz": s["memsz"]}
            for s in elf.segments if s["type"] == PT_LOAD
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("elf", nargs="*", help="ELF files (default: build-rv/*.elf)")
    parser.add_argument("--json", action="store_true", help="emit machine-readable output")
    args = parser.parse_args()

    paths = args.elf or sorted(glob.glob("build-rv/*.elf"))
    if not paths:
        print("no ELF files found; build first or pass paths explicitly", file=sys.stderr)
        return 2

    status = 0
    results = []
    for path in paths:
        try:
            elf = Elf32(path)
        except (ElfError, OSError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            status = 2
            continue
        if args.json:
            results.append(as_dict(elf))
        else:
            status |= report(elf)

    if args.json:
        print(json.dumps(results, indent=2))
    return status


if __name__ == "__main__":
    sys.exit(main())
