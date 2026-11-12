#!/usr/bin/env python3
# =============================================================================
# URTC - tools/read_header_version.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""Prints the version a firmware header carries now, as MAJOR.MINOR.PATCH. Read-only.

The build scripts use it to name the images they make: compiling never changes a version, raising one is the explicit `python3 bump.py`.

Usage: python3 tools/read_header_version.py <header_path> <MACRO_PREFIX>
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from bump_version import get_macro  # noqa: E402  (the same reader of the version macros that the bump uses)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: read_header_version.py <header_path> <MACRO_PREFIX>", file=sys.stderr)
        return 1
    text = Path(argv[1]).read_text(encoding="utf-8")
    print(".".join(str(get_macro(text, argv[1], argv[2], part)) for part in ("MAJOR", "MINOR", "PATCH")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
