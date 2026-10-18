"""Tests for tools/publish_release_assets.py (release asset list and checks)."""
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("publish_release_assets", ROOT / "tools" / "publish_release_assets.py")
pra = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pra)


def make_component(fw: Path, stem: str, version: str, data: bytes, application: bool, kind: str = "production"):
    image = fw / f"{stem}_v{version}.bin"
    image.write_bytes(data)
    (fw / f"{stem}_v{version}.hex").write_text(":00000001FF\n")
    (fw / f"{stem}_v{version}.elf").write_bytes(b"elf")
    digest = hashlib.sha256(data).hexdigest()
    (fw / (image.name + ".sha256")).write_text(f"{digest}  {image.name}\n")
    manifest = {"version": version, "sha256": digest}
    if application:
        manifest["hmac_sha256"] = "ab" * 32
        manifest["signing_key_kind"] = kind
    (fw / (image.name + ".manifest.json")).write_text(json.dumps(manifest))
    return {"version_string": version, "files": {"bin": {"filename": image.name, "sha256": digest}}}


try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError:  # the signing half of the tests needs it
    Ed25519PrivateKey = None


@unittest.skipIf(Ed25519PrivateKey is None, "cryptography is not installed")
class PublishPlanTests(unittest.TestCase):
    def setUp(self):
        # a throwaway key stands in for the key of the releases, which is never read by the tests
        self.test_key = Ed25519PrivateKey.generate()
        sign = pra.release_sign
        self._saved = (sign.PUBLIC_KEY_HEX, sign._private_key)
        sign.PUBLIC_KEY_HEX = sign.public_hex_of(self.test_key)
        sign._private_key = lambda: self.test_key
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.fw = self.root / "firmware"
        self.fw.mkdir()
        (self.root / "urtc.project.json").write_text(json.dumps({"name": "URTC", "version": "1.2.3"}))

    def tearDown(self):
        pra.release_sign.PUBLIC_KEY_HEX, pra.release_sign._private_key = self._saved
        self.tmp.cleanup()

    def write_index(self, components):
        (self.fw / "firmware_manifest.json").write_text(json.dumps({"components": components}))

    def test_indexed_set_lists_bin_hex_sidecars_and_index(self):
        comp = {
            "main_bootloader": make_component(self.fw, "URTC_MAIN_BOOTLOADER", "1.0.1", b"boot", False),
            "main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True),
        }
        self.write_index(comp)
        plan = pra.build_plan(self.root, self.fw, False, "o/r", sign=True)
        self.assertEqual(plan["problems"], [])
        names = sorted(Path(p).name for p in plan["files"])
        self.assertIn("firmware_manifest.json", names)
        self.assertIn("URTC_MAIN_FIRMWARE_v1.2.3.bin.manifest.json", names)
        self.assertIn("URTC_MAIN_FIRMWARE_v1.2.3.hex", names)
        self.assertFalse(any(n.endswith(".elf") for n in names))
        self.assertEqual(plan["tag"], "v1.2.3")
        with_elf = pra.build_plan(self.root, self.fw, True, "o/r", sign=True)
        self.assertTrue(any(p.endswith(".elf") for p in with_elf["files"]))
        self.assertIn("gh release create v1.2.3 --repo o/r", pra.gh_command(plan))

    def test_tampered_image_and_missing_signature_are_reported(self):
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True)}
        self.write_index(comp)
        (self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin").write_bytes(b"changed")
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(any("does not match" in p for p in plan["problems"]))
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", False)}
        self.write_index(comp)
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(any("hmac_sha256" in p for p in plan["problems"]))

    def test_development_key_is_refused_as_official_and_allowed_as_prerelease(self):
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True, "dev")}
        self.write_index(comp)
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(any("production" in p for p in plan["problems"]))
        bench = pra.build_plan(self.root, self.fw, False, "o/r", allow_dev_key=True)
        self.assertEqual(bench["problems"], [])
        self.assertTrue(bench["prerelease"])
        self.assertIn("--prerelease", pra.gh_command(bench))
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True)}
        self.write_index(comp)
        official = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertFalse(official["prerelease"])
        self.assertNotIn("--prerelease", pra.gh_command(official))

    def test_missing_hex_is_reported(self):
        comp = {"main_bootloader": make_component(self.fw, "URTC_MAIN_BOOTLOADER", "1.0.1", b"boot", False)}
        self.write_index(comp)
        (self.fw / "URTC_MAIN_BOOTLOADER_v1.0.1.hex").unlink()
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(any("hex is missing" in p for p in plan["problems"]))

    def test_single_image_layout_writes_missing_sidecar_and_checks_version(self):
        (self.fw / "URTC_SMART_RACK_FIRMWARE_v1.2.3.bin").write_bytes(b"x")
        (self.fw / "URTC_SMART_RACK_FIRMWARE_v1.2.3.hex").write_text(":00000001FF\n")
        plan = pra.build_plan(self.root, self.fw, False, "o/r", sign=True)
        self.assertEqual(plan["problems"], [])
        self.assertTrue((self.fw / "URTC_SMART_RACK_FIRMWARE_v1.2.3.bin.sha256").is_file())
        self.assertEqual(len(plan["files"]), 6)  # bin, hex, sidecar and the three signatures
        (self.root / "urtc.project.json").write_text(json.dumps({"name": "URTC", "version": "9.9.9"}))
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(plan["problems"])

    def test_an_official_release_needs_every_signature_and_a_prerelease_does_not(self):
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True)}
        self.write_index(comp)
        unsigned = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(any(".sig" in p for p in unsigned["problems"]))
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True, "dev")}
        self.write_index(comp)
        bench = pra.build_plan(self.root, self.fw, False, "o/r", allow_dev_key=True)
        self.assertEqual(bench["problems"], [])
        self.assertFalse(any(p.endswith(".sig") for p in bench["files"]))

    def test_a_signature_that_does_not_verify_is_refused(self):
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True)}
        self.write_index(comp)
        pra.build_plan(self.root, self.fw, False, "o/r", sign=True)
        image = self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin"
        other = self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.hex"
        Path(str(image) + ".sig").write_text(Path(str(other) + ".sig").read_text())  # the signature of another file
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(any("not a valid release signature" in p for p in plan["problems"]))

    def test_the_release_key_is_never_taken_from_a_foreign_key(self):
        pra.release_sign.PUBLIC_KEY_HEX = "00" * 32
        with self.assertRaises(SystemExit):
            pra.release_sign.sign_file(self.fw / "x.bin", self.test_key)

    def test_missing_folder_is_a_problem(self):
        plan = pra.build_plan(self.root, self.root / "nowhere", False, "o/r")
        self.assertTrue(plan["problems"])


if __name__ == "__main__":
    unittest.main()
