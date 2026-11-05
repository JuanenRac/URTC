#!/usr/bin/env python3
# =============================================================================
# URTC - tools/build_test.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Run a non-versioning build check for this repository.

    python tools/build_test.py            the full check: build or compile what the stack builds, run the tests the repository lists
    python tools/build_test.py --check    the light check: the manifest, the stack and the files the full check needs, nothing is built or run

This command never invokes version bump scripts and never updates CHANGELOG.md.
It may create normal compiler outputs such as build/, target/ or APK artifacts.

One text for every repository of the family. What differs between repositories is data in an optional ``tools/build_test.json`` next to this file:

    {"pytest": {"dir": "tests", "pythonpath": ["src"], "install": false}}   run the pytest suite of the repository (skipped when pytest is not installed,
                                                                            unless "install" asks for it to be installed first)
    {"unittest": "tests"}                                                   run a unittest discovery
    {"python_extra": [{"cwd": "src/companion", "requires": ["numpy", "pytest"], "args": ["-m", "pytest", "-q", "tests"]}]}
                                                                            further Python commands, each needing the modules it names

The environment variables URTC_WSL_DISTRO and URTC_WSL_DISTRIBUTION pick the WSL distribution for a firmware build on Windows.

The full check of a stack that compiles (rust, go, android, flutter, firmware-c) is locked on a developer PC: it runs on a CI runner (CI or GITHUB_ACTIONS set) or with
ALLOW_LOCAL_BUILD=1, and anywhere else it prints BUILD_TEST=SKIPPED and exits 2, never a pass. The light check and the Python, Qt Quick and node stacks are not locked.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Mapping

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from local_build import local_build_refusal  # noqa: E402  (the lock on builds that compile on this PC: copied by vendor_copy.py)
except ImportError:   # a repository that has not taken the helper yet keeps the same rule
    def local_build_refusal(env: Mapping[str, str] | None = None) -> str | None:
        env = os.environ if env is None else env
        if env.get("ALLOW_LOCAL_BUILD", "").strip() == "1" or any(env.get(n, "").strip().lower() not in ("", "0", "false", "no", "off") for n in ("CI", "GITHUB_ACTIONS")):
            return None
        return "local C builds are off; GitHub Actions builds it. Set ALLOW_LOCAL_BUILD=1 to allow it on this PC"
EXCLUDED_PARTS = {".git", ".venv", "node_modules", "build", "dist", "target", "__pycache__"}
PYTHON_STACKS = {"python", "python-bare", "python-qtquick"}
COMPILED_STACKS = {"rust", "go", "android", "flutter", "firmware-c"}   # the full check of these compiles: locked on a developer PC
STACKS = PYTHON_STACKS | {"node", "rust", "go", "android", "flutter", "firmware-c"}
# what the full check of each stack needs, relative to the root (the light check looks for it)
STACK_MARKERS = {"node": "package.json", "rust": "Cargo.toml", "flutter": "pubspec.yaml", "firmware-c": "build_firmware.sh"}


def fail(message: str) -> None:
    print(f"BUILD_TEST=FAIL {message}", file=sys.stderr)
    raise SystemExit(1)


def run(*command: str, cwd: Path = ROOT, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command))
    try:
        subprocess.run(command, cwd=cwd, env=env, check=True)
    except FileNotFoundError:
        fail(f"required command not found: {command[0]}")
    except subprocess.CalledProcessError as exc:
        fail(f"command failed with exit code {exc.returncode}: {' '.join(command)}")


