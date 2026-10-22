"""tools/keys.py: the registry is well formed, every embedding in this repository matches it, and a different key is noticed."""
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("urtc_keys", ROOT / "tools" / "keys.py")
keys = importlib.util.module_from_spec(_spec)
sys.modules["urtc_keys"] = keys
_spec.loader.exec_module(keys)

sys.path.insert(0, str(ROOT / "tools"))
REG = keys.load_registry()
RELEASE = REG["keys"][0]


class RegistryTests(unittest.TestCase):
    def test_every_key_has_an_id_a_role_and_a_place(self):
        ids = [k["id"] for k in REG["keys"]]
        self.assertEqual(len(ids), len(set(ids)))
        for k in REG["keys"]:
            self.assertTrue(k["role"] and k["private_half"] and k["embedded_in"], k["id"])

    def test_the_document_names_every_registered_key(self):
        text = (ROOT / "docs" / "KEYS.md").read_text(encoding="utf-8")
        for k in REG["keys"]:
            self.assertIn(k["public"], text, k["id"])
            self.assertIn(k["id"], text)

    def test_the_registry_holds_no_private_material(self):
        raw = (ROOT / "keys" / "registry.json").read_text(encoding="utf-8")
        self.assertNotIn("BEGIN PRIVATE", raw)
        self.assertNotIn("BEGIN OPENSSH", raw)

    def test_the_pinned_release_key_of_the_signing_tool_is_the_registered_one(self):
        import release_sign  # noqa: PLC0415
        self.assertEqual(release_sign.PUBLIC_KEY_HEX, RELEASE["public"])


class VerifyTests(unittest.TestCase):
    def test_this_repository_matches(self):
        lines, problems = keys.verify(ROOT.parent, REG, strict=False)
        self.assertEqual(problems, [])
        self.assertTrue(any(line.startswith("OK") and "URTC/tools/release_sign.py" in line for line in lines))

    def test_the_development_keys_are_the_ones_the_signing_module_reads(self):
        sys.path.insert(0, str(ROOT))
        try:
            import firmware_signing  # noqa: PLC0415
        finally:
            sys.path.pop(0)
        by_id = {k["id"]: k["public"] for k in REG["keys"]}
        self.assertEqual(firmware_signing.dev_key("master").hex(), by_id["urtc-main-bootloader-hmac-development"])
        self.assertEqual(firmware_signing.dev_key("slave").hex(), by_id["urtc-slave-bootloader-hmac-development"])

    def _tree(self, directory: Path, value: str) -> None:
        for loc in RELEASE["embedded_in"]:
            path = directory / loc["repo"] / loc["file"]
            path.parent.mkdir(parents=True, exist_ok=True)
            sample = {
                "tools/release_sign.py": f'PUBLIC_KEY_HEX = "{value}"\n',
                "docs/RELEASE_SIGNING.md": f"```\ned25519 {value}\n```\n".replace("ed25519 ", "ed25519 "),
                "flasher_release_signature.py": f'PUBLIC_KEY_HEX = "{value}"\n',
                "scripts/sync-firmware.mjs": f"export const RELEASE_PUBLIC_KEY_HEX = '{value}';\n",
                "src/lib/releaseSignature.ts": f"export const RELEASE_PUBLIC_KEY_HEX = '{value}';\n",
            }[loc["file"]]
            path.write_text(sample, encoding="utf-8")

    def test_a_different_key_in_one_program_is_reported(self):
        only = {"keys": [RELEASE]}
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            self._tree(root, RELEASE["public"])
            (root / "URTC-FLASHER" / "flasher_release_signature.py").write_text('PUBLIC_KEY_HEX = "' + "ab" * 32 + '"\n', encoding="utf-8")
            lines, problems = keys.verify(root, only)
        self.assertEqual(len(problems), 1)
        self.assertIn("URTC-FLASHER", problems[0])
        self.assertTrue(any(line.startswith("DIFFERENT") for line in lines))
        self.assertTrue(sum(line.startswith("OK") for line in lines) == 4)

    def test_a_file_without_the_key_is_reported_and_a_missing_repository_depends_on_strict(self):
        only = {"keys": [RELEASE]}
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            self._tree(root, RELEASE["public"])
            (root / "URTC-WEB-STUDIO" / "src" / "lib" / "releaseSignature.ts").write_text("// nothing\n", encoding="utf-8")
            _, problems = keys.verify(root, only)
            self.assertEqual(len(problems), 1)
            self.assertIn("no key found", problems[0])
        with tempfile.TemporaryDirectory() as d:
            lines, problems = keys.verify(Path(d), only, strict=False)
            # the programs of other repositories are skipped; the files of this repository are still read
            self.assertTrue(all(line.startswith("SKIPPED") for line in lines if "URTC-FLASHER" in line or "URTC-WEB-STUDIO" in line))
            self.assertEqual(problems, [])
            lines, problems = keys.verify(Path(d), only, strict=True)
            self.assertTrue(problems)

    def test_the_c_array_reader_needs_the_right_number_of_bytes(self):
        loc = {"kind": "c_array", "array": "HMAC_KEY"}
        good = "#if X\n#else\nconst uint8_t HMAC_KEY[4] = { 0x01, 0x02, 0x03, 0x0A };\n#endif\n"
        self.assertEqual(keys.extract(loc, good), "0102030a")
        self.assertIsNone(keys.extract(loc, good.replace("[4]", "[5]")))


class CommandTests(unittest.TestCase):
    def test_list_and_verify_run(self):
        self.assertEqual(keys.main(["list"]), 0)
        self.assertEqual(keys.main(["verify"]), 0)


if __name__ == "__main__":
    unittest.main()
