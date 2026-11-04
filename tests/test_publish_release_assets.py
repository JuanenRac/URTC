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


def make_component(fw: Path, stem: str, version: str, data: bytes, application: bool, kind: str = "production", record: bool = True):
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
        if record:  # the image signature record the signing tool writes next to an application image: 76 bytes as 152 hexadecimal digits
            (fw / (image.name + ".imgsig")).write_bytes(b"ab" * 76 + b"\n")
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

    def test_the_image_signature_record_goes_in_the_set_with_its_own_release_signature(self):
        self.write_index({"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True)})
        plan = pra.build_plan(self.root, self.fw, False, "o/r", sign=True)
        self.assertEqual(plan["problems"], [])
        names = sorted(Path(p).name for p in plan["files"])
        self.assertIn("URTC_MAIN_FIRMWARE_v1.2.3.bin.imgsig", names)
        self.assertIn("URTC_MAIN_FIRMWARE_v1.2.3.bin.imgsig.sig", names)

    def test_an_application_image_needs_a_record_or_an_hmac_but_not_both(self):
        component = make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True, "release")
        self.write_index({"main_application": component})
        manifest_path = self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin.manifest.json"
        manifest = json.loads(manifest_path.read_text())
        del manifest["hmac_sha256"]  # what a build of the Ed25519 generation publishes
        manifest_path.write_text(json.dumps(manifest))
        self.assertEqual(pra.build_plan(self.root, self.fw, False, "o/r", sign=True)["problems"], [])
        (self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin.imgsig").unlink()
        plan = pra.build_plan(self.root, self.fw, False, "o/r", sign=True)
        self.assertTrue(any("neither a signature record" in p for p in plan["problems"]), plan["problems"])

    def test_a_bootloader_carries_no_image_signature_record(self):
        self.write_index({"main_bootloader": make_component(self.fw, "URTC_MAIN_BOOTLOADER", "1.0.1", b"boot", False)})
        plan = pra.build_plan(self.root, self.fw, False, "o/r", sign=True)
        self.assertEqual(plan["problems"], [])
        self.assertFalse(any(Path(p).name.endswith(".imgsig") for p in plan["files"]))

    def test_an_official_release_without_the_image_signature_record_is_refused_and_a_bench_one_is_not(self):
        self.write_index({"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True, record=False)})
        plan = pra.build_plan(self.root, self.fw, False, "o/r", sign=False)
        self.assertTrue(any(".imgsig is missing" in p for p in plan["problems"]), plan["problems"])
        self.write_index({"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True, "dev", record=False)})
        bench = pra.build_plan(self.root, self.fw, False, "o/r", allow_dev_key=True)
        self.assertEqual(bench["problems"], [])
        self.assertTrue(bench["prerelease"])

    def test_a_file_that_is_not_an_image_signature_record_is_refused(self):
        self.write_index({"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True)})
        record = self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin.imgsig"
        for text in ("ab" * 75 + "\n", "ab" * 76 + "\n\n", "zz" * 76 + "\n", "ab" * 76 + "\r\n", ""):
            record.write_bytes(text.encode("ascii"))
            plan = pra.build_plan(self.root, self.fw, False, "o/r", sign=True)
            self.assertTrue(any("not an image signature record" in p for p in plan["problems"]), repr(text))

    def test_single_image_layout_lists_the_record(self):
        image = self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin"
        image.write_bytes(b"app")
        (self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.hex").write_text(":00000001FF\n")
        (self.fw / (image.name + ".imgsig")).write_bytes(b"cd" * 76 + b"\n")
        plan = pra.build_plan(self.root, self.fw, False, "o/r", sign=True)
        self.assertEqual(plan["problems"], [])
        self.assertIn("URTC_MAIN_FIRMWARE_v1.2.3.bin.imgsig", [Path(p).name for p in plan["files"]])

    def test_tampered_image_and_missing_signature_are_reported(self):
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True)}
        self.write_index(comp)
        (self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin").write_bytes(b"changed")
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(any("does not match" in p for p in plan["problems"]))
        (self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin.imgsig").unlink()
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", False)}
        self.write_index(comp)
        plan = pra.build_plan(self.root, self.fw, False, "o/r")
        self.assertTrue(any("hmac_sha256" in p for p in plan["problems"]))

    def test_the_key_kind_a_real_build_writes_counts_as_a_production_key(self):
        # build_firmware.sh with URTC_SIGNING_KEY_FILE records "release"; the older name was "production"; only "dev" (or anything else) is refused
        for kind, official in (("release", True), ("production", True), ("dev", False), ("other", False)):
            comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True, kind)}
            self.write_index(comp)
            plan = pra.build_plan(self.root, self.fw, False, "o/r", sign=True)
            self.assertEqual(plan["problems"] == [], official, kind)
            self.assertEqual(plan["prerelease"], not official, kind)

    def test_the_list_and_tag_files_are_written_only_when_nothing_is_wrong(self):
        import contextlib
        import io
        from unittest import mock

        root_patch = mock.patch.object(pra, "ROOT", self.root)
        root_patch.start()
        self.addCleanup(root_patch.stop)
        comp = {"main_application": make_component(self.fw, "URTC_MAIN_FIRMWARE", "1.2.3", b"app", True, "release")}
        self.write_index(comp)
        listing, tagfile = self.root / "assets.txt", self.root / "tag.txt"
        with contextlib.redirect_stdout(io.StringIO()):
            status = pra.main(["--firmware-dir", str(self.fw), "--repo", "o/r", "--sign", "--list-file", str(listing), "--tag-file", str(tagfile)])
        self.assertEqual(status, 0)
        names = listing.read_text(encoding="utf-8").split()
        self.assertTrue(any(n.endswith("URTC_MAIN_FIRMWARE_v1.2.3.bin") for n in names))
        self.assertTrue(any(n.endswith("URTC_MAIN_FIRMWARE_v1.2.3.bin.sig") for n in names))
        self.assertEqual(tagfile.read_text(encoding="utf-8").split(), ["v1.2.3", "official"])
        listing.unlink()
        tagfile.unlink()
        (self.fw / "URTC_MAIN_FIRMWARE_v1.2.3.bin").write_bytes(b"changed")
        with contextlib.redirect_stdout(io.StringIO()):
            status = pra.main(["--firmware-dir", str(self.fw), "--repo", "o/r", "--list-file", str(listing), "--tag-file", str(tagfile)])
        self.assertEqual(status, 1)
        self.assertFalse(listing.exists() or tagfile.exists())

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
