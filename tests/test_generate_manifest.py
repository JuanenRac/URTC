"""Tests for generate_manifest.py: reproducible timestamps, image digests and the bootloader region size."""
import hashlib
import importlib.util
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

_spec = importlib.util.spec_from_file_location("firmware_generate_manifest", ROOT / "generate_manifest.py")
gm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gm)


class BuildTimestampTests(unittest.TestCase):
    def test_source_date_epoch_is_used(self):
        env = {"SOURCE_DATE_EPOCH": "1700000000"}
        self.assertEqual(gm.build_timestamp(ROOT, env), "2023-11-14T22:13:20Z")

    def test_source_date_epoch_wins_over_ci_mode(self):
        env = {"SOURCE_DATE_EPOCH": "0", "URTC_CI": "1"}
        self.assertEqual(gm.build_timestamp(ROOT, env), "1970-01-01T00:00:00Z")

    def test_ci_mode_uses_last_commit_date_and_is_repeatable(self):
        try:
            commit = subprocess.run(
                ["git", "log", "-1", "--format=%ct"], cwd=ROOT,
                capture_output=True, text=True, check=True,
            ).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            self.skipTest("not a git checkout")
        env = {"URTC_CI": "1"}
        first = gm.build_timestamp(ROOT, env)
        self.assertEqual(first, gm.build_timestamp(ROOT, env))
        self.assertEqual(
            first, gm.datetime.fromtimestamp(int(commit), gm.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        )

    def test_invalid_epoch_is_rejected(self):
        with self.assertRaises(SystemExit):
            gm.build_timestamp(ROOT, {"SOURCE_DATE_EPOCH": "not-a-number"})

    def test_local_build_uses_current_time_format(self):
        stamp = gm.build_timestamp(ROOT, {})
        self.assertRegex(stamp, r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")


class ImageDigestTests(unittest.TestCase):
    def test_every_companion_image_carries_its_sha256(self):
        with tempfile.TemporaryDirectory() as folder:
            image = Path(folder) / "URTC_MAIN_FIRMWARE_v0.0.1.bin"
            image.write_bytes(b"AB")
            hex_file = Path(folder) / "URTC_MAIN_FIRMWARE_v0.0.1.hex"
            hex_file.write_bytes(b":00000001FF")
            files = gm.component_files(folder, "URTC_MAIN_FIRMWARE")
        self.assertEqual(files["bin"]["sha256"], hashlib.sha256(b"AB").hexdigest())
        self.assertEqual(files["hex"]["sha256"], hashlib.sha256(b":00000001FF").hexdigest())
        self.assertEqual(files["hex"]["size_bytes"], 11)
        self.assertIsNone(files["elf"])


class BootloaderRegionTests(unittest.TestCase):
    def test_the_manifest_and_the_linker_script_agree_on_the_bootloader_size(self):
        script = (ROOT / "src" / "F303-master" / "boot" / "STM32F303CCTx_BOOTLOADER.ld").read_text(encoding="utf-8")
        kilobytes = int(re.search(r"FLASH \(rx\)\s*:\s*ORIGIN = 0x08000000, LENGTH = (\d+)K", script).group(1))
        source = (ROOT / "generate_manifest.py").read_text(encoding="utf-8")
        declared = int(re.search(r'"main_bootloader".*?"flash_region_max_bytes": (\d+)', source, re.S).group(1))
        self.assertEqual(declared, kilobytes * 1024)
        header = (ROOT / "src" / "F303-master" / "boot" / "bootloader_common.h").read_text(encoding="utf-8")
        metadata = int(re.search(r"#define METADATA_ADDR\s+0x([0-9A-Fa-f]+)UL", header).group(1), 16)
        self.assertEqual(metadata - 0x08000000, declared)


if __name__ == "__main__":
    unittest.main()
