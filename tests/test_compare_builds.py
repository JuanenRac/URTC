"""tools/compare_builds.py: the images of the two build scripts are compared byte by byte."""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import compare_builds  # noqa: E402


def write(folder: Path, files: dict[str, bytes]) -> None:
    for name, data in files.items():
        (folder / name).write_bytes(data)


class Compare(unittest.TestCase):
    def test_the_same_images_pass(self):
        files = {"URTC_a_v1.bin": b"\x01\x02\x03", "URTC_b_v1.bin": b"abc"}
        self.assertEqual(compare_builds.compare(files, dict(files)), [])

    def test_a_different_byte_is_named_with_its_offset(self):
        problems = compare_builds.compare({"URTC_a.bin": b"abcd"}, {"URTC_a.bin": b"abXd"})
        self.assertEqual(len(problems), 1)
        self.assertIn("first different byte at offset 2", problems[0])

    def test_an_image_of_one_side_only_is_named(self):
        problems = compare_builds.compare({"URTC_a.bin": b"1", "URTC_b.bin": b"2"}, {"URTC_a.bin": b"1", "URTC_c.bin": b"3"})
        self.assertEqual(sorted(problems), ["URTC_b.bin: made by build_firmware.sh only", "URTC_c.bin: made by build_firmware.bat only"])

    def test_a_longer_image_differs_at_the_end_of_the_short_one(self):
        problems = compare_builds.compare({"URTC_a.bin": b"ab"}, {"URTC_a.bin": b"abc"})
        self.assertIn("offset 2", problems[0])

    def test_empty_folders_do_not_pass(self):
        self.assertTrue(compare_builds.compare({}, {}))

    def test_the_command_line_reports_and_writes_the_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            sh, bat, summary = Path(tmp, "sh"), Path(tmp, "bat"), Path(tmp, "summary.md")
            sh.mkdir()
            bat.mkdir()
            write(sh, {"URTC_a.bin": b"same", "notes.txt": b"ignored"})
            write(bat, {"URTC_a.bin": b"same"})
            self.assertEqual(compare_builds.main([str(sh), str(bat), "--summary", str(summary)]), 0)
            self.assertIn("URTC_a.bin", summary.read_text(encoding="utf-8"))
            write(bat, {"URTC_a.bin": b"diff"})
            self.assertEqual(compare_builds.main([str(sh), str(bat)]), 1)


if __name__ == "__main__":
    unittest.main()
