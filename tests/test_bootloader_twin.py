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

    def test_a_signed_case_is_a_76_byte_record_and_an_unknown_one_is_an_error(self):
        record = twin.signed_record(3000, 0, twin.minor_field(5, 1), 5)
        self.assertEqual(len(record), 76)
        self.assertEqual(int.from_bytes(record[8:12], "big"), 5)
        self.assertNotEqual(record, twin.signed_record(3000, 0, twin.minor_field(5, 1), 5, key="other"))
        with self.assertRaises(KeyError):
            twin.signed_record(3001, 0, 0, 0)

    def test_the_record_travels_in_eleven_frames_of_an_index_and_seven_bytes(self):
        sent = []

        class Recorder(twin.Twin):
            def __init__(self):
                pass

            def send(self, can_id, data=b""):
                sent.append((can_id, data))

        record = bytes(range(76))
        Recorder().send_signature(record)
        self.assertEqual([i for i, _ in sent], [twin.SIG_CHUNK] * 11)
        self.assertTrue(all(len(d) == 8 and d[0] == n for n, (_, d) in enumerate(sent)))
        self.assertEqual(b"".join(d[1:] for _, d in sent)[:76], record)
        self.assertEqual(sent[10][1][7], 0)      # the pad byte

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


def sealed_case() -> dict:
    """The image the build leaves after the seal step, with the record that signs it (tools/make_boot_sig_cases.py writes it with a throwaway key)."""
    return json.loads(twin.CASES_FILE.read_text(encoding="utf-8"))["sealed"][0]


class SealedImageCaseTests(unittest.TestCase):
    """The sealed image the bootloader is handed is what the seal step makes, and its signature record covers those very bytes (no compiler needed)."""

    def test_the_case_is_the_image_of_the_seal_step_with_a_header_that_verifies(self):
        sys.path.insert(0, str(ROOT / "tools"))
        from image_record import image_header as ih
        case = sealed_case()
        image = bytes.fromhex(case["image"])
        self.assertEqual(len(image), case["size"])
        self.assertEqual(twin.crc32(image), case["crc32"])
        status, info = ih.verify_embedded(image)
        self.assertEqual(status, ih.OK)
        self.assertEqual((info.hardware_id, info.version), (twin.HARDWARE_ID, (0, 5, 1, 0)))
        self.assertNotEqual(image[ih.EMBED_OFFSET + 8:ih.EMBED_OFFSET + 32], bytes(24))      # the length and the CRC-32 were filled in
        self.assertEqual(len(case["record"]), 152)

    def test_the_signature_record_is_found_by_its_parameters(self):
        case = sealed_case()
        self.assertEqual(case["minor_field"], twin.minor_field(5, 1))
        self.assertEqual(len(bytes.fromhex(case["record"])), 76)


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
            self.skipTest("no host C compiler, or local builds are off (ALLOW_LOCAL_BUILD=1 allows them)")

    def test_the_whole_exchange_passes(self):
        self.assertEqual(twin.run_checks(self.prefix), [])

    def test_a_wrong_crc_is_refused_and_the_main_slot_stays_empty(self):
        image = twin.test_image(1500)
        with twin.Twin(self.prefix) as board:
            board.take(twin.STATUS, 4.0)
            board.start_update(len(image))
            board.send_signature(twin.signed_record(1500, 0, twin.minor_field(2, 0), 1))
            board.send_data(image)
            reason = twin.verify_failure(board.end_update(twin.crc32(image) ^ 1, 0, 2, 0))
            self.assertEqual(reason, twin.FAIL_CRC32)
            identity = board.installed_version()
            self.assertTrue(identity is None or identity[1:] == (0, 0))

    def test_a_sealed_image_installs_with_its_signature_and_a_changed_byte_is_refused(self):
        case = sealed_case()
        image, record = bytes.fromhex(case["image"]), bytes.fromhex(case["record"])
        with twin.Twin(self.prefix) as board:
            board.take(twin.STATUS, 4.0)
            board.start_update(len(image))
            board.send_signature(record)
            board.send_data(image)
            statuses = board.end_update(case["crc32"], case["major"], 5, 1)
            self.assertEqual([f[1][0] for f in statuses if f[1]][-1:], [twin.STATUS_VERIFY_OK])
            self.assertEqual(board.event(5.0), "X")
            identity = None
            for _ in range(3):
                identity = board.installed_version()
                if identity:
                    break
            self.assertEqual(identity, (twin.HARDWARE_ID, 0, 5))
        changed = bytearray(image)
        changed[0x210] ^= 1       # inside the header the seal step filled in
        with twin.Twin(self.prefix) as board:
            board.take(twin.STATUS, 4.0)
            board.start_update(len(changed))
            board.send_signature(record)
            board.send_data(bytes(changed))
            self.assertEqual(twin.verify_failure(board.end_update(case["crc32"], case["major"], 5, 1)), twin.FAIL_CRC32)

    def test_a_missing_signature_chunk_is_incomplete(self):
        image = twin.test_image(1500)
        with twin.Twin(self.prefix) as board:
            board.take(twin.STATUS, 4.0)
            board.start_update(len(image))
            board.send_signature(twin.signed_record(1500, 0, twin.minor_field(2, 0), 1), chunks=10)
            board.send_data(image)
            self.assertEqual(twin.verify_failure(board.end_update(twin.crc32(image), 0, 2, 0)), twin.FAIL_INCOMPLETE)

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
