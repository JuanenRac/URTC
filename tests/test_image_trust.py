"""tools/image_trust.py: the public trust-table source is well formed, its table has the documented bytes, and a binary is checked against it."""
import copy
import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("urtc_image_trust", ROOT / "tools" / "image_trust.py")
trust = importlib.util.module_from_spec(_spec)
sys.modules["urtc_image_trust"] = trust
_spec.loader.exec_module(trust)

DOC = trust.load()


class TrustFileTests(unittest.TestCase):
    def test_the_file_is_valid_and_holds_no_private_material(self):
        trust.validate(DOC)
        raw = (ROOT / "keys" / "image_trust.json").read_text(encoding="utf-8")
        for word in ("BEGIN PRIVATE", "BEGIN OPENSSH", '"seed"'):
            self.assertNotIn(word, raw)

    def test_the_dev_table_has_the_documented_layout(self):
        data = trust.table_bytes(DOC, "dev")
        self.assertEqual(len(data), 56)
        self.assertEqual(data[:8], b"TKEY\x01\x01\x00\x00")
        self.assertEqual(data[8:16].hex(), DOC["flavours"]["dev"]["keys"][0]["key_id"])
        self.assertEqual(data[48], 1)

    def test_the_release_table_cannot_be_built_until_a_key_is_provisioned(self):
        if not DOC["flavours"]["release"]["keys"]:
            with self.assertRaises(trust.TrustFileError):
                trust.table_bytes(DOC, "release")

    def test_a_wrong_key_id_or_a_weaker_default_policy_is_refused(self):
        bad = copy.deepcopy(DOC)
        bad["flavours"]["dev"]["keys"][0]["key_id"] = "00" * 8
        with self.assertRaises(trust.TrustFileError):
            trust.validate(bad)
        bad = copy.deepcopy(DOC)
        bad["default_policy"] = "either"
        with self.assertRaises(trust.TrustFileError):
            trust.validate(bad)

    def test_the_binary_check_finds_the_table_at_the_offset_and_notices_any_change(self):
        offset = int(DOC["targets"]["master"]["table_offset"], 16)
        table = trust.table_bytes(DOC, "dev")
        binary = bytes(offset) + table + bytes(100)
        self.assertIsNone(trust.check_binary(DOC, binary, "master", "dev"))
        for index in range(len(table)):
            changed = bytearray(binary)
            changed[offset + index] ^= 1
            self.assertIsNotNone(trust.check_binary(DOC, bytes(changed), "master", "dev"), index)
        self.assertIsNotNone(trust.check_binary(DOC, bytes(offset + 200), "master", "dev"))

    def test_the_layout_agrees_with_the_shared_reference_when_it_is_next_to_this_repository(self):
        common = ROOT.parent / "URTC-COMMON" / "src"
        if not (common / "urtc_common" / "image_sig.py").is_file():
            self.skipTest("URTC-COMMON is not checked out next to this repository")
        sys.path.insert(0, str(common))
        from urtc_common import image_sig as ref  # noqa: PLC0415
        keys = [(bytes.fromhex(e["public"]), trust.STATES[e["state"]]) for e in DOC["flavours"]["dev"]["keys"]]
        self.assertEqual(trust.table_bytes(DOC, "dev"), ref.pack_table(keys))
        self.assertEqual(ref.public_of(ref.DEV_SEED).hex(), DOC["flavours"]["dev"]["keys"][0]["public"])


if __name__ == "__main__":
    unittest.main()
