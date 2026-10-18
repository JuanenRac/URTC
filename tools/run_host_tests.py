#!/usr/bin/env python3
# =============================================================================
# URTC - tools/run_host_tests.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0 - see LICENSE
# =============================================================================
"""Compiles and runs every host logic suite of the firmware (tests/test_*.c) with the host C compiler.

One list of suites for every entry point: build_firmware.sh and build_firmware.bat both call this tool, so a build
on Windows runs the same suites as a build on Linux.

    python tools/run_host_tests.py                 all suites
    python tools/run_host_tests.py --list          the suites and what they compile
    python tools/run_host_tests.py --only NAME     one suite
    python tools/run_host_tests.py --build-dir DIR where the test binaries go (default: build/)

The suites that use the fake HAL map a fake flash in the low 2 GiB and run under the address and undefined-behaviour
sanitizers, which needs a 64-bit Linux host: on Windows this tool runs itself inside WSL when it is installed, and
otherwise says that the suites were skipped (exit status 2), never that they passed.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MASTER = "src/F303-master"
BOOT = f"{MASTER}/boot"
MEL = f"{MASTER}/melexis_mlx90640"
FAKE_HAL = "tests/host_hal"
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
WARN = ["-Wall", "-Wextra", "-Wno-unused-parameter"]
FAKE_HAL_WARN = ["-Wno-cpp", "-Wno-int-to-pointer-cast"]


@dataclass(frozen=True)
class Suite:
    name: str
    test: str
    sources: tuple[str, ...]
    includes: tuple[str, ...]
    std: str = "gnu11"
    sanitize: bool = True
    extra: tuple[str, ...] = ()
    libs: tuple[str, ...] = ()


SUITES = (
    Suite("mlx90640_api", "tests/test_mlx90640_api.c", ("tests/fake_mlx90640_i2c.c", f"{MEL}/MLX90640_API.c"), ("tests", MEL), std="c11", sanitize=False, libs=("-lm",)),
    Suite("can_dispatch_routing", "tests/test_can_dispatch_routing.c", (f"{MASTER}/firmware_can_dispatch_routing.c",), ("tests", MASTER), std="c11", sanitize=False),
    Suite("thermal_guard", "tests/test_thermal_guard.c", (f"{MASTER}/thermal_guard.c",), ("tests", MASTER), std="c11", sanitize=False),
    Suite("version_wire", "tests/test_version_wire.c", (), ("tests", "src/common"), std="c11"),
    Suite("thermal_stats", "tests/test_thermal_stats.c", (f"{MASTER}/thermal_stats.c",), ("tests", MASTER), std="c11", sanitize=False),
    Suite("bootloader_crypto", "tests/test_bootloader_crypto.c", (f"{FAKE_HAL}/fake_hal.c", f"{BOOT}/bootloader_crypto.c"), ("tests", FAKE_HAL, BOOT), extra=tuple(FAKE_HAL_WARN)),
    Suite("bootloader_update", "tests/test_bootloader_update.c",
          (f"{FAKE_HAL}/fake_hal.c", f"{BOOT}/bootloader_protocol.c", f"{BOOT}/bootloader_flash.c", f"{BOOT}/bootloader_crypto.c"),
          ("tests", FAKE_HAL, BOOT, "src/common"), extra=tuple(FAKE_HAL_WARN)),
    Suite("can_global_pre", "tests/test_can_global_pre.c", (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_can_global_pre.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("telemetry_watchdog", "tests/test_telemetry_watchdog.c", (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_telemetry_watchdog.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("can_slavebridge", "tests/test_can_slavebridge.c", (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_can_slavebridge.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("can_dispatch", "tests/test_can_dispatch.c",
          (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_can_dispatch.c", f"{MASTER}/firmware_can_dispatch_routing.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("persistence", "tests/test_persistence.c",
          (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_persistence.c", f"{MASTER}/firmware_fram.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
)


def host_compiler() -> str | None:
    forced = os.environ.get("HOST_CC")
    if forced:
        return forced
    return shutil.which("cc") or shutil.which("gcc")


def command(suite: Suite, compiler: str, binary: Path) -> list[str]:
    cmd = [compiler, f"-std={suite.std}", *WARN, *suite.extra]
    if suite.sanitize:
        cmd += SANITIZE
    for include in suite.includes:
        cmd.append(f"-I{ROOT / include}")
    cmd += ["-o", str(binary), str(ROOT / suite.test), *[str(ROOT / source) for source in suite.sources], *suite.libs]
    return cmd


def run_suite(suite: Suite, compiler: str, build_dir: Path) -> bool:
    binary = build_dir / f"host_tests_{suite.name}"
    if subprocess.run(command(suite, compiler, binary), cwd=ROOT).returncode != 0:
        print(f"FAIL {suite.name}: did not compile")
        return False
    if subprocess.run([str(binary)], cwd=ROOT).returncode != 0:
        print(f"FAIL {suite.name}: a test failed")
        return False
    print(f"PASS {suite.name}" + (" (address + undefined sanitizers)" if suite.sanitize else ""))
    return True


def through_wsl(argv: list[str]) -> int:
    wsl = shutil.which("wsl")
    if wsl is None:
        print("HOST_TESTS=SKIPPED these suites need a 64-bit Linux host (install WSL, or run this tool on Linux)")
        return 2
    result = subprocess.run([wsl, "--cd", str(ROOT), "--", "python3", "tools/run_host_tests.py", *argv])
    return result.returncode


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", action="store_true", help="print the suites and exit")
    parser.add_argument("--only", action="append", help="run only this suite (repeatable)")
    parser.add_argument("--build-dir", default=None, help="folder for the test binaries (default: build/)")
    args = parser.parse_args(argv)
    if args.list:
        for suite in SUITES:
            print(f"{suite.name:22} {suite.test}  [{'sanitizers' if suite.sanitize else 'plain'}]")
        return 0
    if not sys.platform.startswith("linux"):
        return through_wsl(argv)
    compiler = host_compiler()
    if compiler is None:
        print("HOST_TESTS=SKIPPED no host C compiler (cc/gcc) found")
        return 2
    selected = [suite for suite in SUITES if not args.only or suite.name in args.only]
    if args.only and len(selected) != len(set(args.only)):
        print("HOST_TESTS=FAIL unknown suite name in --only")
        return 1
    build_dir = Path(args.build_dir) if args.build_dir else ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    failed = [suite.name for suite in selected if not run_suite(suite, compiler, build_dir)]
    if failed:
        print(f"HOST_TESTS=FAIL {', '.join(failed)}")
        return 1
    print(f"HOST_TESTS=PASS {len(selected)} suites")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
