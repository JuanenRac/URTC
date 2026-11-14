#!/usr/bin/env python3
# =============================================================================
# URTC - tools/check_image_versions.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Checks that the version inside each firmware image is the version its name and the build manifest state.

    python tools/check_image_versions.py DIR [--main-version X.Y.Z]

DIR holds the four images of one build and its firmware_manifest.json. For each of the two application images the version is read from the header
slot that the firmware emits at a fixed offset (tools/seal_image.py seals it); it must equal the version in the name of the file, the version of the
manifest and the hardware of its board. The two bootloaders carry their version as code constants, not as a readable slot, so for them the name of the
file and the manifest are compared with each other and with the sidecar; the version macros in the header files were already read by the build to
name the files. With --main-version the main application must be exactly that version (the version of urtc.project.json).
Prints CHECK_IMAGE_VERSIONS=PASS and exits 0, or names each problem and exits 1.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from image_record import image_header  # noqa: E402

EXPECTED = {"main_bootloader", "main_application", "slave_bootloader", "slave_application"}
NAME_VERSION = re.compile(r"_v(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?\.bin$")
HARDWARE = {"main_application": 0x0303CC01, "slave_application": 0x0303CB01}


def padded(text: str) -> tuple[int, int, int, int]:
    parts = [int(part) for part in text.split(".")]
    return (*parts, *([0] * (4 - len(parts))))[:4]  # type: ignore[return-value]


def check(folder: Path, main_version: str | None = None) -> list[str]:
    problems: list[str] = []
    try:
        components = json.loads((folder / "firmware_manifest.json").read_text(encoding="utf-8"))["components"]
    except (OSError, ValueError, KeyError) as error:
        return [f"firmware_manifest.json cannot be read: {error}"]
    if set(components) != EXPECTED:
        return ["the manifest holds " + ", ".join(sorted(components)) + " instead of the four components"]
    if main_version is not None and components["main_application"].get("version_string") != main_version:
        problems.append(f"main_application is {components['main_application'].get('version_string')}, expected {main_version}")
    for key, component in sorted(components.items()):
        text = str(component.get("version_string"))
        filename = component["files"]["bin"]["filename"]
        found = NAME_VERSION.search(filename)
        if not found or padded(".".join(g for g in found.groups() if g is not None)) != padded(text):
            problems.append(f"{filename}: the name does not carry version {text}")
        image = folder / filename
        if not image.is_file():
            problems.append(f"{filename}: missing")
            continue
        if key in HARDWARE:
            status, info = image_header.verify_embedded(image.read_bytes())
            if info is None:
                problems.append(f"{filename}: the header slot inside the image does not verify (status {status})")
            elif info.version != padded(text):
                problems.append(f"{filename}: the image states {info.version_text()}, the manifest {text}")
            elif info.hardware_id != HARDWARE[key]:
                problems.append(f"{filename}: the image is for hardware 0x{info.hardware_id:08X}")
        sidecar = folder / (filename + ".manifest.json")
        if sidecar.is_file() and padded(str(json.loads(sidecar.read_text(encoding="utf-8")).get("version"))) != padded(text):
            problems.append(f"{sidecar.name}: its version differs from the manifest ({text})")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("folder", type=Path)
    parser.add_argument("--main-version")
    args = parser.parse_args(argv)
    problems = check(args.folder, args.main_version)
    for problem in problems:
        print("CHECK_IMAGE_VERSIONS problem:", problem)
    print("CHECK_IMAGE_VERSIONS=" + ("FAIL" if problems else "PASS"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
