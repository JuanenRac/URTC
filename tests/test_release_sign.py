"""Tests for tools/release_sign.py: the standard-library Ed25519 verifier and the release signature."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("release_sign", ROOT / "tools" / "release_sign.py")
rs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rs)

# RFC 8032, section 7.1, test 1 (empty message)
RFC_PUBLIC = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
RFC_SIGNATURE = bytes.fromhex(
    "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b")


class VerifierTests(unittest.TestCase):
    def test_the_rfc_vector_verifies_and_a_changed_message_or_signature_does_not(self):
        self.assertTrue(rs.ed25519_verify(RFC_PUBLIC, b"", RFC_SIGNATURE))
        self.assertFalse(rs.ed25519_verify(RFC_PUBLIC, b"x", RFC_SIGNATURE))
        broken = bytearray(RFC_SIGNATURE)
        broken[40] ^= 1
        self.assertFalse(rs.ed25519_verify(RFC_PUBLIC, b"", bytes(broken)))
        self.assertFalse(rs.ed25519_verify(RFC_PUBLIC, b"", RFC_SIGNATURE[:63]))
        self.assertFalse(rs.ed25519_verify(RFC_PUBLIC[:31], b"", RFC_SIGNATURE))

    def test_the_pinned_key_is_a_valid_point(self):
        self.assertEqual(len(bytes.fromhex(rs.PUBLIC_KEY_HEX)), 32)
        self.assertIsNotNone(rs._decompress(bytes.fromhex(rs.PUBLIC_KEY_HEX)))

    def test_the_message_binds_the_file_name_and_refuses_separators(self):
        self.assertEqual(rs.release_message("a.bin", "ab" * 32), b"urtc-rel/1|a.bin|" + b"ab" * 32)
        with self.assertRaises(ValueError):
            rs.release_message("a|b.bin", "00" * 32)

    def test_signatures_made_by_the_cryptography_package_verify(self):
        try:
            from cryptography.hazmat.primitives.asymmetric.ed25519 import (
                Ed25519PrivateKey,
            )
        except ImportError:
            self.skipTest("cryptography is not installed")
        key = Ed25519PrivateKey.generate()
        public = rs.public_hex_of(key)
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "URTC_MAIN_FIRMWARE_v1.0.0.bin"
            image.write_bytes(b"image")
            signature = key.sign(rs.release_message(image.name, rs.sha256_file(image))).hex()
            self.assertTrue(rs.verify_file(image, signature, public))
            self.assertFalse(rs.verify_file(image, signature, rs.PUBLIC_KEY_HEX))
            renamed = Path(tmp) / "URTC_MAIN_FIRMWARE_v1.0.1.bin"
            renamed.write_bytes(b"image")
            self.assertFalse(rs.verify_file(renamed, signature, public), "a signature is bound to the file name")
            image.write_bytes(b"changed")
            self.assertFalse(rs.verify_file(image, signature, public))
            self.assertFalse(rs.verify_file(image, "not hex", public))


if __name__ == "__main__":
    unittest.main()
