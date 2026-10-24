"""The tool table of docs/TOOLS.TXT: the block between the markers must be the text its marker was written for, and the prose must
keep the numbers of the sections in the order of the identifiers."""
import hashlib
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC = ROOT / "docs" / "TOOLS.TXT"
BEGIN = re.compile(r"^\[\[ BEGIN GENERATED (?P<name>[a-z0-9_]+) sha256=(?P<digest>[0-9a-f]{64}) \]\]$", re.M)
HEADING = re.compile(r"^(\d+)\. .+ - ID 0x([0-9A-F]{2})$", re.M)


class ToolsDocTests(unittest.TestCase):
    def setUp(self):
        self.text = DOC.read_bytes().decode("utf-8").replace("\r\n", "\n")

    def test_the_table_is_there_once_and_closed(self):
        self.assertEqual([m.group("name") for m in BEGIN.finditer(self.text)], ["tool_table"])
        self.assertEqual(self.text.count("[[ END GENERATED tool_table ]]\n"), 1)

    def test_the_table_was_not_edited_by_hand(self):
        # tools/generate_tools_doc.py of URTC-COMMON writes the table from spec/tools.json and the digest into its marker
        match = BEGIN.search(self.text)
        end = self.text.index("[[ END GENERATED tool_table ]]\n", match.end())
        body = self.text[match.end() + 1:end]
        self.assertEqual(hashlib.sha256(body.encode("utf-8")).hexdigest(), match.group("digest"))

    def test_the_table_has_one_row_for_each_profile_in_order(self):
        rows = re.findall(r"^\| (\d+) \(0x([0-9A-F]{2})\) \|", self.text, re.M)
        self.assertEqual(len(rows), 25)
        self.assertEqual([int(n) for n, _ in rows], list(range(25)))
        self.assertEqual([int(h, 16) for _, h in rows], list(range(25)))

    def test_the_sections_of_the_prose_are_numbered_like_the_identifiers(self):
        sections = HEADING.findall(self.text)
        self.assertEqual([(int(n), int(h, 16)) for n, h in sections], [(i + 1, i) for i in range(25)])


if __name__ == "__main__":
    unittest.main()