def load_manifest() -> tuple[dict, str]:
    try:
        manifest = json.loads((ROOT / "urtc.project.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read project manifest: {exc}")
    stack = manifest.get("stack")
    if not isinstance(stack, str):
        fail("manifest stack must be a string")
    return manifest, stack


def load_config() -> dict:
    path = ROOT / "tools" / "build_test.json"
    if not path.is_file():
        return {}
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read tools/build_test.json: {exc}")
    if not isinstance(config, dict):
        fail("tools/build_test.json must be an object")
    unknown = sorted(set(config) - {"pytest", "unittest", "python_extra"})
    if unknown:
        fail(f"tools/build_test.json has keys this script does not know: {', '.join(unknown)}")
    return config


def compile_python_sources() -> None:
    files = [
        path for path in ROOT.rglob("*.py")
        if not any(part in EXCLUDED_PARTS for part in path.parts)
    ]
    for path in files:
        compile(path.read_text(encoding="utf-8", errors="replace"), str(path), "exec")
    print(f"PYTHON_COMPILE=PASS files={len(files)}")


def qml_sources() -> list[Path]:
    return sorted((ROOT / "assets" / "qml").glob("*.qml"))


def validate_qtquick_deck() -> None:
    """Validate deployable Qt Quick deck sources without a graphics backend."""
    sources = qml_sources()
    if not sources:
        fail("Qt Quick stack is missing assets/qml/*.qml")
    qml_keys: set[str] = set()
    for source in sources:
        text = source.read_text(encoding="utf-8", errors="replace")
        if "import QtQuick" not in text:
            fail(f"Qt Quick import missing from {source.relative_to(ROOT)}")
        qml_keys.update(re.findall(r'uiText\("([A-Z0-9_]+)"\)', text))
    language_files = sorted((ROOT / "language").glob("*.lng"))
    if qml_keys and language_files:
        for language_file in language_files:
            keys = {
                line.split("=", 1)[0].strip()
                for line in language_file.read_text(encoding="utf-8", errors="replace").splitlines()
                if "=" in line and not line.lstrip().startswith("#")
            }
            missing = sorted(qml_keys - keys)
            if missing:
                fail(
                    f"QML language keys missing from {language_file.relative_to(ROOT)}: "
                    + ", ".join(missing)
                )
        print(f"QML_I18N=PASS keys={len(qml_keys)} languages={len(language_files)}")
    print(f"QML_SOURCE=PASS files={len(sources)} runtime=manual-visual-check")


def python_environment(pythonpath: list[str]) -> dict[str, str]:
    environment = dict(os.environ)
    environment["PYTHONPATH"] = os.pathsep.join(str(ROOT / item) for item in pythonpath) or str(ROOT)
    return environment


def run_python_tests(config: dict) -> None:
    """The tests the repository lists in tools/build_test.json. pytest is a development tool that a user of an installed program may not have: without it the
    run says the tests were SKIPPED, which is not a failed validation (nothing is installed from here unless the repository asks for it)."""
    pytest = config.get("pytest")
    if pytest:
        tests_dir = ROOT / pytest.get("dir", "tests")
        if tests_dir.is_dir():
            environment = python_environment(pytest.get("pythonpath", ["."]))
            probe = subprocess.run([sys.executable, "-m", "pytest", "--version"], capture_output=True, text=True, env=environment)
            if probe.returncode != 0 and pytest.get("install"):
                print("PYTEST=MISSING installing pytest for the tests of this repository")
                installed = subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", "pytest>=8"], capture_output=True, text=True, env=environment)
                if installed.returncode != 0:
                    detail = ((installed.stderr or installed.stdout).strip().splitlines() or [""])[-1][:200]
                    print(f"PYTEST_TESTS=SKIPPED pytest is not installed and could not be ({detail})")
                    return
                probe = subprocess.run([sys.executable, "-m", "pytest", "--version"], capture_output=True, text=True, env=environment)
            if probe.returncode != 0:
                print("PYTEST_TESTS=SKIPPED pytest is not installed")
            else:
                run(sys.executable, "-m", "pytest", "-q", str(tests_dir), env=environment)
    unittest_dir = config.get("unittest")
    if unittest_dir and (ROOT / unittest_dir).is_dir():
        run(sys.executable, "-m", "unittest", "discover", "-s", unittest_dir, "-v")
        print(f"UNIT_TESTS=PASS suite={unittest_dir}")


def run_python_extra(config: dict) -> None:
    for item in config.get("python_extra", []):
        cwd = ROOT / item.get("cwd", ".")
        if not (cwd / "tests").is_dir():
            continue
        modules = ", ".join(item.get("requires", []))
        if modules:
            probe = subprocess.run([sys.executable, "-c", f"import {modules}"], capture_output=True)
            if probe.returncode != 0:
                fail(f"the tests in {cwd.relative_to(ROOT)} need these Python modules: {modules}")
        run(sys.executable, *item["args"], cwd=cwd)


def wsl_command() -> list[str]:
    """The WSL command line of a firmware build on Windows: the distribution of URTC_WSL_DISTRO / URTC_WSL_DISTRIBUTION, or the default one."""
    wsl = ["wsl.exe"]
    distro = (os.environ.get("URTC_WSL_DISTRO") or os.environ.get("URTC_WSL_DISTRIBUTION") or "").strip()
    if distro:
        wsl += ["--distribution", distro]
    try:
        probe = subprocess.run(wsl + ["--", "sh", "-c", "command -v arm-none-eabi-gcc"], capture_output=True)
    except FileNotFoundError:
        fail("required command not found: wsl.exe (the firmware build runs inside WSL on Windows)")
    if probe.returncode != 0:
        fail(
            "arm-none-eabi-gcc was not found inside WSL"
            + (f" (distribution {distro})" if distro else " (default distribution)")
            + "; install gcc-arm-none-eabi there, or set URTC_WSL_DISTRO to a distribution that has it"
        )
    return wsl


def light_check(manifest: dict, stack: str, config: dict) -> int:
    """The check that builds and runs nothing: the stack is one this script knows, the files its full check needs are there, the Python compiles."""
    if stack not in STACKS:
        fail(f"unsupported stack: {stack}")
    marker = STACK_MARKERS.get(stack)
    if marker and not (ROOT / marker).is_file():
        fail(f"the {stack} stack needs {marker}")
    if stack == "go" and not ((ROOT / "go.mod").is_file() or (ROOT / "src" / "go.mod").is_file()):
        fail("the go stack needs go.mod")
    if stack == "android" and not (ROOT / "gradlew").is_file():
        fail("the android stack needs gradlew")
    if stack in PYTHON_STACKS:
        compile_python_sources()
        if stack == "python-qtquick":
            validate_qtquick_deck()
        pytest = config.get("pytest")
        if pytest and not (ROOT / pytest.get("dir", "tests")).is_dir():
            fail(f"tools/build_test.json names a tests folder that is not there: {pytest.get('dir', 'tests')}")
    if stack == "node":
        scripts = json.loads((ROOT / "package.json").read_text(encoding="utf-8")).get("scripts", {})
        print(f"NODE_SCRIPTS=PASS {','.join(name for name in ('typecheck', 'test', 'lint') if name in scripts) or 'none of typecheck, test, lint'}")
    if stack == "firmware-c":
        generator = ROOT / "tools" / "gen_build_scripts.py"
        if generator.is_file():
            run(sys.executable, str(generator), "--check")
    print(f"BUILD_TEST_CHECK=PASS project={manifest.get('name', ROOT.name)} stack={stack}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run a non-versioning build check for this repository.")
    parser.add_argument("--check", action="store_true", help="the light check: nothing is built and no test is run")
    args = parser.parse_args(argv)

    manifest, stack = load_manifest()
    config = load_config()
    print(f"BUILD_TEST project={manifest.get('name', ROOT.name)} stack={stack}")
    if args.check:
        return light_check(manifest, stack, config)
    refusal = local_build_refusal() if stack in COMPILED_STACKS else None
    if refusal is not None:
        print(f"BUILD_TEST=SKIPPED {refusal}")   # compiling on this PC is locked: skipped, never passed
        return 2

    # The Qt Quick variant shares the Python syntax baseline. A packaged Qt
    # smoke test belongs to the explicit build flow because it needs a desktop
    # runtime; this non-mutating test must remain safe on headless CI hosts.
    if stack in PYTHON_STACKS:
        compile_python_sources()
        if stack == "python-qtquick":
            validate_qtquick_deck()
        run_python_tests(config)
    elif stack == "node":
        npm = "npm.cmd" if os.name == "nt" else "npm"
        # Reuse an existing local dependency tree so a running editor/linter
        # cannot lock a native module while this non-destructive check runs.
        # CI starts without node_modules and therefore installs exactly from
        # the lockfile before exercising the same checks.
        if not (ROOT / "node_modules").is_dir():
            run(npm, "ci", "--ignore-scripts")
        else:
            print("NODE_DEPENDENCIES=REUSED existing node_modules")
        run(npm, "run", "typecheck", "--if-present")
        run(npm, "test", "--if-present")
        run(npm, "run", "lint", "--if-present")
    elif stack == "rust":
        run("cargo", "build", "--all-targets")
    elif stack == "go":
        module_root = ROOT / "src" if (ROOT / "src" / "go.mod").is_file() else ROOT
        run("go", "build", "./...", cwd=module_root)
    elif stack == "android":
        wrapper = "gradlew.bat" if os.name == "nt" else "./gradlew"
        wrapper_path = ROOT / wrapper
        if os.name != "nt":
            (ROOT / "gradlew").chmod((ROOT / "gradlew").stat().st_mode | 0o111)
        environment = dict(os.environ)
        environment["URTC_CI"] = "1"
        run(
            str(wrapper_path), "assembleDebug", "-PhydraUmcReadOnly=true",
            env=environment,
        )
    elif stack == "flutter":
        flutter = "flutter.bat" if os.name == "nt" else "flutter"
        run(flutter, "pub", "get")
        if ROOT.name == "HYDRA-UMC-IOS-CONTROL" and sys.platform == "darwin":
            run(flutter, "build", "ios", "--no-codesign")
        elif ROOT.name == "HYDRA-UMC-DSI" and os.name == "nt":
            run(flutter, "build", "windows")
        elif ROOT.name == "HYDRA-UMC-DSI" and sys.platform.startswith("linux"):
            run(flutter, "build", "linux")
        else:
            run(flutter, "analyze")
            print("FLUTTER_BUILD=SKIPPED no native desktop/mobile target is available on this host")
    elif stack == "firmware-c":
        script = ROOT / "build_firmware.sh"
        if not script.is_file():
            fail("firmware project is missing build_firmware.sh")
        environment = dict(os.environ)
        environment["URTC_CI"] = "1"
        if os.name == "nt":
            # WSL does not reliably inherit a Python child environment;
            # pass the read-only CI flag as an explicit WSL command argument.
            run(*wsl_command(), "--", "env", "URTC_CI=1", "bash", "build_firmware.sh")
        else:
            run("bash", "build_firmware.sh", env=environment)
        run_python_extra(config)
    else:
        fail(f"unsupported stack: {stack}")

    print("BUILD_TEST=PASS versioning=unchanged changelog=unchanged")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
