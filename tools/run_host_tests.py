#!/usr/bin/env python3
# =============================================================================
# URTC - tools/run_host_tests.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Builds and runs the host logic suites of a firmware (C programs that test the logic with the host C compiler) and measures them.

One executor for every firmware repository: what changes from one to the next is the data file ``tools/host_tests.json`` (the suites, their sources, their
compiler flags and whether they need a Linux host), never this code. The two build scripts of a repository (build_firmware.sh and build_firmware.bat) call this
tool, so a build on Windows runs the same suites as a build on Linux.

    python tools/run_host_tests.py                 all suites
    python tools/run_host_tests.py --list          the suites and what they compile
    python tools/run_host_tests.py --only NAME     one suite (repeatable)
    python tools/run_host_tests.py --build-dir DIR where the test binaries go (default: build/)
    python tools/run_host_tests.py --runner R      ctest (CMake builds every suite and ctest runs them), direct (one compiler call and one run per suite) or auto
    python tools/run_host_tests.py --jobs N        compilations at the same time under ctest (default 2, or the variable URTC_HOST_JOBS)
    python tools/run_host_tests.py --no-analyze    skip the static analysis that follows the suites
    python tools/run_host_tests.py --coverage-floor 80   also build the suites with gcov counters and fail when they execute less
                                                   than that percentage of the lines of the firmware sources they compile

The runner is ``auto`` unless ``--runner`` or the variable URTC_HOST_RUNNER says otherwise: ctest when cmake, make and ctest are found on a Linux host, direct
everywhere else. Both run the same compiler commands and the same binaries; the result is the same.

The data file is an object with ``suites`` (each: ``name``, ``test``, ``sources``, ``includes``; optional ``std``, ``sanitize``, ``extra``, ``libs``,
``unsanitize``, ``defines``, ``warn``) and these optional keys: ``needs_linux_host`` (true when the suites map memory a 64-bit Linux host gives them: on Windows the
tool then runs itself inside WSL), ``analyze`` (run gcc's -fanalyzer on every firmware source afterwards), ``coverage_prefix`` (the folder of the firmware sources the
coverage counts, ``src/``) with ``coverage_exclude`` (folders under it that are not firmware: a companion program, a virtual environment), ``warn`` and ``sanitize_flags`` (the flags every suite gets unless it says its own). A source written ``glob:PATTERN`` is every file the
pattern matches under the repository. ``sanitize`` is true, false or ``env:NAME`` (on when that variable is 1: how CI turns the sanitizers on for a repository whose
developers build without them).

With the sanitizers the suites run under the address and undefined-behaviour checks. When the host cannot run the suites the tool says that they were skipped
(exit status 2), never that they passed.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from local_build import local_build_refusal  # noqa: E402  (the lock on builds that compile on this PC: copied by vendor_copy.py)
except ImportError:   # a repository that has not taken the helper yet keeps the same rule
    from typing import Mapping

    def local_build_refusal(env: Mapping[str, str] | None = None) -> str | None:
        env = os.environ if env is None else env
        if env.get("ALLOW_LOCAL_BUILD", "").strip() == "1" or any(env.get(n, "").strip().lower() not in ("", "0", "false", "no", "off") for n in ("CI", "GITHUB_ACTIONS")):
            return None
        return "local C builds are off; GitHub Actions builds it. Set ALLOW_LOCAL_BUILD=1 to allow it on this PC"
CONFIG = ROOT / "tools" / "host_tests.json"
DEFAULT_SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
DEFAULT_WARN = ["-Wall", "-Wextra", "-Wno-unused-parameter"]
FAKE_HAL = "tests/host_hal"
DEFAULT_JOBS = 2


