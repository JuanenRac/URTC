# =============================================================================
# URTC - tests/test_can_id_macros.py
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
"""The firmware names its CAN identifiers with the macros of the generated header, never with a literal number.

The header ``src/common/urtc_can_ids.h`` is a copy of the one URTC-COMMON writes from the specification; the lock file
beside it records what it was copied from. A literal such as ``0x7F9`` in a comparison or a send would be a second place
to change when the specification moves, so the test fails on it.
"""
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT / "src" / "common"
SKIP_PARTS = ("hal_src", "hal_inc", "melexis")
LITERAL = re.compile(
    r"(?:StdId\s*(?:==|=|!=)\s*|\b(?:SendCanResponse|SendChunkFrames|send_frame)\(\s*|\bf\.id\s*=\s*)0x[0-9A-Fa-f]{3}\b"
)


def sources() -> list[Path]:
    return [p for p in sorted((ROOT / "src").rglob("*.c")) if not any(part in str(p) for part in SKIP_PARTS)]


class CanIdMacros(unittest.TestCase):
    def test_header_matches_its_lock(self) -> None:
        lock = json.loads((COMMON / "urtc_common.lock.json").read_text(encoding="utf-8"))
        entry = lock["files"]["urtc_can_ids.h"]
        data = (COMMON / "urtc_can_ids.h").read_bytes().replace(b"\r\n", b"\n")
        self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"], "urtc_can_ids.h was edited by hand: copy it again with vendor_copy.py")

    def test_no_identifier_literal_in_the_firmware(self) -> None:
        found = []
        for path in sources():
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                code = line.split("//", 1)[0]
                if LITERAL.search(code):
                    found.append(f"{path.relative_to(ROOT)}:{number}: {line.strip()}")
        self.assertEqual(found, [], "name the identifier with its URTC_CAN_ID_ macro:\n" + "\n".join(found))

    def test_every_macro_used_exists(self) -> None:
        header = (COMMON / "urtc_can_ids.h").read_text(encoding="utf-8")
        defined = set(re.findall(r"#define (URTC_CAN_ID_\w+)", header))
        used = set()
        for path in sources():
            used |= set(re.findall(r"\bURTC_CAN_ID_\w+", path.read_text(encoding="utf-8")))
        used -= {"URTC_CAN_ID_VERSION_PATCH"}  # the alias of urtc_version_wire.h
        self.assertEqual(sorted(used - defined), [])


if __name__ == "__main__":
    unittest.main()
