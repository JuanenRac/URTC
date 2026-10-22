#!/usr/bin/env python3
# =============================================================================
# URTC - tools/fuzz_virtual_board.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Throws the frames of the protocol specification (valid, invalid and random) at the virtual board.

The virtual board (tests/virtual_board.c) is the real receive path of the firmware compiled for the host with the address and
undefined-behaviour sanitizers, behind a text port that speaks SLCAN lines. This tool builds it, sends it every frame of
tests/fuzz/fuzz_corpus.txt (a copy of the corpus made by URTC-COMMON from the specification) plus frames of its own, for several
tool profiles and with the optional features off and on, and checks, for every run:

* the program did not stop (a sanitizer report, a crash or a hang ends the run with a non-zero status);
* every line it printed is a well-formed SLCAN frame whose identifier is one the board may transmit (the list is in the corpus);
* the board reset into its bootloader exactly when the frame was 0x7F0 with the four magic bytes and nothing else.

    python tools/fuzz_virtual_board.py                 # build and run
    python tools/fuzz_virtual_board.py --seed 7 --extra 5000
    python tools/fuzz_virtual_board.py --build-dir D   # where the binary goes (default: build/)

On Windows the build and the run go through WSL (the fake flash needs a 64-bit Linux host), like tools/run_host_tests.py.
Exit status: 0 passed, 1 a check failed, 2 skipped because no host compiler was found.
"""
from __future__ import annotations

import argparse
import os
import random
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORPUS = ROOT / "tests" / "fuzz" / "fuzz_corpus.txt"
MASTER = "src/F303-master"
SOURCES = (
    "tests/virtual_board.c", "tests/host_hal/fake_hal.c",
    f"{MASTER}/firmware_can_dispatch.c", f"{MASTER}/firmware_can_dispatch_routing.c", f"{MASTER}/firmware_can_global_pre.c",
    f"{MASTER}/firmware_thermal_stats.c", f"{MASTER}/thermal_stats.c", f"{MASTER}/firmware_blackbox.c", f"{MASTER}/firmware_fram.c",
    f"{MASTER}/fw_features.c", f"{MASTER}/firmware_features.c", f"{MASTER}/pid_control.c", f"{MASTER}/health_profile.c",
    f"{MASTER}/hsi_trim.c", f"{MASTER}/motion_profile.c",
)
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-cpp", "-Wno-int-to-pointer-cast",
         "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g"]
INCLUDES = ["tests", "tests/host_hal", MASTER, "src/common"]
MAGIC = bytes.fromhex("B0071D5A")
FRAME_LINE = re.compile(r"^t([0-7][0-9A-F]{2})([0-8])((?:[0-9A-F]{2}){0,8})$")
# (tool profile, optional features mask): a tool of each family, features off and all on
RUNS = ((0, 0), (0, 0x0F), (1, 0), (10, 0x0F), (19, 0x0F), (22, 0), (5, 0x0F))


def load_corpus() -> tuple[list[tuple[int, bytes, str]], set[int]]:
    frames: list[tuple[int, bytes, str]] = []
    tx_ids: set[int] = set()
    for raw in CORPUS.read_text(encoding="utf-8").splitlines():
        if raw.startswith("# transmit_ids:"):
            tx_ids = {int(x, 16) for x in raw.split(":", 1)[1].split()}
            continue
        if not raw or raw.startswith("#"):
            continue
        parts = raw.split(" ", 3)
        frames.append((int(parts[0], 16), b"" if parts[1] == "-" else bytes.fromhex(parts[1]), parts[2]))
    return frames, tx_ids


def extra_frames(seed: int, count: int, known: list[int]) -> list[tuple[int, bytes, str]]:
    """Frames made here, not by the specification: bit flips of corpus frames and edge cases the corpus does not keep."""
    rng = random.Random(seed)
    out = []
    for _ in range(count):
        can_id = rng.choice(known) if rng.random() < 0.8 else rng.randrange(0, 0x800)
        data = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 9)))
        if rng.random() < 0.15:
            can_id = 0x7F0
            data = MAGIC[: rng.randrange(0, 5)] + data[:4]
            data = data[: rng.randrange(0, 9)]
        out.append((can_id, data, "extra"))
    return out


def _wsl_path(value: str) -> str:
    match = re.match(r"^([A-Za-z]):[\\/](.*)$", value)
    return f"/mnt/{match.group(1).lower()}/{match.group(2).replace(chr(92), '/')}" if match else value


def build(binary: Path) -> list[str] | None:
    """Compile the virtual board; returns the command prefix that runs it (through WSL on Windows), or None when it cannot be built."""
    inc = [f"-I{i}" for i in INCLUDES]
    if os.name == "nt":
        wsl = shutil.which("wsl")
        if wsl is None:
            return None
        rel = binary.resolve().relative_to(ROOT).as_posix() if str(binary.resolve()).startswith(str(ROOT)) else None
        out = _wsl_path(str(binary)) if rel is None else rel
        cmd = [wsl, "--cd", str(ROOT), "--", "gcc", *FLAGS, *inc, "-o", out, *SOURCES]
        if subprocess.run(cmd).returncode != 0:
            raise SystemExit("FUZZ=FAIL the virtual board did not compile")
        return [wsl, "--cd", str(ROOT), "--", out]
    cc = shutil.which("cc") or shutil.which("gcc")
    if cc is None:
        return None
    cmd = [cc, *FLAGS, *[f"-I{ROOT / i}" for i in INCLUDES], "-o", str(binary), *[str(ROOT / s) for s in SOURCES]]
    if subprocess.run(cmd, cwd=ROOT).returncode != 0:
        raise SystemExit("FUZZ=FAIL the virtual board did not compile")
    return [str(binary)]


def slcan(can_id: int, data: bytes) -> str:
    return f"t{can_id:03X}{len(data)}{data.hex().upper()}"


def run_once(prefix: list[str], tool: int, features: int, frames: list[tuple[int, bytes, str]], tx_ids: set[int]) -> list[str]:
    """Send the frames; returns the problems found (empty when the run is clean)."""
    problems: list[str] = []
    lines = ["S6", "O"] + [slcan(i, d) for i, d, _ in frames] + ["C"]
    # one frame per line; the program answers each one before it reads the next, so the output is split by frame with a marker
    stdin = "\n".join(lines) + "\n"
    try:
        result = subprocess.run([*prefix, str(tool), str(features)], input=stdin, capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return [f"tool {tool} features {features:#x}: the board did not finish (hang)"]
    tag = f"tool {tool} features {features:#x}"
    if result.returncode != 0:
        tail = (result.stderr or "").strip().splitlines()[-6:]
        return [f"{tag}: the board stopped with status {result.returncode}: " + " | ".join(tail)]
    resets = 0
    for out_line in result.stdout.replace("\r", "").split("\n"):
        if out_line == "X":
            resets += 1
            continue
        if out_line in ("", "\a") or re.fullmatch(r"V\d{4}", out_line):
            continue
        m = FRAME_LINE.match(out_line)
        if not m:
            problems.append(f"{tag}: malformed output line {out_line!r}")
            continue
        if int(m.group(1), 16) not in tx_ids:
            problems.append(f"{tag}: the board transmitted 0x{m.group(1)}, which is not an identifier it may send")
        if len(m.group(3)) != int(m.group(2)) * 2:
            problems.append(f"{tag}: length digit and data disagree in {out_line!r}")
    expected = sum(1 for i, d, _ in frames if i == 0x7F0 and d == MAGIC)
    if resets != expected:
        problems.append(f"{tag}: {resets} reset(s) into the bootloader, expected {expected} (only 0x7F0 with the exact magic may reset)")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--extra", type=int, default=2000, help="frames made here in addition to the corpus")
    parser.add_argument("--build-dir", default=None)
    args = parser.parse_args(argv)
    if not CORPUS.is_file():
        print("FUZZ=FAIL tests/fuzz/fuzz_corpus.txt is missing", file=sys.stderr)
        return 1
    build_dir = Path(args.build_dir) if args.build_dir else ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    prefix = build(build_dir / "virtual_board")
    if prefix is None:
        print("FUZZ=SKIPPED no host C compiler (install gcc, or WSL on Windows)")
        return 2
    frames, tx_ids = load_corpus()
    if not tx_ids:
        print("FUZZ=FAIL the corpus has no transmit_ids line", file=sys.stderr)
        return 1
    frames += extra_frames(args.seed, args.extra, sorted({i for i, _, _ in frames}))
    problems: list[str] = []
    for tool, features in RUNS:
        problems += run_once(prefix, tool, features, frames, tx_ids)
    if problems:
        print("\n".join(problems[:40]), file=sys.stderr)
        print(f"FUZZ=FAIL {len(problems)} problem(s) in {len(RUNS)} runs of {len(frames)} frames", file=sys.stderr)
        return 1
    print(f"FUZZ=PASS {len(frames)} frames x {len(RUNS)} runs, address and undefined-behaviour sanitizers, no problem")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
