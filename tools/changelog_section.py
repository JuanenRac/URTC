#!/usr/bin/env python3
# =============================================================================
# URTC - tools/changelog_section.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Prints the CHANGELOG.md section of one version: the text of a release.

    python tools/changelog_section.py 0.3.9            the section "## [0.3.9] - ..." without its heading line
    python tools/changelog_section.py 0.3.9 --heading  with the heading line
    python tools/changelog_section.py 0.3.9 --compat   followed by the compatibility table of docs/COMPATIBILITY.md

Exit status 1 when the version has no section or its section is empty, so a release job stops instead of
publishing a release with no notes.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def section(text: str, version: str, with_heading: bool = False) -> str | None:
    """The body of the `## [version]` section, or None when the version has none or the section is empty."""
    lines = text.replace("\r\n", "\n").split("\n")
    start = None
    for index, line in enumerate(lines):
        if re.match(rf"^## \[{re.escape(version)}\](\s|$)", line):
            start = index
            break
    if start is None:
        return None
    end = len(lines)
    for index in range(start + 1, len(lines)):
        if lines[index].startswith("## "):
            end = index
            break
    body = "\n".join(lines[start + 1:end]).strip()
    if not body:
        return None
    return (lines[start] + "\n\n" + body if with_heading else body) + "\n"


def compatibility_table(root: Path = ROOT) -> str | None:
    """The generated compatibility table (protocol, firmware, programs) without its generator comment, or None when the file is missing."""
    path = root / "docs" / "COMPATIBILITY.md"
    if not path.is_file():
        return None
    lines = [ln for ln in path.read_text(encoding="utf-8").splitlines() if not ln.startswith("<!-- GENERATED")]
    return "\n".join(lines).strip() + "\n"


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 1 or not re.fullmatch(r"\d+(\.\d+){2,3}", args[0]):
        print("usage: changelog_section.py VERSION [--heading] [--compat]", file=sys.stderr)
        return 2
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    found = section(text, args[0], "--heading" in argv)
    if found is None:
        print(f"CHANGELOG.md has no section for {args[0]}", file=sys.stderr)
        return 1
    if "--compat" in argv:
        table = compatibility_table()
        if table is None:
            print("docs/COMPATIBILITY.md is missing: copy it from URTC-COMMON (tools/vendor_copy.py sync --set compat)", file=sys.stderr)
            return 1
        found = found + "\n" + table
    sys.stdout.write(found)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
