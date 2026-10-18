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


class PublishPlanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.fw = self.root / "firmware"
        self.fw.mkdir()
        (self.root / "urtc.project.json").write_text(json.dumps({"name": "URTC", "version": "1.2.3"}))

    def tearDown(self):
        self.tmp.cleanup()

    def write_index(self, components):
        (self.fw / "firmware_manifest.json").write_text(json.dumps({"components": components}))

    def test_indexed_set_lists_bin_hex_sidecars_and_index(self):
        comp = {
            "main_bootloader": make_component(self.fw, "URTC_MAIN_BOOTLOADER", "1.0.1", b"boot", False),
            "main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True),
        }
        self.write_index(comp)
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertEqual(plan["problems"], [])
        names = sorted(Path(p).name for p in plan["files"])
        self.assertIn("firmware_manifest.json", names)
        self.assertIn("URTC_MAIN_FIRMWARE_v1.2.3.bin.manifest.json", names)
        self.assertIn("URTC_MAIN_FIRMWARE_v1.2.3.hex", names)
        self.assertFalse(any(n.endswith(".elf") for n in names))
        self.assertEqual(plan["tag"], "v1.2.3")
        with_elf = pra.build_plan(self.root, self.fw, True, "o/r")
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
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertEqual(plan["problems"], [])
        self.assertTrue((self.fw / "URTC_SMART_RACK_FIRMWARE_v1.2.3.bin.sha256").is_file())
        self.assertEqual(len(plan["files"]), 3)
        (self.root / "urtc.project.json").write_text(json.dumps({"name": "URTC", "version": "9.9.9"}))
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(plan["problems"])

    def test_missing_folder_is_a_problem(self):
        plan = pra.build_plan(self.root, self.root / "nowhere", False, "o/r")
        self.assertTrue(plan["problems"])


if __name__ == "__main__":
    unittest.main()
