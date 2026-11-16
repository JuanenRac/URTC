"""tools/verify_image_records.py: a release set is accepted only when every image signature record verifies with no key, for the image next to it.

The keys here are throwaway: the seed of the real image-signing key is never read by a test.
"""
import copy
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import image_trust as trust  # noqa: E402
import verify_image_records as vir  # noqa: E402
from image_record import image_header as ih  # noqa: E402
from image_record import image_sig as sg  # noqa: E402

SEED = hashlib.sha256(b"verify_image_records test key").digest()
OTHER = hashlib.sha256(b"verify_image_records another key").digest()
MAIN_HW, SLAVE_HW = 0x0303CC01, 0x0303CB01
OFFSET = 0x200


def make_doc(min_counter=0):
    doc = copy.deepcopy(trust.load())
    public = sg.public_of(SEED)
    doc["flavours"]["release"]["keys"] = [{"state": "current", "public": public.hex(), "key_id": trust.key_id(public).hex()}]
    doc["min_counter"] = min_counter
    return doc


def bootloader_with(doc, flavour="release"):
    return bytes(OFFSET) + trust.bootloader_table(doc, flavour) + bytes(64)


class RecordsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.doc = make_doc()
        self.payload = bytes(range(256)) * 8

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, name="URTC_MAIN_FIRMWARE_v1.2.3.bin", payload=None, seed=SEED, hardware=MAIN_HW, version=(1, 2, 3, 0), counter=1):
        payload = self.payload if payload is None else payload
        (self.dir / name).write_bytes(payload)
        text = sg.sign_image(seed, vir.header28(hardware, version, payload), payload, counter)
        # exactly the bytes the signing tool writes: 152 digits and one line feed
        (self.dir / (name + ".imgsig")).write_bytes((text.strip() + "\n").encode("ascii"))
        return self.dir / name

    def problems(self, doc=None):
        return vir.check_folder(self.dir, "release", doc or self.doc)

    def test_a_good_set_of_the_two_application_images_passes(self):
        self.put()
        self.put("URTC_SLAVE_FIRMWARE_v0.3.1.bin", hardware=SLAVE_HW, version=(0, 3, 1, 0), counter=4)
        self.assertEqual(self.problems(), [])

    def test_a_fourth_version_component_in_the_name_is_part_of_what_is_signed(self):
        self.put("URTC_MAIN_FIRMWARE_v0.8.0.2.bin", version=(0, 8, 0, 2))
        self.assertEqual(self.problems(), [])
        self.put("URTC_MAIN_FIRMWARE_v0.8.0.3.bin", version=(0, 8, 0, 2))   # record made for another build number
        self.assertTrue(any("v0.8.0.3" in p for p in self.problems()))

    def test_one_changed_bit_of_the_image_breaks_the_record(self):
        image = self.put()
        data = bytearray(image.read_bytes())
        data[100] ^= 1
        image.write_bytes(bytes(data))
        self.assertTrue(any("bad_signature" in p for p in self.problems()), self.problems())

    def sealed(self, hardware=MAIN_HW, version=(1, 2, 3, 0)):
        raw = bytearray(range(256)) * 8
        raw[ih.EMBED_OFFSET:ih.EMBED_OFFSET + 32] = ih.embedded_slot(hardware, version)
        return ih.seal_embedded(bytes(raw))

    def test_an_image_with_its_header_inside_is_signed_and_verified_like_any_other(self):
        self.put(payload=self.sealed())   # the record is made over the flat header the bootloaders rebuild and the SHA-256 of the whole sealed file
        self.assertEqual(self.problems(), [])

    def test_a_sealed_image_that_states_another_version_than_its_name_is_refused(self):
        self.put(payload=self.sealed(version=(1, 2, 4, 0)))
        self.assertTrue(any("header inside the image" in p and "wrong_version" in p for p in self.problems()), self.problems())

    def test_a_sealed_image_for_another_board_is_refused(self):
        self.put(payload=self.sealed(hardware=SLAVE_HW))
        self.assertTrue(any("wrong_hardware" in p for p in self.problems()), self.problems())

    def test_a_damaged_header_inside_the_image_is_refused_before_the_record_is_looked_at(self):
        damaged = bytearray(self.sealed())
        damaged[ih.EMBED_OFFSET + 10] ^= 1
        self.put(payload=bytes(damaged))
        self.assertTrue(any("header_bad_header_crc" in p for p in self.problems()), self.problems())

    def test_a_record_made_for_the_other_board_is_refused(self):
        self.put(hardware=SLAVE_HW)   # signed as the expansion slave, published as the tool head
        self.assertTrue(any("does not verify" in p for p in self.problems()))

    def test_a_record_of_a_key_that_is_not_in_the_table_is_refused(self):
        self.put(seed=OTHER)
        self.assertTrue(any("unknown_key" in p for p in self.problems()), self.problems())

    def test_a_counter_below_the_minimum_of_the_bootloaders_is_refused(self):
        self.put(counter=4)
        self.assertEqual(self.problems(make_doc(min_counter=4)), [])
        self.assertTrue(any("rollback_counter" in p for p in self.problems(make_doc(min_counter=5))))

    def test_a_record_that_is_not_well_formed_is_refused(self):
        image = self.put()
        record = Path(str(image) + ".imgsig")
        record.write_bytes(record.read_bytes() + b"\n")
        self.assertTrue(any("bad_args" in p for p in self.problems()))

    def test_a_record_without_its_image_or_with_an_unknown_name_is_refused(self):
        (self.dir / "URTC_MAIN_FIRMWARE_v1.2.3.bin.imgsig").write_bytes(b"ab" * 76 + b"\n")
        self.assertTrue(any("is not in the folder" in p for p in self.problems()))
        (self.dir / "URTC_RACK_FIRMWARE_v1.0.0.bin.imgsig").write_bytes(b"ab" * 76 + b"\n")
        self.assertTrue(any("not the record of an application image" in p for p in self.problems()))

    def test_the_bootloader_of_the_set_must_hold_the_table_whose_keys_signed(self):
        self.put()
        (self.dir / "URTC_MAIN_BOOTLOADER_v0.4.6.bin").write_bytes(bootloader_with(self.doc))
        self.assertEqual(self.problems(), [])
        (self.dir / "URTC_MAIN_BOOTLOADER_v0.4.6.bin").write_bytes(bootloader_with(trust.load(), "dev"))
        self.assertTrue(any("keys that sign are not the keys it trusts" in p for p in self.problems()))
        (self.dir / "URTC_MAIN_BOOTLOADER_v0.4.6.bin").write_bytes(bytes(OFFSET + 200))
        self.assertTrue(any("no trust table" in p for p in self.problems()))

    def test_a_folder_without_records_has_nothing_to_check_here(self):
        (self.dir / "URTC_MAIN_BOOTLOADER_v0.4.6.bin").write_bytes(bytes(OFFSET + 200))
        self.assertEqual(self.problems(), [])

    def test_a_flavour_without_a_key_cannot_verify_anything(self):
        self.put()
        empty = copy.deepcopy(self.doc)
        empty["flavours"]["release"]["keys"] = []
        self.assertTrue(any("cannot be built" in p for p in self.problems(empty)))

    def test_the_record_does_not_verify_against_the_published_release_table(self):
        """A test record is made with a throwaway key, so against the real public table it must fail: the check really looks at the keys."""
        self.put()
        self.assertTrue(self.problems(trust.load()))

    def test_the_command_line_reports_pass_and_fail(self):
        self.put()
        self.assertEqual(vir.main([str(self.dir), "--flavour", "dev"]), 1)   # the throwaway key is not the development key either
        self.assertEqual(vir.main([str(self.dir / "missing")]), 1)

    def test_the_sets_are_checked_by_the_release_tool(self):
        import verify_release_set
        self.assertIs(verify_release_set.verify_image_records, vir)
        self.assertIn("records", verify_release_set.verify_set.__code__.co_varnames)
        self.assertTrue(json.loads((ROOT / "keys" / "image_trust.json").read_text(encoding="utf-8"))["flavours"]["release"]["keys"])


