#!/usr/bin/env python3
# =============================================================================
# URTC - tools/bootloader_size.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Measure the bootloaders with arm-none-eabi-size and fail when one does not fit its reserved region with the configured margin.

    python tools/bootloader_size.py [--config tools/bootloader_size.json] [--summary FILE]

For every bootloader of the configuration file it reads the ELF (Berkeley format: text, data, bss), takes the size of the FLASH and RAM regions from the
linker script, and works out the flash used (text + data: the initialised data is stored in flash) and the RAM used (data + bss). The result goes to
the standard output as a Markdown table (and is appended to --summary, for the job summary of a workflow). The exit status is 1 when the free flash is
below ``flash_margin_bytes`` or the free RAM below ``ram_margin_bytes`` of a bootloader, or when an ELF is missing. Nothing is compiled here.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "tools" / "bootloader_size.json"
_UNITS = {"": 1, "K": 1024, "M": 1024 * 1024}
_REGION = re.compile(r"^\s*(\w+)\s*\([rwxai!]+\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*LENGTH\s*=\s*(\d+)\s*([KM]?)", re.MULTILINE)


def regions(linker_text: str) -> dict[str, int]:
    """The length in bytes of every memory region of a linker script."""
    return {m.group(1): int(m.group(3)) * _UNITS[m.group(4)] for m in _REGION.finditer(linker_text)}


def parse_size(output: str) -> tuple[int, int, int]:
    """text, data, bss from the last line of ``arm-none-eabi-size`` (Berkeley format)."""
    columns = output.strip().splitlines()[-1].split()
    return int(columns[0]), int(columns[1]), int(columns[2])


def measure(entry: dict, size_tool: str = "arm-none-eabi-size") -> dict:
    elfs = sorted(ROOT.glob(entry["elf_glob"]))
    if len(elfs) != 1:
        raise FileNotFoundError(f"{entry['elf_glob']}: expected one ELF, found {len(elfs)}")
    found = regions((ROOT / entry["linker"]).read_text(encoding="utf-8"))
    flash_name, ram_name = entry.get("flash_region", "FLASH"), entry.get("ram_region", "RAM")
    if flash_name not in found or ram_name not in found:
        raise KeyError(f"{entry['linker']}: regions {flash_name}/{ram_name} not found (has {sorted(found)})")
    text, data, bss = parse_size(subprocess.run([size_tool, str(elfs[0])], check=True, capture_output=True, text=True).stdout)
    flash_used, ram_used = text + data, data + bss
    return {
        "elf": elfs[0].name, "text": text, "data": data, "bss": bss,
        "flash_used": flash_used, "flash_region": found[flash_name], "flash_free": found[flash_name] - flash_used,
        "ram_used": ram_used, "ram_region": found[ram_name], "ram_free": found[ram_name] - ram_used,
        "flash_margin": int(entry["flash_margin_bytes"]), "ram_margin": int(entry["ram_margin_bytes"]),
    }


def evaluate(config: dict, size_tool: str = "arm-none-eabi-size") -> tuple[list[tuple[str, dict]], list[str]]:
    results, problems = [], []
    for name, entry in config["bootloaders"].items():
        try:
            row = measure(entry, size_tool)
        except (FileNotFoundError, KeyError, subprocess.CalledProcessError, OSError, ValueError) as error:
            problems.append(f"{name}: {error}")
            continue
        results.append((name, row))
        if row["flash_free"] < row["flash_margin"]:
            problems.append(f"{name}: {row['flash_free']} B of flash free, below the margin of {row['flash_margin']} B (region {row['flash_region']} B)")
        if row["ram_free"] < row["ram_margin"]:
            problems.append(f"{name}: {row['ram_free']} B of RAM free, below the margin of {row['ram_margin']} B (region {row['ram_region']} B)")
    return results, problems


def markdown(results: list[tuple[str, dict]], problems: list[str]) -> str:
    lines = [
        "### Bootloader size (arm-none-eabi-size)", "",
        "| Bootloader | text | data | bss | Flash used / region | Flash free (margin) | RAM used / region | RAM free (margin) |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for name, r in results:
        lines.append(f"| {name} | {r['text']} | {r['data']} | {r['bss']} | {r['flash_used']} / {r['flash_region']} | {r['flash_free']} ({r['flash_margin']}) "
                     f"| {r['ram_used']} / {r['ram_region']} | {r['ram_free']} ({r['ram_margin']}) |")
    lines += ["", "Flash used = text + data; RAM used = data + bss (bytes)."]
    if problems:
        lines += ["", "**Too big:**"] + [f"- {p}" for p in problems]
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default=str(CONFIG))
    parser.add_argument("--summary", help="append the Markdown table to this file (the job summary)")
    args = parser.parse_args(argv)
    config = json.loads(Path(args.config).read_text(encoding="utf-8"))
    results, problems = evaluate(config)
    text = markdown(results, problems)
    print(text)
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as handle:
            handle.write(text)
    for problem in problems:
        print(f"::error::{problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
