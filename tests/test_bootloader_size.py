"""tools/bootloader_size.py: the regions come from the linker script, the size output is parsed, and a bootloader that leaves too little room fails."""
import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("urtc_bootloader_size", ROOT / "tools" / "bootloader_size.py")
size = importlib.util.module_from_spec(_spec)
sys.modules["urtc_bootloader_size"] = size
_spec.loader.exec_module(size)


class BootloaderSizeTests(unittest.TestCase):
    def test_regions_of_a_linker_script(self):
        text = "MEMORY\n{\n  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 30K\n  RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 40K\n}\n"
        self.assertEqual(size.regions(text), {"FLASH": 30720, "RAM": 40960})

    def test_every_configured_linker_script_has_its_regions(self):
        config = size.json.loads(size.CONFIG.read_text(encoding="utf-8"))
        for name, entry in config["bootloaders"].items():
            found = size.regions((ROOT / entry["linker"]).read_text(encoding="utf-8"))
            self.assertIn(entry.get("flash_region", "FLASH"), found, name)
            self.assertIn(entry.get("ram_region", "RAM"), found, name)
            self.assertGreater(entry["flash_margin_bytes"], 0)

    def test_berkeley_output(self):
        self.assertEqual(size.parse_size("   text\t   data\t    bss\t    dec\t    hex\tfilename\n  25624\t     28\t   4888\t  30540\t   774c\ta.elf\n"), (25624, 28, 4888))

    def test_the_table_lists_the_problems(self):
        row = {"text": 1, "data": 2, "bss": 3, "flash_used": 3, "flash_region": 10, "flash_free": 7, "flash_margin": 8,
               "ram_used": 5, "ram_region": 10, "ram_free": 5, "ram_margin": 1}
        text = size.markdown([("main", row)], ["main: too big"])
        self.assertIn("| main | 1 | 2 | 3 |", text)
        self.assertIn("**Too big:**", text)


if __name__ == "__main__":
    unittest.main()
