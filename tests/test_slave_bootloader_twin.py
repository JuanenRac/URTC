"""The model of the expansion slave's bootloader (tools/slave_bootloader_twin.py): its scenarios, and the checks that hold it to the C it was written from.

A model that drifts from the source is worse than none, so the numbers it repeats (layout, registers, status codes, versions, timeouts) are read back from the C
files here, and so are the four properties of the code as built that the model reproduces: when one of those lines changes, the model has to change with it.
"""
import importlib.util
import re
import struct
import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("slave_bootloader_twin", ROOT / "tools" / "slave_bootloader_twin.py")
twin = importlib.util.module_from_spec(_spec)
sys.modules["slave_bootloader_twin"] = twin
_spec.loader.exec_module(twin)

BOOT = ROOT / "src" / "F303-slave" / "boot"
COMMON = ROOT / "src" / "common"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def defines(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for name, expression in re.findall(r"#define[ \t]+([A-Z0-9_]+)[ \t]+\(?([0-9A-Fa-fxuUlL*() ]+?)\)?[ \t]*(?:/[/*].*)?$", text(path), re.M):
        try:
            values[name] = eval(re.sub(r"(?<=[0-9A-Fa-f])[uUlL]+\b", "", expression), {"__builtins__": {}})   # noqa: S307 - digits and * from our own headers
        except Exception:    # a macro that is not a plain number
            continue
    return values


class SourceTests(unittest.TestCase):
    def test_the_layout_is_the_one_of_the_header(self):
        layout = defines(COMMON / "urtc_flash_layout.h")
        self.assertEqual(twin.BOOTLOADER_SIZE, layout["URTC_SLAVE_BOOTLOADER_SIZE"])
        self.assertEqual(twin.METADATA_ADDR, layout["URTC_SLAVE_METADATA_ADDR"])
        self.assertEqual(twin.MAIN_APP_ADDR, layout["URTC_SLAVE_MAIN_APP_ADDR"])
        self.assertEqual(twin.BACKUP_APP_ADDR, layout["URTC_SLAVE_BACKUP_APP_ADDR"])
        self.assertEqual(twin.APP_MAX_SIZE, layout["URTC_SLAVE_APP_MAX_SIZE"])
        self.assertEqual(twin.BACKUP_APP_ADDR + twin.APP_MAX_SIZE, twin.FLASH_BASE + twin.FLASH_SIZE)      # the backup slot ends where the 128 KB of the chip end

    def test_the_registers_status_codes_and_reasons_are_the_ones_of_the_specification(self):
        ids = defines(COMMON / "urtc_can_ids.h")
        pairs = {
            "URTC_SLAVE_BOOT_REG_STATUS": twin.REG_STATUS, "URTC_SLAVE_BOOT_REG_START_UPDATE": twin.REG_START_UPDATE,
            "URTC_SLAVE_BOOT_REG_DIGEST_EXPECTED": twin.REG_DIGEST_EXPECTED, "URTC_SLAVE_BOOT_REG_DATA": twin.REG_DATA,
            "URTC_SLAVE_BOOT_REG_END_UPDATE": twin.REG_END_UPDATE, "URTC_SLAVE_BOOT_REG_PROGRESS": twin.REG_PROGRESS,
            "URTC_SLAVE_BOOT_REG_QUERY_VERSION": twin.REG_QUERY_VERSION, "URTC_SLAVE_BOOT_REG_VERIFY_FAIL_REASON": twin.REG_VERIFY_FAIL_REASON,
            "URTC_SLAVE_BOOT_REG_QUERY_VERSION_EXT": twin.REG_QUERY_VERSION_EXT, "URTC_SLAVE_BOOT_REG_CHALLENGE": twin.REG_CHALLENGE,
            "URTC_SLAVE_BOOT_REG_CONFIRM": twin.REG_CONFIRM, "URTC_SLAVE_BOOT_REG_COUNTER_FLOOR": twin.REG_COUNTER_FLOOR,
            "URTC_SLAVE_BOOT_REG_RECORD_COUNTER": twin.REG_RECORD_COUNTER, "URTC_SLAVE_VERSION_EXT_MARKER": twin.VERSION_EXT_MARKER,
            "URTC_HARDWARE_ID_SLAVE": twin.HARDWARE_ID,
            "URTC_STATUS_LISTENING": twin.STATUS_LISTENING, "URTC_STATUS_ERASING": twin.STATUS_ERASING, "URTC_STATUS_RECEIVING": twin.STATUS_RECEIVING,
            "URTC_STATUS_VERIFYING": twin.STATUS_VERIFYING, "URTC_STATUS_COPYING": twin.STATUS_COPYING, "URTC_STATUS_VERIFY_OK": twin.STATUS_VERIFY_OK,
            "URTC_STATUS_VERIFY_FAIL": twin.STATUS_VERIFY_FAIL, "URTC_STATUS_ERROR": twin.STATUS_ERROR, "URTC_SLAVE_STATUS_AWAIT_CONFIRM": twin.STATUS_AWAIT_CONFIRM,
            "URTC_VERIFY_FAIL_REASON_INCOMPLETE": twin.FAIL_INCOMPLETE, "URTC_VERIFY_FAIL_REASON_CRC32": twin.FAIL_CRC32,
            "URTC_VERIFY_FAIL_REASON_HMAC": twin.FAIL_DIGEST, "URTC_VERIFY_FAIL_REASON_HARDWARE_ID": twin.FAIL_HARDWARE_ID,
            "URTC_VERIFY_FAIL_REASON_ROLLBACK": twin.FAIL_ROLLBACK, "URTC_SLAVE_VERIFY_FAIL_REASON_NOT_CONFIRMED": twin.FAIL_NOT_CONFIRMED,
        }
        for name, value in pairs.items():
            self.assertEqual(ids[name], value, name)

    def test_the_versions_the_metadata_and_the_confirmation_are_the_ones_of_the_source(self):
        common = text(BOOT / "slaveboot_common.h")
        version = tuple(int(re.search(rf"#define BOOTLOADER_VERSION_{part}\s+(\d+)", common).group(1)) for part in ("MAJOR", "MINOR", "PATCH"))
        self.assertEqual(twin.BOOTLOADER_VERSION, version)
        self.assertEqual(twin.META_RECORD_SIZE, int(re.search(r"#define META_RECORD_SIZE\s+(\d+)u", common).group(1)))
        self.assertEqual(twin.METADATA_MAGIC, int(re.search(r"#define METADATA_MAGIC_VALID\s+(0x[0-9A-Fa-f]+)UL", common).group(1), 16))
        self.assertEqual(twin.COUNTER_FLOOR_COMPILED, int(re.search(r"#define URTC_SLAVE_COUNTER_FLOOR\s+(\d+)UL", common).group(1)))
        self.assertEqual(len(twin.Meta().pack()) + 8, twin.META_RECORD_SIZE)
        confirm = text(COMMON / "urtc_slave_confirm.h")
        self.assertIn("'U', 'R', 'T', 'C', '-', 'S', 'L', 'V', '-', 'C', 'O', 'N', 'F', '-', 'V', '2'", confirm)
        self.assertEqual(int(re.search(r"URTC_SLAVE_CONFIRM_MESSAGE_SIZE\s+(\d+)u", confirm).group(1)), len(twin.confirm_message(bytes(16), bytes(32), 1, 2, 3, 4, 5)))

    def test_the_timing_and_the_buffer_are_the_ones_of_the_main_file(self):
        main = text(BOOT / "slaveboot_main.c")
        self.assertIn(f"> {twin.IDLE_ABORT_MS})", main)
        self.assertIn(f"HAL_Delay({twin.LOOP_MS})", main)
        self.assertIn(f"rx_buffer[{twin.RX_BUFFER}]", main)
        self.assertIn("hiwdg.Init.Prescaler = IWDG_PRESCALER_32", main)
        self.assertIn("hiwdg.Init.Reload = 999", main)
        self.assertEqual(twin.IWDG_MS, 32 * 1000 * 1000 // 40000)

    def test_the_properties_the_model_reproduces_are_still_in_the_source(self):
        main, protocol = text(BOOT / "slaveboot_main.c"), text(BOOT / "slaveboot_protocol.c")
        # the direction constant that describes the master reading arms the receive
        self.assertRegex(main, r"if \(TransferDirection == I2C_DIRECTION_RECEIVE\) \{\s*//[^\n]*\n\s*HAL_I2C_Slave_Seq_Receive_IT")
        # no window: the application is entered straight after the validity check in main()
        self.assertRegex(main, r"if \(ApplicationIsValid\(\)\) \{\s*JumpToApplication\(\);")
        # a short write is expected to reach the receive-complete callback, which reads the count from XferCount
        self.assertIn("sizeof(rx_buffer) - hi2c->XferCount", main)
        # the work is done in the callbacks themselves, and the pause before the reset is a HAL_Delay inside the one of the confirmation
        self.assertIn("HAL_Delay(200)", protocol)
        self.assertRegex(protocol, r"HAL_Delay\(200\);[^\n]*\n\s*NVIC_SystemReset\(\);")
        self.assertIn("Flash_ErasePages(BACKUP_APP_ADDR, pages_needed)", protocol)
        self.assertIn("HandleConfirm(&rx_buffer[1])", main)
        # the ST HAL default places the tick below every other interrupt
        hal_conf = ROOT / "build" / "common" / "HAL_Include" / "stm32f3xx_hal_conf.h"
        if hal_conf.is_file():
            self.assertIn("TICK_INT_PRIORITY            ((uint32_t)(1U<<__NVIC_PRIO_BITS) - 1U)", text(hal_conf))


class FlashTests(unittest.TestCase):
    def test_bits_only_go_down_until_the_page_is_erased(self):
        flash = twin.Flash()
        address = twin.BACKUP_APP_ADDR
        self.assertTrue(flash.program_halfword(address, 0x1234))
        self.assertFalse(flash.program_halfword(address, 0x1234))      # not blank any more
        self.assertEqual(flash.read(address, 2), b"\x34\x12")
        flash.erase_page(address)
        self.assertEqual(flash.read(address, 2), b"\xff\xff")

    def test_a_power_cut_tears_the_operation_it_hits(self):
        flash = twin.Flash()
        flash.cut_after = 1
        with self.assertRaises(twin.PowerCut):
            flash.program_halfword(twin.BACKUP_APP_ADDR, 0xABCD)
        self.assertEqual(flash.read(twin.BACKUP_APP_ADDR, 2), b"\xcd\xff")
        flash = twin.Flash()
        flash.data[:] = b"\x00" * len(flash.data)
        flash.cut_after = 1
        with self.assertRaises(twin.PowerCut):
            flash.erase_page(twin.BACKUP_APP_ADDR)
        self.assertEqual(flash.read(twin.BACKUP_APP_ADDR, 2), b"\xff\xff")
        self.assertEqual(flash.read(twin.BACKUP_APP_ADDR + twin.PAGE - 2, 2), b"\x00\x00")

    def test_the_version_rule_uses_the_patch_only_when_both_sides_know_it(self):
        pack = twin.minor_field_pack
        self.assertTrue(twin.version_is_older(0, pack(4, 0), 0, pack(5, 0)))
        self.assertTrue(twin.version_is_older(0, pack(5, 0), 0, pack(5, 1)))
        self.assertFalse(twin.version_is_older(0, 5, 0, pack(5, 1)))
        self.assertFalse(twin.version_is_older(0, pack(5, 0), 0, 5))
        self.assertTrue(twin.version_is_older(0, pack(9, 9), 1, pack(0, 0)))


class ModelTests(unittest.TestCase):
    def test_the_scenarios_pass(self):
        problems, known = twin.run_checks()
        self.assertEqual(problems, [])
        self.assertEqual(len(known), 5)

    def test_the_address_callback_as_built_makes_every_register_unreachable(self):
        chip = twin.SlaveBootloader(twin.Flash())
        chip.boot()
        self.assertEqual(chip.master_write(bytes([twin.REG_STATUS])), 0)
        self.assertIsNone(chip.master_read(1))
        self.assertIsNone(chip.read_register(twin.REG_QUERY_VERSION, 10))

    def test_with_the_direction_inverted_every_register_answers(self):
        chip = twin.SlaveBootloader(twin.Flash(), direction_fixed=True)
        chip.boot()
        self.assertEqual(chip.read_register(twin.REG_QUERY_VERSION, 10)[0], 1)
        self.assertEqual(chip.read_register(twin.REG_QUERY_VERSION_EXT, 4)[0], twin.VERSION_EXT_MARKER)
        self.assertEqual(chip.read_register(twin.REG_CHALLENGE, 16), bytes(16))
        self.assertEqual(chip.read_register(0x55, 1), bytes([twin.STATUS_LISTENING]))
        self.assertEqual(chip.read_register(twin.REG_PROGRESS, 1), b"\xff")

    def test_a_write_longer_than_the_buffer_is_cut_to_it(self):
        chip = twin.SlaveBootloader(twin.Flash(), direction_fixed=True)
        chip.boot()
        self.assertEqual(chip.master_write(bytes(40)), twin.RX_BUFFER)

    def test_the_confirmation_binds_every_field(self):
        base = (bytes(range(16)), bytes(range(32)), twin.HARDWARE_ID, 0, 0x8104, 3000, 7)
        tag = twin.confirm_tag(*base)
        for index, changed in enumerate((bytes(16), bytes(32), 1, 1, 0x8105, 3001, 8)):
            args = list(base)
            args[index] = changed
            self.assertNotEqual(twin.confirm_tag(*args), tag, index)
        self.assertEqual(len(twin.confirm_message(*base)), 80)

    def test_a_power_cut_in_the_metadata_append_never_leaves_a_lower_floor(self):
        flash = twin.Flash()
        chip = twin.SlaveBootloader(flash, direction_fixed=True)
        chip.boot()
        twin.Master(chip, twin.application_image(3000, seed=1), counter=4).run()
        for cut in range(1, 120):
            copy = twin.Flash()
            copy.data[:] = flash.data
            copy.cut_after = cut
            victim = twin.SlaveBootloader(copy, direction_fixed=True)
            try:
                victim.metadata_write(twin.Meta(version_minor=9, size=3000, counter=9, sha256=bytes(32)))
            except twin.PowerCut:
                pass
            copy.cut_after = None
            self.assertIn(twin.SlaveBootloader(copy, direction_fixed=True).counter_floor(), (4, 9), cut)

    def test_a_chip_that_is_not_the_slave_cannot_be_updated_with_the_wrong_board_id(self):
        chip = twin.SlaveBootloader(twin.Flash(), direction_fixed=True)
        chip.boot()
        master = twin.Master(chip, twin.application_image(3000), hardware_id=twin.HARDWARE_ID ^ 0x100)
        master.start()
        self.assertEqual((master.status(), master.reason()), (twin.STATUS_VERIFY_FAIL, twin.FAIL_HARDWARE_ID))


class MainTests(unittest.TestCase):
    def test_check_prints_the_verdict_and_exits_zero(self):
        import contextlib
        import io
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(twin.main(["--check"]), 0)
        self.assertIn("SLAVE_TWIN=PASS", out.getvalue())
        self.assertEqual(sum(line.startswith("KNOWN ") for line in out.getvalue().splitlines()), 5)

    def test_the_meta_record_is_72_bytes_with_its_crc(self):
        chip = twin.SlaveBootloader(twin.Flash(), direction_fixed=True)
        chip.boot()
        chip.metadata_write(twin.Meta(size=10, counter=3))
        record = chip.flash.read(twin.METADATA_ADDR, twin.META_RECORD_SIZE)
        self.assertEqual(struct.unpack_from("<I", record, 0)[0], 1)
        self.assertEqual(struct.unpack_from("<I", record, 68)[0], zlib.crc32(record[:68]) & 0xFFFFFFFF)


if __name__ == "__main__":
    unittest.main()
