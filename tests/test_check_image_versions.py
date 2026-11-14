"""tools/check_image_versions.py: the version inside an application image is the one its name and the manifest state."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import check_image_versions  # noqa: E402
from image_record import image_header  # noqa: E402

MAIN, SLAVE = 0x0303CC01, 0x0303CB01


def application(hardware: int, version: tuple[int, int, int, int]) -> bytes:
    blank = bytes(image_header.EMBED_OFFSET) + image_header.embedded_slot(hardware, version) + bytes(64)
    return image_header.seal_embedded(blank, hardware, version)


def write_build(folder: Path, main=(0, 4, 9, 0), slave=(0, 3, 3, 0), main_text="0.4.9", slave_text="0.3.3") -> None:
    parts = {
        "main_bootloader": ("URTC_MAIN_BOOTLOADER_v0.5.1.bin", "0.5.1", b"boot-main"),
        "main_application": (f"URTC_MAIN_FIRMWARE_v{main_text}.bin", main_text, application(MAIN, main)),
        "slave_bootloader": ("URTC_SLAVE_BOOTLOADER_v0.3.5.bin", "0.3.5", b"boot-slave"),
        "slave_application": (f"URTC_SLAVE_FIRMWARE_v{slave_text}.bin", slave_text, application(SLAVE, slave)),
    }
    components = {}
    for key, (name, text, data) in parts.items():
        (folder / name).write_bytes(data)
        (folder / (name + ".manifest.json")).write_text(json.dumps({"version": text}), encoding="utf-8")
        components[key] = {"version_string": text, "files": {"bin": {"filename": name}}}
    (folder / "firmware_manifest.json").write_text(json.dumps({"components": components}), encoding="utf-8")


class CheckImageVersions(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.folder = Path(self._tmp.name)

    def test_a_consistent_build_passes(self):
        write_build(self.folder)
        self.assertEqual(check_image_versions.check(self.folder, "0.4.9"), [])

    def test_the_main_version_must_be_the_one_asked_for(self):
        write_build(self.folder)
        self.assertTrue(any("expected 0.5.0" in p for p in check_image_versions.check(self.folder, "0.5.0")))

    def test_an_image_that_states_another_version_than_its_name_fails(self):
        write_build(self.folder, main=(0, 4, 8, 0))
        problems = check_image_versions.check(self.folder)
        self.assertTrue(any("states 0.4.8.0" in p for p in problems), problems)

    def test_an_unsealed_slot_fails(self):
        write_build(self.folder)
        path = self.folder / "URTC_SLAVE_FIRMWARE_v0.3.3.bin"
        path.write_bytes(bytes(image_header.EMBED_OFFSET) + image_header.embedded_slot(SLAVE, (0, 3, 3, 0)) + bytes(64))
        self.assertTrue(any("does not verify" in p for p in check_image_versions.check(self.folder)))

    def test_a_missing_component_fails(self):
        write_build(self.folder)
        manifest = json.loads((self.folder / "firmware_manifest.json").read_text(encoding="utf-8"))
        del manifest["components"]["slave_bootloader"]
        (self.folder / "firmware_manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        self.assertEqual(len(check_image_versions.check(self.folder)), 1)

    def test_a_sidecar_with_another_version_fails(self):
        write_build(self.folder)
        (self.folder / "URTC_MAIN_BOOTLOADER_v0.5.1.bin.manifest.json").write_text(json.dumps({"version": "0.5.0"}), encoding="utf-8")
        self.assertTrue(any("sidecar" in p or "manifest.json" in p for p in check_image_versions.check(self.folder)))


if __name__ == "__main__":
    unittest.main()
