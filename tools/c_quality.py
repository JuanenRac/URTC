#!/usr/bin/env python3
# =============================================================================
# URTC - tools/c_quality.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Quality checks of the firmware C sources that the compiler of the target does not make: implicit conversions, cppcheck and the length of the comments.

    python tools/c_quality.py                 all three checks
    python tools/c_quality.py --conversion    every firmware source the host suites compile, with gcc -Wconversion -fsyntax-only (no object is written)
    python tools/c_quality.py --cppcheck      cppcheck over the firmware folders (warnings, performance and portability findings)
    python tools/c_quality.py --comments      comment blocks longer than the limit (history belongs in the changelog and the documentation, not in the code)

Everything that can be tuned lives in tools/c_quality.json: the flags, the budget of warnings (0), the folders that are not ours (the vendored sensor drivers, kept
byte for byte), the findings of cppcheck that were judged and why (each with its reason) and the limits of the comment check. A source that no host suite compiles
cannot be checked for conversions here, because it needs the vendor HAL: cppcheck still reads it.

The host compiler is the one of the machine (gcc); on Windows without one, the tool runs itself inside WSL. cppcheck is optional on a developer machine (the check
is skipped with a note and exit status 2) and required when URTC_CI=1. Exit status: 0 passed, 1 a check failed, 2 skipped because a tool is missing.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

CONFIG = ROOT / "tools" / "c_quality.json"
SOURCE_SUFFIXES = (".c", ".h")


def load_config(path: Path = CONFIG) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def excluded(name: str, prefixes: list[str]) -> bool:
    return any(name == prefix.rstrip("/") or name.startswith(prefix.rstrip("/") + "/") for prefix in prefixes)


# ---------------------------------------------------------------------------------------------------------------------
# implicit conversions
# ---------------------------------------------------------------------------------------------------------------------
def conversion_command(compiler: str, suite, source: str, flags: list[str]) -> list[str]:
    cmd = [compiler, f"-std={suite.std}", *(suite.warn or ()), *suite.extra, *suite.defines, *flags, "-fsyntax-only"]
    cmd += [f"-I{ROOT / include}" for include in suite.includes]
    return cmd + [str(ROOT / source)]


def check_conversion(config: dict, compiler: str, pairs: list[tuple]) -> tuple[bool, int, int]:
    """(passed, sources checked, warnings) over the (suite, source) pairs, minus the folders that are not ours."""
    section = config["conversion"]
    skip = [entry["path"] for entry in section.get("exclude", [])]
    total = 0
    checked = 0
    for suite, source in pairs:
        if excluded(source, skip):
            continue
        checked += 1
        result = subprocess.run(conversion_command(compiler, suite, source, section["flags"]), cwd=ROOT, capture_output=True, text=True)
        found = [line for line in result.stderr.splitlines() if "warning:" in line or "error:" in line]
        total += len(found)
        for line in found:
            print("CONVERSION " + line.replace(str(ROOT) + os.sep, "").replace(str(ROOT) + "/", ""))
        if result.returncode != 0 and not found:
            total += 1
            print(f"CONVERSION {source}: the compiler failed:\n{result.stderr.strip()}")
    ok = total <= int(section.get("max_warnings", 0))
    print(f"CONVERSION={'PASS' if ok else 'FAIL'} {checked} sources, {total} warning(s), budget {section.get('max_warnings', 0)}")
    return ok, checked, total


# ---------------------------------------------------------------------------------------------------------------------
# cppcheck
# ---------------------------------------------------------------------------------------------------------------------
def cppcheck_command(config: dict, executable: str = "cppcheck") -> list[str]:
    section = config["cppcheck"]
    cmd = [executable, f"--enable={section['enable']}", f"--std={section['std']}", "--quiet", "--error-exitcode=1", "--inline-suppr",
           "--template={file}:{line}:{id}:{message}"]
    for entry in section.get("suppress", []):
        cmd.append(f"--suppress={entry['id']}:{entry['file']}")
    cmd += [f"-I{include}" for include in section.get("includes", [])]
    cmd += [f"-i{path}" for path in section.get("exclude", [])]
    return cmd + list(section["sources"])


def check_suppressions_have_reasons(config: dict) -> list[str]:
    return [f"{entry.get('id', '?')}:{entry.get('file', '?')}" for entry in config["cppcheck"].get("suppress", []) if not str(entry.get("reason", "")).strip()]


def check_cppcheck(config: dict, required: bool) -> tuple[bool, str]:
    """(passed, status): status is PASS, FAIL or SKIPPED (cppcheck missing and not required)."""
    missing = check_suppressions_have_reasons(config)
    if missing:
        print("CPPCHECK=FAIL a suppression has no reason: " + ", ".join(missing))
        return False, "FAIL"
    executable = shutil.which("cppcheck")
    if executable is None:
        if required:
            print("CPPCHECK=FAIL cppcheck is not installed (it is required when URTC_CI=1)")
            return False, "FAIL"
        print("CPPCHECK=SKIPPED cppcheck is not installed (Debian/Ubuntu: sudo apt install cppcheck)")
        return True, "SKIPPED"
    result = subprocess.run(cppcheck_command(config, executable), cwd=ROOT, capture_output=True, text=True)
    findings = [line for line in (result.stdout + result.stderr).splitlines() if line.strip()]
    for line in findings:
        print("CPPCHECK " + line)
    ok = result.returncode == 0 and not findings
    print(f"CPPCHECK={'PASS' if ok else 'FAIL'} {len(findings)} finding(s)")
    return ok, "PASS" if ok else "FAIL"


