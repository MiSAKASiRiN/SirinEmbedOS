#!/usr/bin/env python3
"""Symbolize a SirinOS panic transcript with the exact firmware ELF."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


REGISTER_NAMES = (
    "R0", "R1", "R2", "R3", "R12", "LR", "PC", "xPSR",
    "raw", "frame", "MSP", "PSP", "EXC_RETURN", "CFSR",
    "HFSR", "MMFAR", "BFAR",
)

CFSR_CAUSES = (
    (1 << 0, "MemManage: IACCVIOL"),
    (1 << 1, "MemManage: DACCVIOL"),
    (1 << 3, "MemManage: MUNSTKERR"),
    (1 << 4, "MemManage: MSTKERR"),
    (1 << 5, "MemManage: MLSPERR"),
    (1 << 8, "BusFault: IBUSERR"),
    (1 << 9, "BusFault: PRECISERR"),
    (1 << 10, "BusFault: IMPRECISERR"),
    (1 << 11, "BusFault: UNSTKERR"),
    (1 << 12, "BusFault: STKERR"),
    (1 << 13, "BusFault: LSPERR"),
    (1 << 16, "UsageFault: UNDEFINSTR"),
    (1 << 17, "UsageFault: INVSTATE"),
    (1 << 18, "UsageFault: INVPC"),
    (1 << 19, "UsageFault: NOCP"),
    (1 << 24, "UsageFault: UNALIGNED"),
    (1 << 25, "UsageFault: DIVBYZERO"),
)


def parse_addresses(transcript: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for name in REGISTER_NAMES:
        match = re.search(rf"\b{re.escape(name)}=0x([0-9A-Fa-f]+)", transcript)
        if match:
            values[name] = int(match.group(1), 16)
    return values


def decode_cfsr(value: int) -> list[str]:
    return [description for mask, description in CFSR_CAUSES if value & mask]


def is_executable_address(address: int,
                          ranges: list[tuple[int, int]]) -> bool:
    normalized = address & ~1
    return any(start <= normalized < end for start, end in ranges)


def executable_ranges(objdump: str, elf: Path) -> list[tuple[int, int]]:
    output = run_tool([objdump, "-h", str(elf)])
    ranges: list[tuple[int, int]] = []
    pending: tuple[int, int] | None = None
    for line in output.splitlines():
        section = re.match(
            r"^\s*\d+\s+\S+\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+",
            line)
        if section:
            size = int(section.group(1), 16)
            start = int(section.group(2), 16)
            pending = (start, start + size) if size != 0 else None
        elif pending is not None:
            if "CODE" in line:
                ranges.append(pending)
            pending = None
    return ranges


def stack_code_candidates(
        transcript: str,
        ranges: list[tuple[int, int]] | None = None) -> list[int]:
    if ranges is None:
        ranges = [(0x08000000, 0x0A000000)]
    candidates: list[int] = []
    seen: set[int] = set()
    for line in transcript.splitlines():
        if "[STACK+" not in line:
            continue
        for token in re.findall(r"0x([0-9A-Fa-f]{8})", line):
            address = int(token, 16) & ~1
            if is_executable_address(address, ranges) and address not in seen:
                seen.add(address)
                candidates.append(address)
                if len(candidates) >= 24:
                    return candidates
    return candidates


def run_tool(command: list[str]) -> str:
    completed = subprocess.run(command, text=True, capture_output=True,
                               check=False)
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(f"{' '.join(command)} failed: {detail}")
    return completed.stdout.rstrip()


def symbolize(addr2line: str, elf: Path, label: str, address: int) -> None:
    normalized = address & ~1
    print(f"[{label}] 0x{address:08X}")
    output = run_tool([
        addr2line, "-a", "-f", "-C", "-e", str(elf),
        f"0x{normalized:08X}",
    ])
    for line in output.splitlines():
        print(f"  {line}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Decode and symbolize a SirinOS panic transcript.")
    parser.add_argument("--elf", required=True, type=Path,
                        help="the exact ELF flashed when the panic occurred")
    parser.add_argument("--log", default="-", type=Path,
                        help="panic text file, or '-' for stdin")
    parser.add_argument("--tool-prefix", default="arm-none-eabi-",
                        help="GNU Arm tool prefix")
    args = parser.parse_args(argv)

    if not args.elf.is_file():
        parser.error(f"ELF not found: {args.elf}")
    if str(args.log) == "-":
        transcript = sys.stdin.read()
    else:
        try:
            transcript = args.log.read_text(encoding="utf-8", errors="replace")
        except OSError as error:
            parser.error(str(error))

    values = parse_addresses(transcript)
    if "PC" not in values:
        parser.error("panic transcript does not contain PC=0x...")

    addr2line = f"{args.tool_prefix}addr2line"
    objdump = f"{args.tool_prefix}objdump"
    for tool in (addr2line, objdump):
        if shutil.which(tool) is None:
            parser.error(f"required tool not found: {tool}")

    print(f"ELF: {args.elf.resolve()}")
    try:
        code_ranges = executable_ranges(objdump, args.elf)
    except RuntimeError as error:
        print(f"decode_panic: {error}", file=sys.stderr)
        return 2
    if not code_ranges:
        parser.error("ELF does not contain an executable section")
    cfsr = values.get("CFSR")
    if cfsr is not None:
        causes = decode_cfsr(cfsr)
        print(f"CFSR=0x{cfsr:08X}: " +
              (", ".join(causes) if causes else "no decoded cause bit"))

    try:
        symbolize(addr2line, args.elf, "PC", values["PC"])
        if "LR" in values:
            if is_executable_address(values["LR"], code_ranges):
                symbolize(addr2line, args.elf, "LR", values["LR"])
            else:
                print(f"[LR] 0x{values['LR']:08X} is outside ELF code sections")

        excluded = {values["PC"] & ~1}
        if "LR" in values:
            excluded.add(values["LR"] & ~1)
        candidates = [address for address in
                      stack_code_candidates(transcript, code_ranges)
                      if address not in excluded]
        if candidates:
            print("Stack return-address candidates:")
            for index, address in enumerate(candidates):
                symbolize(addr2line, args.elf, f"STACK#{index}", address)

        pc = values["PC"] & ~1
        start = max(0, pc - 16)
        stop = pc + 18
        print(f"Disassembly around PC 0x{pc:08X}:")
        disassembly = run_tool([
            objdump, "-d", "-S", "-C",
            f"--start-address=0x{start:X}",
            f"--stop-address=0x{stop:X}", str(args.elf),
        ])
        print(disassembly)
    except RuntimeError as error:
        print(f"decode_panic: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
