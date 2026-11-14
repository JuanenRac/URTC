"""The generated reference of docs/CANBUS.TXT: a block between the markers must be the text its marker was written for."""
import hashlib
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC = ROOT / "docs" / "CANBUS.TXT"
BEGIN = re.compile(r"^\[\[ BEGIN GENERATED (?P<name>[a-z0-9_]+) sha256=(?P<digest>[0-9a-f]{64}) \]\]$", re.M)
EXPECTED = {"summary", "index", "layouts", "tools", "enums", "constants", "magic", "registers", "procedures", "multinode", "rack"}


def blocks(text):
    found = {}
    for match in BEGIN.finditer(text):
        end = text.index(f"[[ END GENERATED {match.group('name')} ]]\n", match.end())
        found[match.group("name")] = (match.group("digest"), text[match.end() + 1:end])
    return found


class CanbusDocTests(unittest.TestCase):
    def setUp(self):
        self.text = DOC.read_bytes().decode("utf-8").replace("\r\n", "\n")

    def test_the_generated_blocks_are_all_there(self):
        self.assertEqual(set(blocks(self.text)), EXPECTED)

    def test_no_generated_block_was_edited_by_hand(self):
        # tools/generate_canbus_doc.py of URTC-COMMON writes the blocks from the specification and the digest of each one into its marker;
        # to change a table, change the specification there and generate again.
        for name, (digest, body) in blocks(self.text).items():
            self.assertEqual(hashlib.sha256(body.encode("utf-8")).hexdigest(), digest, f"block '{name}' was edited by hand")

    def test_every_marker_is_closed_once(self):
        for name in EXPECTED:
            self.assertEqual(self.text.count(f"[[ BEGIN GENERATED {name} "), 1)
            self.assertEqual(self.text.count(f"[[ END GENERATED {name} ]]\n"), 1)

    def test_the_weld_pulse_options_of_the_firmware_are_in_the_text(self):
        # the pulse note of 0x1C0 and 0x200 used to say that a hardware timer was "not implemented" after the option existed: every build option of
        # weld_pulse_timing.h is named in the reference, and the sentence that denied the timer is gone
        header = (ROOT / "src" / "F303-master" / "weld_pulse_timing.h").read_text(encoding="utf-8")
        options = set(re.findall(r"^#ifndef (URTC_WELD_PULSE_[A-Z_]+_(?:MS|TIMER))$", header, re.M))
        self.assertGreaterEqual(len(options), 3)
        for option in sorted(options):
            self.assertIn(option, self.text, f"{option} is a build option of the weld pulse that CANBUS.TXT does not name")
        self.assertNotIn("interrupt, not implemented", self.text)


if __name__ == "__main__":
    unittest.main()