@dataclass(frozen=True)
class Suite:
    name: str
    test: str
    sources: tuple[str, ...]
    includes: tuple[str, ...]
    std: str = "gnu11"
    sanitize: bool | str = True
    extra: tuple[str, ...] = ()
    libs: tuple[str, ...] = ()
    unsanitize: tuple[str, ...] = ()   # checks of the sanitizers that stay off for this suite (given after them, so they win)
    defines: tuple[str, ...] = ()      # -D options (a source whose main() the suite calls under another name)
    warn: tuple[str, ...] | None = None  # the warning flags of this suite when they are not the ones of the file
    sanitize_flags: tuple[str, ...] = tuple(DEFAULT_SANITIZE)

    @property
    def sanitized(self) -> bool:
        """Whether the sanitizers are on for this suite: true, false, or the value of the variable the file names."""
        if isinstance(self.sanitize, str):
            return os.environ.get(self.sanitize.removeprefix("env:"), "0") == "1"
        return bool(self.sanitize)


def expand(root: Path, entry: str) -> list[str]:
    """One source of the file: itself, or the files a ``glob:`` pattern matches (sorted, relative to the repository)."""
    if not entry.startswith("glob:"):
        return [entry]
    return sorted(p.relative_to(root).as_posix() for p in root.glob(entry[len("glob:"):]) if p.is_file())


def load_config(root: Path = ROOT) -> dict:
    data = json.loads((root / "tools" / "host_tests.json").read_text(encoding="utf-8"))
    if not isinstance(data, dict) or not isinstance(data.get("suites"), list) or not data["suites"]:
        raise ValueError("tools/host_tests.json must be an object with a non-empty list under 'suites'")
    return data


def build_suites(data: dict, root: Path = ROOT) -> tuple[Suite, ...]:
    warn_default = tuple(data.get("warn", DEFAULT_WARN))
    sanitize_default = tuple(data.get("sanitize_flags", DEFAULT_SANITIZE))
    suites = []
    for raw in data["suites"]:
        sources: list[str] = []
        for entry in raw.get("sources", []):
            for item in expand(root, entry):
                if item not in sources and item != raw["test"]:
                    sources.append(item)
        suites.append(Suite(
            name=raw["name"], test=raw["test"], sources=tuple(sources), includes=tuple(raw.get("includes", ())),
            std=raw.get("std", "gnu11"), sanitize=raw.get("sanitize", True), extra=tuple(raw.get("extra", ())),
            libs=tuple(raw.get("libs", ())), unsanitize=tuple(raw.get("unsanitize", ())), defines=tuple(raw.get("defines", ())),
            warn=tuple(raw["warn"]) if "warn" in raw else warn_default, sanitize_flags=sanitize_default))
    return tuple(suites)


SETTINGS = load_config() if CONFIG.is_file() else {"suites": []}
NEEDS_LINUX = bool(SETTINGS.get("needs_linux_host", False))
ANALYZE = bool(SETTINGS.get("analyze", False))
COVERAGE_PREFIX = str(SETTINGS.get("coverage_prefix", "src/"))
COVERAGE_EXCLUDE = tuple(SETTINGS.get("coverage_exclude", ()))
SUITES = build_suites(SETTINGS) if SETTINGS["suites"] else ()

_GCOV_FILE = re.compile(r"^File '(.+)'$")
_GCOV_LINES = re.compile(r"^Lines executed:([0-9.]+)% of (\d+)$")


def parse_gcov(output: str) -> dict[str, tuple[int, int]]:
    """{source file: (executed lines, executable lines)} from the text of ``gcov -n``; only the ``.c`` files under the coverage prefix."""
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
            if short.endswith(".c") and short.startswith(COVERAGE_PREFIX) and not short.startswith(COVERAGE_EXCLUDE):
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


def binary_name(stem: str) -> str:
    return stem + (".exe" if os.name == "nt" else "")


def measure_coverage(selected: list["Suite"], compiler: str, build_dir: Path) -> dict[str, tuple[int, int]]:
    """Builds every suite again with ``--coverage -O0`` (no sanitizers), runs it and reads the counters with gcov."""
    parts: list[dict[str, tuple[int, int]]] = []
    for suite in selected:
        work = build_dir / "coverage" / suite.name
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
        binary = work / binary_name("t")
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
    folder = ROOT / COVERAGE_PREFIX.rstrip("/")
    found = (path.relative_to(ROOT).as_posix() for path in folder.rglob("*.c"))
    return sorted(name for name in found if not name.startswith(COVERAGE_EXCLUDE))


def host_compiler() -> str | None:
    forced = os.environ.get("HOST_CC")
    if forced:
        return forced
    return shutil.which("cc") or shutil.which("gcc")


