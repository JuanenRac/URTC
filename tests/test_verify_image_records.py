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


if __name__ == "__main__":
    unittest.main()
