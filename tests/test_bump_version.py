"""Boundary tests for the firmware odometer in bump_version.py.

Firmware keeps its own three-component odometer: it must keep working
(and keep printing a plain MAJOR.MINOR.PATCH) across the versions where the
project manifest grows a fourth component.
"""
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "bump_version.py"

_spec = importlib.util.spec_from_file_location("firmware_bump_version", SCRIPT)
bump = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bump)


def header(prefix, major, minor, patch):
    return (
        f"#define {prefix}_MAJOR {major}\n"
        f"#define {prefix}_MINOR {minor}\n"
        f"#define {prefix}_PATCH {patch}\n"
    )


class NextVersionTests(unittest.TestCase):
    def test_ordinary_step(self):
        self.assertEqual(bump.next_version(0, 1, 1), (0, 1, 2))

    def test_patch_carry(self):
        self.assertEqual(bump.next_version(0, 3, 9), (0, 4, 0))

    def test_boundary_0_7_9(self):
        self.assertEqual(bump.next_version(0, 7, 9), (0, 8, 0))

    def test_boundary_0_9_9(self):
        self.assertEqual(bump.next_version(0, 9, 9), (1, 0, 0))

    def test_boundary_9_9_9(self):
        self.assertEqual(bump.next_version(9, 9, 9), (10, 0, 0))

    def test_minor_already_past_nine_still_carries(self):
        self.assertEqual(bump.next_version(0, 9, 0), (0, 9, 1))
        self.assertEqual(bump.next_version(1, 10, 0), (2, 0, 1))


class BumpScriptTests(unittest.TestCase):
    def run_bump(self, prefix, version, mirror_version=None):
        with tempfile.TemporaryDirectory() as tmp:
            primary = Path(tmp) / "common.h"
            primary.write_text(header(prefix, *version), encoding="utf-8")
            args = [sys.executable, str(SCRIPT), str(primary), prefix]
            mirror = None
            if mirror_version is not None:
                mirror = Path(tmp) / "boot_common.h"
                mirror.write_text(header(prefix, *mirror_version), encoding="utf-8")
                args.append(str(mirror))
            result = subprocess.run(args, capture_output=True, text=True, cwd=tmp)
            return (
                result,
                primary.read_text(encoding="utf-8"),
                mirror.read_text(encoding="utf-8") if mirror else None,
            )

    def test_script_runs_at_each_boundary(self):
        for version, expected in (
            ((0, 7, 9), "0.8.0"),
            ((0, 9, 9), "1.0.0"),
            ((9, 9, 9), "10.0.0"),
        ):
            with self.subTest(version=version):
                result, text, _ = self.run_bump("FIRMWARE_VERSION", version)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), expected)
                major, minor, patch = (int(p) for p in expected.split("."))
                self.assertEqual(text, header("FIRMWARE_VERSION", major, minor, patch))

    def test_mirror_header_follows_at_boundary(self):
        result, text, mirror = self.run_bump(
            "FIRMWARE_VERSION", (0, 7, 9), mirror_version=(0, 7, 9)
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "0.8.0")
        self.assertEqual(mirror, text)

    def test_stdout_is_only_the_version(self):
        result, _, _ = self.run_bump("BOOTLOADER_VERSION", (0, 7, 9))
        self.assertEqual(result.stdout.strip().count("."), 2)
        self.assertEqual(result.stdout.strip().count("\n"), 0)


if __name__ == "__main__":
    unittest.main()