def flags(suite: Suite) -> list[str]:
    """The options that are not paths: the standard, the warnings, the definitions and, where the suite asks, the sanitizers."""
    cmd = [f"-std={suite.std}", *(suite.warn or ()), *suite.extra, *suite.defines]
    if suite.sanitized:
        cmd += suite.sanitize_flags
        cmd += suite.unsanitize
    return cmd


def command(suite: Suite, compiler: str, binary: Path) -> list[str]:
    cmd = [compiler, *flags(suite)]
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
    cmd = [compiler, f"-std={suite.std}", *(suite.warn or ()), *suite.extra, *suite.defines, "-fanalyzer"]
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
    binary = build_dir / binary_name(f"host_tests_{suite.name}")
    if subprocess.run(command(suite, compiler, binary), cwd=ROOT).returncode != 0:
        print(f"FAIL {suite.name}: did not compile")
        return False
    if subprocess.run([str(binary)], cwd=ROOT).returncode != 0:
        print(f"FAIL {suite.name}: a test failed")
        return False
    print(f"PASS {suite.name}" + (" (address + undefined sanitizers)" if suite.sanitized else ""))
    return True


def cmake_quote(value: str) -> str:
    return '"' + value.replace("\\", "/").replace('"', '\\"') + '"'


def cmake_lists(selected: list[Suite], compiler: str) -> str:
    """The CMakeLists.txt that makes ctest the executor: one program and one test per suite, with the flags the direct runner would give the compiler."""
    lines = ["# Written by tools/run_host_tests.py; never edited by hand.", "cmake_minimum_required(VERSION 3.16)",
             f"set(CMAKE_C_COMPILER {cmake_quote(compiler)})", "project(host_tests C)", "set(CMAKE_C_FLAGS \"\")", "enable_testing()"]
    for suite in selected:
        files = " ".join(cmake_quote(str(ROOT / item)) for item in (suite.test, *suite.sources))
        lines.append(f"add_executable({suite.name} {files})")
        options = " ".join(cmake_quote(item) for item in flags(suite))
        lines.append(f"target_compile_options({suite.name} PRIVATE {options})")
        if suite.sanitized:
            lines.append(f"target_link_options({suite.name} PRIVATE {' '.join(cmake_quote(item) for item in (*suite.sanitize_flags, *suite.unsanitize))})")
        if suite.includes:
            lines.append(f"target_include_directories({suite.name} PRIVATE {' '.join(cmake_quote(str(ROOT / item)) for item in suite.includes)})")
        if suite.libs:
            lines.append(f"target_link_libraries({suite.name} PRIVATE {' '.join(cmake_quote(item) for item in suite.libs)})")
        lines.append(f"add_test(NAME {suite.name} COMMAND {suite.name} WORKING_DIRECTORY {cmake_quote(str(ROOT))})")
    return "\n".join(lines) + "\n"


_CTEST_FAILED = re.compile(r"^\s*\d+ - (\S+)")


def failed_tests(output: str) -> list[str]:
    """The names ctest lists under 'The following tests FAILED'."""
    names: list[str] = []
    inside = False
    for line in output.splitlines():
        if line.startswith("The following tests FAILED"):
            inside = True
        elif inside:
            match = _CTEST_FAILED.match(line)
            if match:
                names.append(match.group(1))
    return names


