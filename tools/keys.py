#!/usr/bin/env python3
# =============================================================================
# URTC - tools/keys.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The keys of the URTC family: list them, and check that every public key embedded in a program or a bootloader is the registered one.

    python tools/keys.py list                 # the keys, what each is for and where it is embedded
    python tools/keys.py verify               # compare every embedding with keys/registry.json
    python tools/keys.py verify --strict      # a repository that is not next to this one counts as a failure
    python tools/keys.py verify --root DIR    # the folder that holds the repositories (default: the parent of this one)

The registry (keys/registry.json) holds only public values and file names. ``verify`` reads each file named there, finds the
key with the pattern given for it (or, for a C array, the development branch of the bootloader) and compares it with the
registered value; it prints one line per embedding and exits 1 when one differs, is missing or cannot be found. Without
``--strict`` a repository that is not checked out beside this one is reported as skipped, not as a failure. It never reads a
private key and never needs one.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "keys" / "registry.json"


def load_registry(path: Path = REGISTRY) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    for key in data["keys"]:
        if not re.fullmatch(r"[0-9a-f]{64}", key["public"]):
            raise ValueError(f"{key['id']}: the registered value is not 64 lowercase hexadecimal digits")
    return data


def extract(location: dict, text: str) -> str | None:
    """The key value found in a file, lowercase hexadecimal, or None when the file has no such key."""
    if location["kind"] == "text":
        match = re.search(location["pattern"], text)
        return match.group(1).lower() if match else None
    if location["kind"] == "c_array":
        # the development branch: the array that follows `#else`, as firmware_signing.dev_key reads it
        match = re.search(r"#else.*?const uint8_t " + re.escape(location["array"]) + r"\[(\d+)\]\s*=\s*\{(.*?)\};", text, re.S)
        if not match:
            return None
        values = re.findall(r"0[xX]([0-9A-Fa-f]{2})", match.group(2))
        if len(values) != int(match.group(1)):
            return None
        return "".join(values).lower()
    raise ValueError(f"unknown embedding kind {location['kind']!r}")


def verify(root: Path, registry: dict, strict: bool = False) -> tuple[list[str], list[str]]:
    """(report lines, problems). A problem is a mismatch, a file or key that cannot be found, or - with strict - a missing repository."""
    lines: list[str] = []
    problems: list[str] = []
    for key in registry["keys"]:
        for loc in key["embedded_in"]:
            label = f"{key['id']} in {loc['repo']}/{loc['file']}"
            base = ROOT if loc["repo"] == "URTC" and not (root / "URTC").is_dir() else root / loc["repo"]
            if not base.is_dir():
                if strict:
                    problems.append(f"{label}: repository not found at {base}")
                    lines.append(f"MISSING {label}")
                else:
                    lines.append(f"SKIPPED {label} (repository not next to this one)")
                continue
            path = base / loc["file"]
            if not path.is_file():
                problems.append(f"{label}: file not found")
                lines.append(f"MISSING {label}")
                continue
            found = extract(loc, path.read_text(encoding="utf-8", errors="replace"))
            if found is None:
                problems.append(f"{label}: no key found with the pattern of the registry")
                lines.append(f"NOT-FOUND {label}")
            elif found != key["public"]:
                problems.append(f"{label}: embedded {found} but registered {key['public']}")
                lines.append(f"DIFFERENT {label}")
            else:
                lines.append(f"OK {label}")
    return lines, problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list")
    check = sub.add_parser("verify")
    check.add_argument("--strict", action="store_true")
    check.add_argument("--root", default=str(ROOT.parent))
    args = parser.parse_args(argv)
    registry = load_registry()
    if args.command == "list":
        for key in registry["keys"]:
            print(f"{key['id']}  ({key['algorithm']})\n  {key['public']}\n  {key['role']}")
            for loc in key["embedded_in"]:
                print(f"    in {loc['repo']}/{loc['file']}")
        return 0
    lines, problems = verify(Path(args.root), registry, args.strict)
    print("\n".join(lines))
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print(f"KEYS=FAIL {len(problems)} problem(s)", file=sys.stderr)
        return 1
    checked = sum(1 for line in lines if line.startswith("OK"))
    print(f"KEYS=PASS {checked} embedding(s) match the registry, {len(lines) - checked} skipped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
