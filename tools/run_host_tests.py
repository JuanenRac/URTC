#!/usr/bin/env python3
# =============================================================================
# URTC - tools/run_host_tests.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Compiles and runs every host logic suite of the firmware (tests/test_*.c) with the host C compiler.

One list of suites for every entry point: build_firmware.sh and build_firmware.bat both call this tool, so a build
on Windows runs the same suites as a build on Linux.

    python tools/run_host_tests.py                 all suites
    python tools/run_host_tests.py --list          the suites and what they compile
    python tools/run_host_tests.py --only NAME     one suite
    python tools/run_host_tests.py --build-dir DIR where the test binaries go (default: build/)
    python tools/run_host_tests.py --no-analyze    skip the static analysis that follows the suites
    python tools/run_host_tests.py --coverage-floor 80   also build the suites with gcov counters and fail when they execute less
                                                   than that percentage of the lines of the firmware sources they compile

After the suites pass, every firmware source they compile (not the tests and not the fake HAL) is compiled once more
with gcc's -fanalyzer (null dereference, use after free, double free, leaks, out-of-bounds writes and the like). Any
warning fails the run; it is skipped, with a note, when the host compiler is not gcc 10 or newer.

The suites that use the fake HAL map a fake flash in the low 2 GiB and run under the address and undefined-behaviour
sanitizers, which needs a 64-bit Linux host: on Windows this tool runs itself inside WSL when it is installed, and
otherwise says that the suites were skipped (exit status 2), never that they passed.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MASTER = "src/F303-master"
BOOT = f"{MASTER}/boot"
MEL = f"{MASTER}/melexis_mlx90640"
SLAVEBOOT = "src/F303-slave/boot"
FAKE_HAL = "tests/host_hal"
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
WARN = ["-Wall", "-Wextra", "-Wno-unused-parameter"]
FAKE_HAL_WARN = ["-Wno-cpp", "-Wno-int-to-pointer-cast"]
FAKE_STATE = f"{FAKE_HAL}/fake_state.c"  # the shared state firmware_common.h declares, defined once for the suites that link real handlers
TOOL_HANDLERS = tuple(f"{MASTER}/firmware_can_{name}.c" for name in (
    "soldering", "hotair", "laser", "drill", "uvcuring", "electromagnet", "aoi", "printer3d", "pastejetting", "crimping", "weldpulse", "flyingprobe", "motion"))


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
    Suite("slaveboot_crypto", "tests/test_slaveboot_crypto.c", (f"{FAKE_HAL}/fake_hal.c", f"{SLAVEBOOT}/slaveboot_crypto.c"), ("tests", FAKE_HAL, SLAVEBOOT), extra=tuple(FAKE_HAL_WARN)),
    Suite("slaveboot_update", "tests/test_slaveboot_update.c",
          (f"{FAKE_HAL}/fake_hal.c", f"{SLAVEBOOT}/slaveboot_protocol.c", f"{SLAVEBOOT}/slaveboot_flash.c", f"{SLAVEBOOT}/slaveboot_crypto.c"),
          ("tests", FAKE_HAL, SLAVEBOOT, "src/common"), extra=tuple(FAKE_HAL_WARN)),
    Suite("can_global_pre", "tests/test_can_global_pre.c", (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_can_global_pre.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("telemetry_watchdog", "tests/test_telemetry_watchdog.c", (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_telemetry_watchdog.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("can_slavebridge", "tests/test_can_slavebridge.c", (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_can_slavebridge.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("can_dispatch", "tests/test_can_dispatch.c",
          (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_can_dispatch.c", f"{MASTER}/firmware_can_dispatch_routing.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("multinode", "tests/test_multinode.c",
          (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/multinode.c", f"{MASTER}/firmware_multinode.c", f"{MASTER}/firmware_fram.c",
           f"{MASTER}/firmware_can_dispatch.c", f"{MASTER}/firmware_can_dispatch_routing.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("motion_profile", "tests/test_motion_profile.c", (f"{MASTER}/motion_profile.c",), ("tests", MASTER), std="c11"),
    Suite("pid_control", "tests/test_pid_control.c", (f"{MASTER}/pid_control.c",), ("tests", MASTER), std="c11", libs=("-lm",)),
    Suite("health_hsi", "tests/test_health_hsi.c", (f"{MASTER}/health_profile.c", f"{MASTER}/hsi_trim.c"), ("tests", MASTER), std="c11"),
    Suite("features", "tests/test_features.c",
          (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/fw_features.c", f"{MASTER}/firmware_features.c", f"{MASTER}/firmware_fram.c", f"{MASTER}/pid_control.c",
           f"{MASTER}/health_profile.c", f"{MASTER}/hsi_trim.c", f"{MASTER}/motion_profile.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
    Suite("can_tools", "tests/test_can_tools.c", (f"{FAKE_HAL}/fake_hal.c", FAKE_STATE, *TOOL_HANDLERS), ("tests", FAKE_HAL, MASTER, "src/common"), extra=tuple(FAKE_HAL_WARN)),
    Suite("blackbox", "tests/test_blackbox.c", (f"{FAKE_HAL}/fake_hal.c", FAKE_STATE, f"{MASTER}/firmware_blackbox.c", f"{MASTER}/firmware_fram.c"), ("tests", FAKE_HAL, MASTER, "src/common"), extra=tuple(FAKE_HAL_WARN)),
    Suite("control_thermal", "tests/test_control_thermal.c",
          (f"{FAKE_HAL}/fake_hal.c", FAKE_STATE, f"{MASTER}/firmware_control_thermal.c", f"{MASTER}/thermal_guard.c", f"{MASTER}/firmware_thermal_stats.c", f"{MASTER}/thermal_stats.c",
           f"{MASTER}/firmware_features.c", f"{MASTER}/fw_features.c", f"{MASTER}/firmware_fram.c", f"{MASTER}/pid_control.c", f"{MASTER}/health_profile.c",
           f"{MASTER}/hsi_trim.c", f"{MASTER}/motion_profile.c"), ("tests", FAKE_HAL, MASTER, "src/common"), extra=tuple(FAKE_HAL_WARN), libs=("-lm",)),
    Suite("identify", "tests/test_identify.c",
          (f"{FAKE_HAL}/fake_hal.c", FAKE_STATE, f"{MASTER}/firmware_identify.c", f"{MASTER}/firmware_fram.c", f"{MASTER}/firmware_persistence.c"),
          ("tests", FAKE_HAL, MASTER, "src/common"), extra=tuple(FAKE_HAL_WARN)),
    Suite("can_global_post", "tests/test_can_global_post.c",
          (f"{FAKE_HAL}/fake_hal.c", FAKE_STATE, f"{MASTER}/firmware_can_global_post.c", f"{MASTER}/firmware_fram.c"),
          ("tests", FAKE_HAL, MASTER, "src/common"), extra=tuple(FAKE_HAL_WARN)),
    Suite("sensors_inspection", "tests/test_sensors_inspection.c",
          (f"{FAKE_HAL}/fake_hal.c", FAKE_STATE, f"{MASTER}/firmware_control_sensors.c", f"{MASTER}/firmware_can_thermalinspection.c",
           f"{MASTER}/firmware_thermal_stats.c", f"{MASTER}/thermal_stats.c", f"{MASTER}/firmware_fram.c"),
          ("tests", FAKE_HAL, MASTER, "src/common"), extra=tuple(FAKE_HAL_WARN)),
    Suite("persistence", "tests/test_persistence.c",
          (f"{FAKE_HAL}/fake_hal.c", f"{MASTER}/firmware_persistence.c", f"{MASTER}/firmware_fram.c"), ("tests", FAKE_HAL, MASTER, "src/common")),
)


_GCOV_FILE = re.compile(r"^File '(.+)'$")
_GCOV_LINES = re.compile(r"^Lines executed:([0-9.]+)% of (\d+)$")


def parse_gcov(output: str) -> dict[str, tuple[int, int]]:
    """{source file: (executed lines, executable lines)} from the text of ``gcov -n``; only the ``.c`` files under ``src/``."""
    found: dict[str, tuple[int, int]] = {}
    current: str | None = None
    root = ROOT.as_posix()
    for line in output.splitlines():
        match = _GCOV_FILE.match(line.strip())
        if match:
            current = match.group(1).replace("\\", "/")
            continue
        match = _GCOV_LINES.match(line.strip())
        if match and current is not None:
            total = int(match.group(2))
            executed = round(float(match.group(1)) * total / 100)
            short = current[len(root) + 1:] if current.startswith(root + "/") else current
            if short.endswith(".c") and short.startswith("src/"):
                done, count = found.get(short, (0, 0))
                found[short] = (max(done, executed), max(count, total))
            current = None
    return found


def merge_coverage(parts: list[dict[str, tuple[int, int]]]) -> dict[str, tuple[int, int]]:
    """The best a file did in any suite (a line run by one suite counts; the counters of different suites are not added)."""
    merged: dict[str, tuple[int, int]] = {}
    for part in parts:
        for name, (done, count) in part.items():
            old = merged.get(name, (0, 0))
            merged[name] = (max(old[0], done), max(old[1], count))
    return merged


def coverage_percent(files: dict[str, tuple[int, int]]) -> float:
    total = sum(count for _, count in files.values())
    return 100.0 * sum(done for done, _ in files.values()) / total if total else 0.0


def measure_coverage(selected: list["Suite"], compiler: str, build_dir: Path) -> dict[str, tuple[int, int]]:
    """Builds every suite again with ``--coverage -O0`` (no sanitizers), runs it and reads the counters with gcov."""
    parts: list[dict[str, tuple[int, int]]] = []
    for suite in selected:
        work = build_dir / "coverage" / suite.name
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
        binary = work / "t"
        cmd = [c for c in command(suite, compiler, binary) if not c.startswith(("-fsanitize", "-fno-sanitize"))]
        cmd[1:1] = ["--coverage", "-O0"]
        if subprocess.run(cmd, cwd=work, capture_output=True).returncode != 0:
            print(f"COVERAGE=FAIL {suite.name}: did not compile with the counters")
            return {}
        subprocess.run([str(binary)], cwd=ROOT, capture_output=True)
        for gcda in work.rglob("*.gcda"):
            out = subprocess.run(["gcov", "-n", "-o", str(gcda.parent), str(gcda)], cwd=work, capture_output=True, text=True).stdout
            parts.append(parse_gcov(out))
    return merge_coverage(parts)


def all_firmware_sources() -> list[str]:
    return sorted(path.relative_to(ROOT).as_posix() for path in (ROOT / "src").rglob("*.c"))


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


def is_gcc_with_analyzer(compiler: str) -> bool:
    """True for a gcc that has -fanalyzer (10 or newer); clang and old gcc are skipped."""
    try:
        out = subprocess.run([compiler, "-dumpfullversion", "-dumpversion"], capture_output=True, text=True).stdout.strip()
        banner = subprocess.run([compiler, "--version"], capture_output=True, text=True).stdout
    except OSError:
        return False
    if "clang" in banner.lower() or "gcc" not in banner.lower() and "free software foundation" not in banner.lower():
        return False
    try:
        return int(out.split(".")[0]) >= 10
    except ValueError:
        return False


def analysis_sources(selected: list[Suite]) -> list[tuple[Suite, str]]:
    """Each firmware source once, with the suite whose flags compile it; the tests and the fake HAL are not analysed."""
    seen: dict[str, Suite] = {}
    for suite in selected:
        for source in suite.sources:
            if not source.startswith(("tests/", FAKE_HAL)) and source not in seen:
                seen[source] = suite
    return [(suite, source) for source, suite in seen.items()]


def analyze_command(suite: Suite, compiler: str, source: str) -> list[str]:
    cmd = [compiler, f"-std={suite.std}", *WARN, *suite.extra, "-fanalyzer"]
    cmd += [f"-I{ROOT / include}" for include in suite.includes]
    return cmd + ["-c", "-o", os.devnull, str(ROOT / source)]


def analyze(selected: list[Suite], compiler: str) -> bool:
    pairs = analysis_sources(selected)
    failed = []
    for suite, source in pairs:
        result = subprocess.run(analyze_command(suite, compiler, source), cwd=ROOT, capture_output=True, text=True)
        if result.returncode != 0 or "warning:" in result.stderr:
            failed.append(source)
            print(f"ANALYZER {source}:\n{result.stderr}")
    if failed:
        print(f"FAIL static analysis: {len(failed)} of {len(pairs)} file(s) have warnings: {', '.join(failed)}")
        return False
    print(f"PASS static analysis (gcc -fanalyzer) {len(pairs)} source files, no warning")
    return True


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


_WINDOWS_PATH = re.compile(r"^([A-Za-z]):[\\/](.*)$")


def wsl_path(value: str) -> str:
    """A Windows path (C:\\x\\y) as WSL sees it (/mnt/c/x/y). Passed through unchanged, WSL would take "C:" for a folder name and put the binaries inside the repository."""
    match = _WINDOWS_PATH.match(value)
    if match is None:
        return value
    return "/mnt/" + match.group(1).lower() + "/" + match.group(2).replace("\\", "/")


def translate_arguments(argv: list[str]) -> list[str]:
    """The arguments for the copy of this tool that runs inside WSL: the folder given with --build-dir is translated."""
    result = []
    previous = ""
    for item in argv:
        if previous == "--build-dir":
            item = wsl_path(item)
        elif item.startswith("--build-dir="):
            item = "--build-dir=" + wsl_path(item.split("=", 1)[1])
        result.append(item)
        previous = item
    return result


def through_wsl(argv: list[str]) -> int:
    wsl = shutil.which("wsl")
    if wsl is None:
        print("HOST_TESTS=SKIPPED these suites need a 64-bit Linux host (install WSL, or run this tool on Linux)")
        return 2
    result = subprocess.run([wsl, "--cd", str(ROOT), "--", "python3", "tools/run_host_tests.py", *translate_arguments(argv)])
    return result.returncode


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", action="store_true", help="print the suites and exit")
    parser.add_argument("--only", action="append", help="run only this suite (repeatable)")
    parser.add_argument("--no-analyze", action="store_true", help="do not run the static analysis after the suites")
    parser.add_argument("--build-dir", default=None, help="folder for the test binaries (default: build/)")
    parser.add_argument("--coverage-floor", type=float, default=None, help="build the suites with gcov counters and fail under this percentage of executed lines")
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
    if args.build_dir and _WINDOWS_PATH.match(args.build_dir):
        print(f"HOST_TESTS=FAIL --build-dir {args.build_dir} is a Windows path on a Linux host; it would create a folder named like a drive")
        return 1
    build_dir = Path(args.build_dir) if args.build_dir else ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    failed = [suite.name for suite in selected if not run_suite(suite, compiler, build_dir)]
    if failed:
        print(f"HOST_TESTS=FAIL {', '.join(failed)}")
        return 1
    if not args.no_analyze:
        if is_gcc_with_analyzer(compiler):
            if not analyze(selected, compiler):
                print("HOST_TESTS=FAIL static analysis")
                return 1
        else:
            print(f"NOTE static analysis skipped: {compiler} is not gcc 10 or newer")
    if args.coverage_floor is not None:
        if shutil.which("gcov") is None:
            print("COVERAGE=FAIL gcov is not installed")
            return 1
        covered = measure_coverage(selected, compiler, build_dir)
        if not covered:
            return 1
        percent = coverage_percent(covered)
        listed = all_firmware_sources()
        never = [name for name in listed if name not in covered]
        for name, (done, count) in sorted(covered.items()):
            print(f"  {100.0 * done / count if count else 0.0:6.1f}%  {done:5}/{count:<5} {name}")
        print(f"COVERAGE {percent:.1f}% of the lines of {len(covered)} firmware sources the suites compile ({len(never)} of {len(listed)} sources are compiled by no suite)")
        if percent < args.coverage_floor:
            print(f"COVERAGE=FAIL below the floor of {args.coverage_floor:g}%")
            return 1
        print(f"COVERAGE=PASS floor {args.coverage_floor:g}%")
    print(f"HOST_TESTS=PASS {len(selected)} suites")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