class CounterLedgerTests(unittest.TestCase):
    """The rollback counter of a release against the public ledger of the counters of the earlier ones."""

    LEDGER = {"counters": [{"image": "MAIN", "version": "1.2.2", "counter": 4}, {"image": "SLAVE", "version": "0.3.3", "counter": 9}]}

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.payload = bytes(range(256)) * 8

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, counter, kind="MAIN", version="1.2.3"):
        name = f"URTC_{kind}_FIRMWARE_v{version}.bin"
        hardware = MAIN_HW if kind == "MAIN" else SLAVE_HW
        numbers = tuple(int(part) for part in version.split("."))
        text = sg.sign_image(SEED, vir.header28(hardware, numbers + (0,) * (4 - len(numbers)), self.payload), self.payload, counter)
        (self.dir / (name + ".imgsig")).write_bytes((text.strip() + "\n").encode("ascii"))

    def test_a_counter_above_the_earlier_ones_passes(self):
        self.put(5)
        self.assertEqual(vir.check_counters(self.dir, self.LEDGER), [])

    def test_a_counter_that_does_not_rise_is_refused(self):
        for counter in (4, 3, 0):
            self.put(counter)
            problems = vir.check_counters(self.dir, self.LEDGER)
            self.assertTrue(any("not above 4" in p for p in problems), (counter, problems))

    def test_the_kinds_are_counted_apart(self):
        self.put(5, "SLAVE", "0.3.4")
        self.assertTrue(any("not above 9" in p for p in vir.check_counters(self.dir, self.LEDGER)))
        self.put(10, "SLAVE", "0.3.4")
        self.assertEqual(vir.check_counters(self.dir, self.LEDGER), [])

    def test_a_version_that_is_in_the_ledger_must_repeat_its_counter(self):
        self.put(4, version="1.2.2")
        self.assertEqual(vir.check_counters(self.dir, self.LEDGER), [])
        self.put(7, version="1.2.2")
        self.assertTrue(any("ledger holds for MAIN 1.2.2" in p for p in vir.check_counters(self.dir, self.LEDGER)))

    def test_a_missing_ledger_is_a_problem_and_an_empty_one_is_not(self):
        self.put(1)
        self.assertTrue(vir.check_counters(self.dir, None))
        self.assertEqual(vir.check_counters(self.dir, {"counters": []}), [])

    def test_the_ledger_of_the_repository_is_well_formed_and_consistent(self):
        ledger = json.loads((ROOT / "keys" / "counter_ledger.json").read_text(encoding="utf-8"))
        seen = set()
        for item in ledger["counters"]:
            self.assertIn(item["image"], ("MAIN", "SLAVE"))
            self.assertIsInstance(item["counter"], int)
            self.assertNotIn((item["image"], item["version"]), seen)
            seen.add((item["image"], item["version"]))
        for kind in ("MAIN", "SLAVE"):
            ordered = [item["counter"] for item in sorted((i for i in ledger["counters"] if i["image"] == kind), key=lambda i: tuple(int(p) for p in i["version"].split(".")))]
            self.assertEqual(ordered, sorted(ordered), f"the counters of the {kind} images rise with the version")


if __name__ == "__main__":
    unittest.main()
