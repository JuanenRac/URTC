#!/usr/bin/env python3
# =============================================================================
# URTC - tools/compare_builds.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Compares the firmware images that build_firmware.sh and build_firmware.bat make from the same commit.

    python tools/compare_builds.py SH_DIR BAT_DIR [--summary FILE]

Both folders hold the URTC_*.bin files of one build. The two scripts are written by hand for their shell (only the data
blocks come from tools/build_plan.json), so the only proof that they build the same program is that the images are the
same bytes. Prints COMPARE_BUILDS=PASS and exits 0 when both folders hold the same set of images and every image has the
same bytes; otherwise it names each image that is missing on one side or differs (the first byte that differs and the
sizes) and exits 1.
"""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path


def images(folder: Path) -> dict[str, bytes]:
    return {path.name: path.read_bytes() for path in sorted(folder.glob("URTC_*.bin"))}


def compare(sh: dict[str, bytes], bat: dict[str, bytes]) -> list[str]:
    """The differences between the two sets of images, one line each; empty when the builds are the same."""
    problems: list[str] = []
    if not sh and not bat:
        return ["neither folder holds a URTC_*.bin image"]
    for name in sorted(set(sh) - set(bat)):
        problems.append(f"{name}: made by build_firmware.sh only")
    for name in sorted(set(bat) - set(sh)):
        problems.append(f"{name}: made by build_firmware.bat only")
    for name in sorted(set(sh) & set(bat)):
        a, b = sh[name], bat[name]
        if a == b:
            continue
        first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
        problems.append(f"{name}: differs (sh {len(a)} bytes, bat {len(b)} bytes, first different byte at offset {first})")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("sh_dir", type=Path)
    parser.add_argument("bat_dir", type=Path)
    parser.add_argument("--summary", type=Path, help="append a table of the images to this file (the job summary)")
    args = parser.parse_args(argv)
    sh, bat = images(args.sh_dir), images(args.bat_dir)
    problems = compare(sh, bat)
    if args.summary:
        with args.summary.open("a", encoding="utf-8") as out:
            out.write("| image | sh SHA-256 | bat SHA-256 |\n| --- | --- | --- |\n")
            for name in sorted(set(sh) | set(bat)):
                digest = lambda data: hashlib.sha256(data).hexdigest()[:16] if data is not None else "-"  # noqa: E731
                out.write(f"| {name} | {digest(sh.get(name))} | {digest(bat.get(name))} |\n")
    for line in problems:
        print(f"  DIFF {line}")
    print(f"COMPARE_BUILDS={'FAIL' if problems else 'PASS'} images={len(set(sh) | set(bat))}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
