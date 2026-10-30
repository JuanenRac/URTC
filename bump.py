#!/usr/bin/env python3
# =============================================================================
# URTC - bump.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Raises the version of firmware components. Compiling never does this: it is an explicit step done before a build.

    python bump.py                    raise all four components (main/slave, application/bootloader)
    python bump.py main-app slave-boot   raise only the named ones
    python bump.py --dry-run          show what would change, write nothing
    python bump.py --list             show the current versions

A component's version is its own three-number odometer (patch 0-9 carries into minor, minor into major), kept in its header
(see bump_version.py). Raising the main application also mirrors the version into its bootloader's copy and synchronises
urtc.project.json (version and components[]) and CHANGELOG.md through bump_manifest_version.py --sync.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MANIFEST = ROOT / "urtc.project.json"

#: key -> (component name in the manifest, header, macro prefix, bootloader header that mirrors an application version)
COMPONENTS = {
    "main-boot": ("URTC_MAIN_BOOTLOADER", "src/F303-master/boot/bootloader_common.h", "BOOTLOADER_VERSION", None),
    "main-app": ("URTC_MAIN_FIRMWARE", "src/F303-master/firmware_common.h", "FIRMWARE_VERSION", "src/F303-master/boot/bootloader_common.h"),
    "slave-boot": ("URTC_SLAVE_BOOTLOADER", "src/F303-slave/boot/slaveboot_common.h", "BOOTLOADER_VERSION", None),
    "slave-app": ("URTC_SLAVE_FIRMWARE", "src/F303-slave/slave_common.h", "FIRMWARE_VERSION", "src/F303-slave/boot/slaveboot_common.h"),
}


def current(header: str, prefix: str) -> str:
    text = (ROOT / header).read_text(encoding="utf-8", errors="replace")
    parts = []
    for part in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(rf"#define\s+{prefix}_{part}\s+(\d+)", text)
        if match is None:
            raise SystemExit(f"bump.py: {prefix}_{part} not found in {header}")
        parts.append(match.group(1))
    return ".".join(parts)


def sync_components() -> bool:
    """Writes each header's current version into the matching components[] entry of the manifest."""
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    changed = False
    for component in data.get("components", []):
        for name, header, prefix, _mirror in COMPONENTS.values():
            if component.get("name") == name and component.get("version") != current(header, prefix):
                component["version"] = current(header, prefix)
                changed = True
    if changed:
        MANIFEST.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return changed


def run(*command: str) -> str:
    result = subprocess.run([sys.executable, *command], cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(result.returncode)
    return result.stdout.strip()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Raise firmware component versions (compiling never does).")
    parser.add_argument("components", nargs="*", metavar="COMPONENT",
                        help="main-app, main-boot, slave-app or slave-boot (default: all four)")
    parser.add_argument("--dry-run", action="store_true", help="show the new versions, write nothing")
    parser.add_argument("--list", action="store_true", help="show the current versions and stop")
    args = parser.parse_args(argv)

    if args.list:
        for key, (name, header, prefix, _mirror) in COMPONENTS.items():
            print(f"{key:11} {name:22} {current(header, prefix)}")
        return 0

    unknown = [name for name in args.components if name not in COMPONENTS]
    if unknown:
        parser.error(f"unknown component(s): {', '.join(unknown)} (choose from {', '.join(COMPONENTS)})")
    chosen = args.components or list(COMPONENTS)
    sys.path.insert(0, str(ROOT))
    import bump_version

    for key in chosen:
        name, header, prefix, mirror = COMPONENTS[key]
        before = current(header, prefix)
        after = ".".join(str(n) for n in bump_version.next_version(*(int(n) for n in before.split("."))))
        if args.dry_run:
            print(f"{key}: {before} -> {after} (dry run)")
            continue
        command = ["bump_version.py", header, prefix] + ([mirror] if mirror else [])
        print(f"{key}: {before} -> {run(*command)}")
    if args.dry_run:
        return 0
    if "main-app" in chosen:
        print(run("bump_manifest_version.py", "--sync"))
    if sync_components():
        print("urtc.project.json components[] synchronised")
    if (ROOT / "tools" / "version_checklist_state.py").exists() and (ROOT / "VERSION_CHECKLIST.txt").exists():
        print(run("tools/version_checklist_state.py"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
