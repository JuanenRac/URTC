"""The twin of the bootloader: the real C of the master's bootloader behind an SLCAN-style port, driven the way a flashing tool drives it."""
import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("bootloader_twin", ROOT / "tools" / "bootloader_twin.py")
twin = importlib.util.module_from_spec(_spec)
sys.modules["bootloader_twin"] = twin
_spec.loader.exec_module(twin)


class HelperTests(unittest.TestCase):
    def test_the_minor_field_carries_the_patch_when_it_is_known(self):
        self.assertEqual(twin.minor_field(5, 1), 0x8105)
        self.assertEqual(twin.minor_field(5, None), 5)
        self.assertEqual(twin.minor_field(5, 0x80), 5)

    def test_the_signature_is_the_hmac_of_the_image_with_the_development_key(self):
        image = twin.test_image(64)
        self.assertEqual(len(twin.sign(image)), 32)
        self.assertEqual(twin.sign(image), twin.sign(image))
        self.assertNotEqual(twin.sign(image), twin.sign(image, bytes(32)))

    def test_the_test_image_is_not_a_valid_application(self):
        image = twin.test_image(16)
        stack = int.from_bytes(image[:4], "little")
        self.assertFalse(0x20000000 <= stack < 0x2000A000 and stack % 4 == 0 and image[4] & 1)

    def test_the_frames_are_written_as_slcan_lines(self):
        self.assertEqual(twin.slcan(0x7F1, bytes.fromhex("00000BB8")), "t7F1400000BB8")
        self.assertEqual(twin.slcan(0x7F8, b""), "t7F80")

    def test_a_verify_failure_reason_is_found_among_the_statuses(self):
        statuses = [(twin.STATUS, bytes([twin.STATUS_VERIFYING])), (twin.STATUS, bytes([twin.STATUS_VERIFY_FAIL, twin.FAIL_CRC32]))]
        self.assertEqual(twin.verify_failure(statuses), twin.FAIL_CRC32)
        self.assertIsNone(twin.verify_failure(statuses[:1]))


class BuildOnlyTests(unittest.TestCase):
    def test_build_only_prints_the_command_that_runs_the_twin(self):
        original = twin.build
        twin.build = lambda binary: ["/somewhere/virtual_bootloader"]
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                status = twin.main(["--build-only", "--build-dir", tempfile.mkdtemp()])
        finally:
            twin.build = original
        self.assertEqual(status, 0)
        line = out.getvalue().strip().splitlines()[-1]
        self.assertEqual(json.loads(line.removeprefix("VIRTUAL_BOARD_COMMAND=")), ["/somewhere/virtual_bootloader"])

    def test_without_a_compiler_it_says_skipped(self):
        original = twin.build
        twin.build = lambda binary: None
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(twin.main(["--check", "--build-dir", tempfile.mkdtemp()]), 2)
        finally:
            twin.build = original


class RealBootloaderTests(unittest.TestCase):
    """Built once for the class: the run of the real bootloader takes a few seconds."""

    prefix = None

    @classmethod
    def setUpClass(cls):
        directory = tempfile.mkdtemp(dir=ROOT / "build" if (ROOT / "build").is_dir() else None)
        cls.prefix = twin.build(Path(directory) / "virtual_bootloader")

    def setUp(self):
        if self.prefix is None:
            self.skipTest("no host C compiler")

    def test_the_whole_exchange_passes(self):
        self.assertEqual(twin.run_checks(self.prefix), [])

    def test_a_wrong_crc_is_refused_and_the_main_slot_stays_empty(self):
        image = twin.test_image(1500)
        with twin.Twin(self.prefix) as board:
            board.take(twin.STATUS, 4.0)
            board.start_update(len(image))
            board.send_signature(twin.sign(image))
            board.send_data(image)
            reason = twin.verify_failure(board.end_update(twin.crc32(image) ^ 1, 0, 2))
            self.assertEqual(reason, twin.FAIL_CRC32)
            identity = board.installed_version()
            self.assertTrue(identity is None or identity[1:] == (0, 0))

    def test_a_missing_signature_chunk_is_incomplete(self):
        image = twin.test_image(1500)
        with twin.Twin(self.prefix) as board:
            board.take(twin.STATUS, 4.0)
            board.start_update(len(image))
            board.send_signature(twin.sign(image), chunks=3)
            board.send_data(image)
            self.assertEqual(twin.verify_failure(board.end_update(twin.crc32(image), 0, 2)), twin.FAIL_INCOMPLETE)

    def test_a_size_above_the_slot_is_refused_before_anything_is_erased(self):
        with twin.Twin(self.prefix) as board:
            board.take(twin.STATUS, 4.0)
            statuses = board.start_update(twin.APP_MAX_SIZE + 1)
            self.assertTrue(statuses and statuses[-1][1] == bytes([0xFF]))
            self.assertFalse(any(f[1][:1] == bytes([twin.STATUS_ERASING]) for f in statuses))

    def test_the_version_query_is_answered_while_listening(self):
        with twin.Twin(self.prefix) as board:
            board.take(twin.STATUS, 4.0)
            board.send(twin.QUERY_VERSION)
            frame = board.take(twin.VERSION_RESPONSE, 5.0)
            self.assertIsNotNone(frame)
            self.assertEqual(frame[1][0], 1)    # answered as the bootloader


if __name__ == "__main__":
    unittest.main()
