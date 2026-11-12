"""tools/seal_image.py and the places that make it work: the slot the firmware emits, the linker section at the fixed offset and the call in the build scripts."""
import importlib.util
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
_spec = importlib.util.spec_from_file_location("urtc_seal_image", ROOT / "tools" / "seal_image.py")
seal_image = importlib.util.module_from_spec(_spec)
sys.modules["urtc_seal_image"] = seal_image
_spec.loader.exec_module(seal_image)
from image_record import image_header as ih  # noqa: E402

APPS = {
    "main": {"common": "src/F303-master/firmware_common.h", "slot": "src/F303-master/firmware_image_slot.c", "linker": "src/F303-master/STM32F303CCTx_APP.ld"},
    "slave": {"common": "src/F303-slave/slave_common.h", "slot": "src/F303-slave/slave_image_slot.c", "linker": "src/F303-slave/STM32F303CBTx_SLAVEAPP.ld"},
}


def defines(path):
    out = {}
    for name, value in re.findall(r"^#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)", (ROOT / path).read_text(encoding="utf-8"), flags=re.M):
        out[name] = int(value, 0)
    return out


def firmware(board, version, length=1024):
    """A stand-in for a linked application: a vector table, zeros up to the offset of the slot, the slot the firmware emits, then code."""
    hardware = seal_image.BOARDS[board][0]
    raw = bytearray(range(32)) * 12 + bytearray(length - 384)   # 384 bytes of "vector table" (the F303 table is 0x188), then code
    raw[0x188:0x200] = bytes(0x200 - 0x188)
    raw[ih.EMBED_OFFSET:ih.EMBED_OFFSET + 32] = ih.embedded_slot(hardware, version)
    return bytes(raw)


class SealTests(unittest.TestCase):
    def run_seal(self, board, name, data):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / name
            binary.write_bytes(data)
            hex_path = binary.with_suffix(".hex")
            hex_path.write_bytes(b"old\n")
            code = seal_image.main([board, str(binary)])
            return code, binary.read_bytes(), hex_path.read_bytes().decode("ascii")

    def test_a_main_image_is_sealed_in_place_and_its_hex_is_written_again(self):
        data = firmware("main", (0, 4, 7, 0))
        code, sealed, hex_text = self.run_seal("main", "URTC_MAIN_FIRMWARE_v0.4.7.bin", data)
        self.assertEqual(code, 0)
        self.assertEqual(len(sealed), len(data))
        self.assertEqual(sealed[:ih.EMBED_OFFSET], data[:ih.EMBED_OFFSET])      # the vector table has not moved
        self.assertEqual(sealed[ih.EMBED_OFFSET + 32:], data[ih.EMBED_OFFSET + 32:])
        status, info = ih.verify_embedded(sealed)
        self.assertEqual(status, ih.OK)
        self.assertEqual((info.hardware_id, info.version), (0x0303CC01, (0, 4, 7, 0)))
        self.assertTrue(hex_text.startswith(":020000040800F2\r\n") and hex_text.endswith(":00000001FF\r\n"))

    def test_a_slave_image_with_three_components_has_a_zero_build(self):
        code, sealed, _ = self.run_seal("slave", "URTC_SLAVE_FIRMWARE_v0.3.1.bin", firmware("slave", (0, 3, 1, 0)))
        self.assertEqual(code, 0)
        self.assertEqual(ih.verify_embedded(sealed)[1].version, (0, 3, 1, 0))

    def test_the_same_bytes_come_out_every_time(self):
        data = firmware("main", (0, 4, 7, 0))
        self.assertEqual(self.run_seal("main", "URTC_MAIN_FIRMWARE_v0.4.7.bin", data)[1], self.run_seal("main", "URTC_MAIN_FIRMWARE_v0.4.7.bin", data)[1])

    def test_nothing_is_written_for_another_board_another_version_or_a_binary_without_the_slot(self):
        main = firmware("main", (0, 4, 7, 0))
        for board, name, data in (("slave", "URTC_SLAVE_FIRMWARE_v0.4.7.bin", main), ("main", "URTC_MAIN_FIRMWARE_v0.4.8.bin", main),
                                  ("main", "URTC_MAIN_FIRMWARE_v0.4.7.bin", bytes(1024)), ("main", "firmware.bin", main)):
            code, after, hex_text = self.run_seal(board, name, data)
            self.assertEqual(code, 1, name)
            self.assertEqual(after, data, name)
            self.assertEqual(hex_text, "old\n", name)

    def test_intel_hex_records_have_a_checksum_and_cross_the_64k_boundary(self):
        text = seal_image.intel_hex(bytes(range(32)), 0x0800FFF0)
        records = [line for line in text.split("\r\n") if line]
        self.assertEqual(records[0], ":020000040800F2")
        self.assertEqual(records[2], ":020000040801F1")
        for line in records:
            raw = bytes.fromhex(line[1:])
            self.assertEqual(sum(raw) & 0xFF, 0, line)
        self.assertEqual(records[1][:9], ":10FFF000")


class FirmwareSideTests(unittest.TestCase):
    def test_each_slot_states_the_hardware_and_the_version_of_its_own_headers(self):
        for board, files in APPS.items():
            values = defines(files["common"])
            self.assertEqual(values["THIS_HARDWARE_ID"], seal_image.BOARDS[board][0], board)
            slot = (ROOT / files["slot"]).read_text(encoding="utf-8")
            self.assertIn('section(".urtc_image_header")', slot)
            self.assertIn("URTC_IMAGE_EMBED_SLOT(THIS_HARDWARE_ID, FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH,", slot)
            self.assertIn("FIRMWARE_VERSION_MINOR", values)

    def test_each_linker_script_puts_the_slot_at_the_fixed_offset_after_the_vector_table(self):
        for board, files in APPS.items():
            text = (ROOT / files["linker"]).read_text(encoding="utf-8")
            self.assertIn(f".urtc_image_header ORIGIN(FLASH) + 0x{ih.EMBED_OFFSET:X} :", text, board)
            self.assertIn("KEEP(*(.urtc_image_header))", text)
            self.assertIn("ASSERT(SIZEOF(.isr_vector) <= 0x200", text)
            self.assertIn("ASSERT(SIZEOF(.urtc_image_header) == 32", text)
            self.assertLess(text.index(".isr_vector"), text.index(".urtc_image_header ORIGIN"), "the table comes first")
            base = int(re.search(r"FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+)", text).group(1), 16)
            self.assertEqual(base, seal_image.BOARDS[board][1], board)

    def test_the_slot_header_is_the_shared_one_and_its_macro_gives_the_slot_of_the_reference(self):
        header = (ROOT / "src/common/urtc_image_header.h").read_text(encoding="utf-8")
        self.assertIn("URTC_IMAGE_EMBED_SLOT", header)
        self.assertIn(f"#define URTC_IMAGE_EMBED_OFFSET     0x{ih.EMBED_OFFSET:X}u", header)

    def test_both_build_scripts_seal_both_applications_after_the_binary_is_made(self):
        for script in ("build_firmware.sh", "build_firmware.bat"):
            text = (ROOT / script).read_text(encoding="utf-8")
            for board in ("main", "slave"):
                self.assertRegex(text, rf"seal_image\.py[\"\\]? {board} ", script)


if __name__ == "__main__":
    unittest.main()
