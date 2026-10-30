"""The generated block of VERSION_CHECKLIST.txt follows the manifest and leaves the hand-written text alone."""
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("version_checklist_state", ROOT / "tools" / "version_checklist_state.py")
tool = importlib.util.module_from_spec(spec)
sys.modules["version_checklist_state"] = tool
spec.loader.exec_module(tool)

MANIFEST = {
    "version": "1.2.3",
    "components": [{"name": "URTC_X", "version": "0.1.2", "target": "chip", "source": {"file": "a.h", "prefix": "FIRMWARE_VERSION"}}],
}
TEXT = "=====\nTITLE\n=====\n\nhand written text\n"


class VersionChecklistStateTests(unittest.TestCase):
    def test_the_block_is_added_once_below_the_banner_and_the_rest_is_kept(self):
        once = tool.rewrite(TEXT, tool.build_block(MANIFEST))
        twice = tool.rewrite(once, tool.build_block(MANIFEST))
        self.assertEqual(once, twice)
        self.assertIn("hand written text", once)
        self.assertIn("URTC_X", once)
        self.assertLess(once.index(tool.BEGIN), once.index("hand written text"))

    def test_a_new_version_replaces_the_old_row(self):
        old = tool.rewrite(TEXT, tool.build_block(MANIFEST))
        newer = dict(MANIFEST, components=[dict(MANIFEST["components"][0], version="0.1.3")])
        updated = tool.rewrite(old, tool.build_block(newer))
        self.assertIn("0.1.3", updated)
        self.assertNotIn("0.1.2 ", updated)

    def test_the_checklist_of_the_repository_is_current(self):
        with tempfile.TemporaryDirectory():
            self.assertEqual(tool.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