def run_with_ctest(selected: list[Suite], compiler: str, build_dir: Path, jobs: int) -> list[str]:
    """Builds every suite with CMake and runs them with ctest. Returns the names of the suites that did not pass (empty: all passed)."""
    work = build_dir / "ctest"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    (work / "CMakeLists.txt").write_text(cmake_lists(selected, compiler), encoding="utf-8", newline="\n")
    configure = subprocess.run(["cmake", "-S", str(work), "-B", str(work / "out"), "-G", "Unix Makefiles"], capture_output=True, text=True)
    if configure.returncode != 0:
        print(configure.stdout + configure.stderr)
        print("FAIL cmake could not configure the suites")
        return [suite.name for suite in selected]
    built = subprocess.run(["cmake", "--build", str(work / "out"), "--parallel", str(jobs), "--", "--keep-going"], capture_output=True, text=True)
    if built.returncode != 0:
        print(built.stdout[-6000:] + built.stderr[-6000:])
        print("FAIL a suite did not compile")
    ran = subprocess.run(["ctest", "--output-on-failure"], cwd=work / "out", capture_output=True, text=True)
    print(ran.stdout, end="")
    if ran.stderr:
        print(ran.stderr, end="")
    failed = failed_tests(ran.stdout)
    if built.returncode != 0 or ran.returncode != 0:
        return failed or [suite.name for suite in selected]
    return []


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
        print("HOST_TESTS=SKIPPED these suites need a 64-bit Linux host or a host C compiler (install WSL, or run this tool on Linux)")
        return 2
    result = subprocess.run([wsl, "--cd", str(ROOT), "--", "python3", "tools/run_host_tests.py", *translate_arguments(argv)])
    return result.returncode


def choose_runner(requested: str) -> str:
    """ctest where cmake, make and ctest exist on a Linux host (the case in CI and in WSL), direct otherwise; an explicit request is honoured."""
    if requested != "auto":
        return requested
    if sys.platform.startswith("linux") and shutil.which("cmake") and shutil.which("ctest") and shutil.which("make"):
        return "ctest"
    return "direct"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", action="store_true", help="print the suites and exit")
    parser.add_argument("--only", action="append", help="run only this suite (repeatable)")
    parser.add_argument("--no-analyze", action="store_true", help="do not run the static analysis after the suites")
    parser.add_argument("--build-dir", default=None, help="folder for the test binaries (default: build/)")
    parser.add_argument("--runner", choices=("auto", "ctest", "direct"), default=os.environ.get("URTC_HOST_RUNNER", "auto"), help="who builds and runs the suites (default: auto)")
    parser.add_argument("--jobs", type=int, default=int(os.environ.get("URTC_HOST_JOBS", DEFAULT_JOBS)), help="compilations at the same time under ctest")
    parser.add_argument("--coverage-floor", type=float, default=None, help="build the suites with gcov counters and fail under this percentage of executed lines")
    args = parser.parse_args(argv)
    if args.list:
        for suite in SUITES:
            print(f"{suite.name:22} {suite.test}  [{'sanitizers' if suite.sanitized else 'plain'}]")
        return 0
    if not SUITES:
        print("HOST_TESTS=FAIL tools/host_tests.json is missing or lists no suite")
        return 1
    refusal = local_build_refusal()
    if refusal is not None:
        print(f"HOST_TESTS=SKIPPED {refusal}")   # compiling on this PC is locked: skipped, never passed
        return 2
    compiler = host_compiler()
    if not sys.platform.startswith("linux") and (NEEDS_LINUX or compiler is None):
        return through_wsl(argv)
    if compiler is None:
        print("HOST_TESTS=SKIPPED no host C compiler (cc/gcc) found")
        return 2
    selected = [suite for suite in SUITES if not args.only or suite.name in args.only]
    if args.only and len(selected) != len(set(args.only)):
        print("HOST_TESTS=FAIL unknown suite name in --only")
        return 1
    if args.build_dir and _WINDOWS_PATH.match(args.build_dir) and sys.platform.startswith("linux"):
        print(f"HOST_TESTS=FAIL --build-dir {args.build_dir} is a Windows path on a Linux host; it would create a folder named like a drive")
        return 1
    runner = choose_runner(args.runner)
    if runner == "ctest" and not (shutil.which("cmake") and shutil.which("ctest")):
        print("HOST_TESTS=FAIL --runner ctest needs cmake and ctest")
        return 1
    build_dir = Path(args.build_dir) if args.build_dir else ROOT / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    if runner == "ctest":
        failed = run_with_ctest(selected, compiler, build_dir, max(1, args.jobs))
    else:
        failed = [suite.name for suite in selected if not run_suite(suite, compiler, build_dir)]
    if failed:
        print(f"HOST_TESTS=FAIL {', '.join(failed)}")
        return 1
    if ANALYZE and not args.no_analyze:
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
    print(f"HOST_TESTS=PASS {len(selected)} suites ({runner})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
