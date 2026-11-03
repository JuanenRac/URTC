"""firmware_signing.py: the main bootloader holds no key; the build only picks which public trust table it is compiled with."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import firmware_signing  # noqa: E402


class FlavourTests(unittest.TestCase):
    def test_the_bench_build_compiles_the_development_table(self):
        self.assertEqual(firmware_signing.trust_flavour({}), "dev")
        self.assertEqual(firmware_signing.cflags("master", {}), "-DURTC_TRUST_DEV")

    def test_an_official_build_compiles_the_release_table_and_needs_no_key_for_it(self):
        env = {"URTC_RELEASE": "1"}
        self.assertEqual(firmware_signing.trust_flavour(env), "release")
        self.assertEqual(firmware_signing.cflags("master", env), "")

    def test_the_development_hmac_key_of_the_main_board_is_only_for_the_earlier_generation(self):
        self.assertEqual(len(firmware_signing.dev_key("master")), 32)
        source = (ROOT / "src" / "F303-master" / "boot" / "bootloader_crypto.c").read_text(encoding="utf-8")
        self.assertNotIn("HMAC_KEY", source)
        self.assertNotIn(firmware_signing.MASTER_DEV_HMAC_KEY_HEX[:16], source.lower())


if __name__ == "__main__":
    unittest.main()
