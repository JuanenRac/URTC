#!/usr/bin/env python3
# =============================================================================
# URTC - tools/gen_build_scripts.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Writes the shared data of the firmware build into build_firmware.sh and build_firmware.bat from one file, tools/build_plan.json.

The two scripts used to carry their own copy of the pinned vendor sources, the compiler and linker flags and the list of HAL modules, and a change made to one and
forgotten in the other built a different firmware on Windows than on Linux. The data now lives in the plan; this tool rewrites the lines between the markers

    # BUILD_PLAN_BEGIN name ... # BUILD_PLAN_END name        (build_firmware.sh)
    REM BUILD_PLAN_BEGIN name ... REM BUILD_PLAN_END name    (build_firmware.bat)

and leaves everything else of the scripts as it is (the steps differ in how each shell does them, not in what they compile).

    python tools/gen_build_scripts.py            rewrite the blocks
    python tools/gen_build_scripts.py --check    fail (exit 1) when a script differs from what the plan generates
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLAN = ROOT / "tools" / "build_plan.json"
SCRIPTS = {"sh": ROOT / "build_firmware.sh", "bat": ROOT / "build_firmware.bat"}


def load_plan(path: Path = PLAN) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _path(value: str, kind: str) -> str:
    """A plan path ({BUILD}/common/x) as the shell of the script writes it: $BUILD/common/x or the same with %BUILD% and backslashes."""
    if kind == "sh":
        return re.sub(r"\{(\w+)\}", r"$\1", value)
    return re.sub(r"\{(\w+)\}", r"%\1%", value).replace("/", "\\")


def blocks(plan: dict, kind: str) -> dict[str, list[str]]:
    flags = plan["flags"]
    includes = " ".join("-I" + _path(item, kind) for item in flags["c_includes"])
    cflags = f"{flags['cpu']} {flags['c_defines']} {includes} {flags['c_extra']}"
    cxxflags = f"{flags['cpu']} {flags['cxx_extra']}"
    ldcommon = f"{flags['cpu']} {flags['ld_extra']}"
    modules = " ".join(plan["hal_modules"])
    pins = plan["pins"]
    if kind == "sh":
        return {
            "pins": [f'{name}="{value}"' for name, value in pins.items()],
            "flags": [f'CFLAGS="{cflags}"', f'CXXFLAGS="{cxxflags}"', f'LDCOMMON="{ldcommon}"', "", f'HAL_MODULES="{modules}"'],
        }
    return {
        "pins": [f'set "{name}={value}"' for name, value in pins.items()],
        "flags": [f'set "CFLAGS={cflags}"', f'set "CXXFLAGS={cxxflags}"', f'set "LDCOMMON={ldcommon}"', "", f'set "HAL_MODULES={modules}"'],
    }


def rewrite(text: str, kind: str, plan: dict) -> str:
    """The script with every marked block replaced by what the plan generates; the newline style of the script is kept."""
    newline = "\r\n" if "\r\n" in text else "\n"
    comment = "#" if kind == "sh" else "REM"
    generated = blocks(plan, kind)
    for name, lines in generated.items():
        pattern = re.compile(rf"({re.escape(comment)} BUILD_PLAN_BEGIN {name}[^\r\n]*\r?\n)(.*?)({re.escape(comment)} BUILD_PLAN_END {name}\b)", re.S)
        match = pattern.search(text)
        if match is None:
            raise SystemExit(f"GEN_BUILD=FAIL the {kind} script has no BUILD_PLAN markers for '{name}'")
        body = newline.join(lines) + newline
        text = text[:match.start(2)] + body + text[match.start(3):]
    return text


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    plan = load_plan()
    differing = []
    for kind, path in SCRIPTS.items():
        current = path.read_bytes().decode("utf-8")
        wanted = rewrite(current, kind, plan)
        if wanted != current:
            differing.append(path.name)
            if not args.check:
                path.write_bytes(wanted.encode("utf-8"))
    if args.check:
        if differing:
            print(f"GEN_BUILD=FAIL {', '.join(differing)} differ from tools/build_plan.json (run: python tools/gen_build_scripts.py)")
            return 1
        print("GEN_BUILD=PASS build_firmware.sh and build_firmware.bat carry what tools/build_plan.json says")
        return 0
    print("GEN_BUILD=" + (f"WROTE {', '.join(differing)}" if differing else "PASS nothing to change"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