# ---------------------------------------------------------------------------------------------------------------------
# comments
# ---------------------------------------------------------------------------------------------------------------------
def comment_blocks(text: str) -> list[tuple[int, int]]:
    """(first line, number of lines) of every run of comment-only lines: // lines, or the lines of a /* */ block."""
    blocks: list[tuple[int, int]] = []
    start = None
    in_block = False
    for number, raw in enumerate(text.splitlines() + [""], start=1):
        line = raw.strip()
        comment_only = in_block or line.startswith("//") or line.startswith("/*") or (line.startswith("*") and start is not None)
        if line.startswith("/*") and "*/" not in line:
            in_block = True
        elif in_block and "*/" in line:
            in_block = False
        if comment_only:
            if start is None:
                start = number
        elif start is not None:
            blocks.append((start, number - start))
            start = None
    return blocks


def long_comment_blocks(config: dict) -> list[tuple[str, int, int]]:
    section = config["comments"]
    limit = int(section["max_block_lines"])
    header_lines = int(section.get("header_lines", 3))
    skip = section.get("exclude", [])
    found: list[tuple[str, int, int]] = []
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES:
            continue
        name = path.relative_to(ROOT).as_posix()
        if excluded(name, skip):
            continue
        for first, count in comment_blocks(path.read_text(encoding="utf-8", errors="replace")):
            if count > limit and first > header_lines:   # the description at the top of a file is documentation, not narrative
                found.append((name, first, count))
    return sorted(found, key=lambda item: -item[2])


def check_comments(config: dict) -> tuple[bool, int]:
    section = config["comments"]
    found = long_comment_blocks(config)
    allowed = int(section["max_long_blocks"])
    for name, first, count in found[:10]:
        print(f"COMMENTS {name}:{first} {count} lines of comment in one block")
    ok = len(found) <= allowed
    print(f"COMMENTS={'PASS' if ok else 'FAIL'} {len(found)} block(s) over {section['max_block_lines']} lines, budget {allowed}")
    return ok, len(found)


# ---------------------------------------------------------------------------------------------------------------------
def through_wsl(argv: list[str]) -> int:
    wsl = shutil.which("wsl")
    if wsl is None:
        print("C_QUALITY=SKIPPED no host C compiler and no WSL")
        return 2
    return subprocess.run([wsl, "--cd", str(ROOT), "--", "python3", "tools/c_quality.py", *argv]).returncode


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--conversion", action="store_true")
    parser.add_argument("--cppcheck", action="store_true")
    parser.add_argument("--comments", action="store_true")
    parser.add_argument("--require-cppcheck", action="store_true", help="fail when cppcheck is not installed (set by URTC_CI=1)")
    args = parser.parse_args(argv)
    everything = not (args.conversion or args.cppcheck or args.comments)
    config = load_config()
    import run_host_tests as host

    from local_build import local_build_refusal

    wants_compiler = everything or args.conversion
    locked = local_build_refusal() if wants_compiler else None   # compiling on this PC is locked: the conversion check is skipped, never passed
    if locked is not None:
        wants_compiler = False
    compiler = host.host_compiler() if wants_compiler else None
    if wants_compiler and not sys.platform.startswith("linux") and compiler is None:
        return through_wsl(argv)
    if wants_compiler and not sys.platform.startswith("linux") and shutil.which("cppcheck") is None and (everything or args.cppcheck):
        return through_wsl(argv)   # the compiler of the machine is there but the checks of the C sources are better run where both tools are
    required = args.require_cppcheck or os.environ.get("URTC_CI") == "1"
    failed: list[str] = []
    skipped = False
    if everything or args.conversion:
        if locked is not None:
            print(f"CONVERSION=SKIPPED {locked}")
            skipped = True
        elif compiler is None:
            print("CONVERSION=SKIPPED no host C compiler")
            skipped = True
        else:
            pairs = host.analysis_sources(list(host.SUITES))
            if not check_conversion(config, compiler, pairs)[0]:
                failed.append("conversion")
    if everything or args.cppcheck:
        ok, status = check_cppcheck(config, required)
        skipped = skipped or status == "SKIPPED"
        if not ok:
            failed.append("cppcheck")
    if everything or args.comments:
        if not check_comments(config)[0]:
            failed.append("comments")
    if failed:
        print(f"C_QUALITY=FAIL {', '.join(failed)}")
        return 1
    print("C_QUALITY=" + ("SKIPPED some checks needed a tool that is not installed" if skipped else "PASS"))
    return 2 if skipped else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
