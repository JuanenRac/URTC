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


class NoSlaveKeyTests(unittest.TestCase):
    def test_the_slave_bootloader_has_no_key_and_no_hmac_routine(self):
        for name in ("slaveboot_crypto.c", "slaveboot_crypto.h", "slaveboot_common.h", "slaveboot_protocol.c"):
            source = (ROOT / "src" / "F303-slave" / "boot" / name).read_text(encoding="utf-8")
            self.assertNotIn("HMAC_KEY", source, name)
            self.assertNotIn("hmac_sha256_flash_region", source, name)
            self.assertNotIn("URTC_SLAVE_HMAC_KEY_BYTES", source, name)

    def test_the_build_asks_for_no_slave_compiler_flag(self):
        with self.assertRaises(firmware_signing.SigningKeyError):
            firmware_signing.cflags("slave", {})
        for script in ("build_firmware.sh", "build_firmware.bat"):
            self.assertNotIn("SLAVE_KEY_FLAGS", (ROOT / script).read_text(encoding="utf-8"), script)

    def test_an_old_key_file_with_a_slave_line_is_still_read_and_the_line_ignored(self):
        text = "master=" + "ab" * 32 + chr(10) + "slave=" + "cd" * 32 + chr(10)
        keys = firmware_signing.parse_key_file(text)
        self.assertEqual(list(keys), ["master"])

    def test_the_slave_application_never_gets_an_hmac_whatever_the_flavour(self):
        for env in ({}, {"URTC_RELEASE": "1"}):
            block = firmware_signing.application_signing("slave", "nowhere.bin", env)
            self.assertEqual(block["algorithm"], "none")
            self.assertNotIn("hmac_sha256", block)
            self.assertEqual(block["key_kind"], firmware_signing.trust_flavour(env))

    def test_a_release_build_passes_without_any_key_file_and_publishes_no_hmac(self):
        block = firmware_signing.application_signing("master", "nowhere.bin", {"URTC_RELEASE": "1"})
        self.assertEqual(block, {"algorithm": "none", "key_kind": "release"})

    def test_a_bench_build_keeps_the_legacy_hmac_of_the_main_application(self):
        import tempfile  # noqa: PLC0415
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "image.bin"
            path.write_bytes(b"abc")
            block = firmware_signing.application_signing("master", str(path), {})
        self.assertEqual(block["algorithm"], "HMAC-SHA256")
        self.assertEqual(block["key_kind"], "dev")
        self.assertEqual(len(block["hmac_sha256"]), 64)


if __name__ == "__main__":
    unittest.main()
