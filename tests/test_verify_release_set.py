"""Tests for tools/verify_release_set.py: what the release workflow accepts from the signed files the maintainer uploads.

A throwaway Ed25519 key stands in for the key of the releases (the real one is never read by a test).
"""
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
_spec = importlib.util.spec_from_file_location("verify_release_set", ROOT / "tools" / "verify_release_set.py")
vrs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(vrs)
from test_publish_release_assets import make_component  # noqa: E402

try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError:
    Ed25519PrivateKey = None

sign = vrs.release_sign


@unittest.skipIf(Ed25519PrivateKey is None, "cryptography is not installed")
class VerifyReleaseSetTests(unittest.TestCase):
    def setUp(self):
        self.key = Ed25519PrivateKey.generate()
        self.public = sign.public_hex_of(self.key)
        self._saved = (sign.PUBLIC_KEY_HEX, sign._private_key)
        sign.PUBLIC_KEY_HEX = self.public
        sign._private_key = lambda: self.key
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.fw = self.root / "signed"
        self.fw.mkdir()
        (self.root / "urtc.project.json").write_text(json.dumps({"name": "URTC", "version": "1.2.3"}))

    def tearDown(self):
        sign.PUBLIC_KEY_HEX, sign._private_key = self._saved
        self.tmp.cleanup()

    def make_signed_set(self, kind="production"):
        comp = {
            "main_bootloader": make_component(self.fw, "URTC_MAIN_BOOTLOADER", "1.0.1", b"boot", False),
            "main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True, kind),
        }
        (self.fw / "firmware_manifest.json").write_text(json.dumps({"components": comp}))
        # what the maintainer does on their machine: tools/publish_release_assets.py --sign
        plan = vrs.publish_release_assets.build_plan(self.root, self.fw, False, "o/r", allow_dev_key=True, sign=True)
        self.assertEqual(plan["problems"], [])
        for stray in self.fw.glob("*.elf"):
            stray.unlink()

    def check(self, tag="v1.2.3"):
        # the records of these sets are placeholders: their cryptography is held by tests/test_verify_image_records.py
        return vrs.verify_set(self.fw, tag, root=self.root, public_hex=self.public, records=False)

    def test_a_complete_signed_set_passes_and_lists_every_file(self):
        self.make_signed_set()
        problems, names = self.check()
        self.assertEqual(problems, [])
        self.assertIn("firmware_manifest.json.sig", names)
        self.assertIn("URTC_MAIN_FIRMWARE_v1.2.3.bin", names)
        self.assertEqual(names, sorted(path.name for path in self.fw.iterdir()))

    def test_a_changed_file_is_refused(self):
        self.make_signed_set()
        (self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin").write_bytes(b"tampered")
        problems, _ = self.check()
        self.assertTrue(problems)

    def test_a_missing_signature_is_refused(self):
        self.make_signed_set()
        (self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin.sig").unlink()
        problems, _ = self.check()
        self.assertTrue(any("no URTC_MAIN_FIRMWARE_v1.2.3.bin.sig" in p for p in problems))

    def test_a_signature_by_another_key_is_refused(self):
        self.make_signed_set()
        other = Ed25519PrivateKey.generate()
        image = self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin"
        # the pinned-key guard refuses to sign with a foreign key, so the signature is written by hand
        signature = other.sign(sign.release_message(image.name, sign.sha256_file(image))).hex()
        Path(str(image) + ".sig").write_text(signature + "\n")
        problems, _ = self.check()
        self.assertTrue(problems)

    def test_the_wrong_tag_is_refused(self):
        self.make_signed_set()
        problems, _ = self.check("v9.9.9")
        self.assertTrue(any("v1.2.3" in p for p in problems))

    def test_images_signed_with_the_development_key_are_refused(self):
        self.make_signed_set("dev")
        problems, _ = self.check()
        self.assertTrue(problems)

    def test_a_file_the_set_does_not_account_for_is_refused(self):
        self.make_signed_set()
        (self.fw / "notes.txt").write_text("not part of the set")
        problems, _ = self.check()
        self.assertTrue(any("notes.txt" in p for p in problems))

    def test_a_public_key_that_is_not_the_pinned_one_is_refused(self):
        self.make_signed_set()
        problems, _ = vrs.verify_set(self.fw, "v1.2.3", root=self.root, public_hex="00" * 32)
        self.assertTrue(any("pinned" in p for p in problems))

    def test_the_registry_key_is_the_pinned_one(self):
        self.assertEqual(vrs.registry_public_key(), "4020c46d76c8ebacb64c5c7b9f3e7c62475afeb133d7421591988174e0f5dc90")


if __name__ == "__main__":
    unittest.main()
