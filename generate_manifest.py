#!/usr/bin/env python3
# =============================================================================
# generate_manifest.py - Regenerate firmware/firmware_manifest.json
#
# PROJECT: URTC (Universal Robot Tool Controller)
# AUTHOR: JuanenRac (Electro Hobby 3D) - electrohobby3d@gmail.com
# LICENSE: GPL-3.0 (same as the firmware this indexes - see LICENSE at repo root)
#
# Reads every component's own real version straight from its own source
# header (never hardcoded here) and computes a fresh CRC32 over each real
# .bin file in firmware/ - run this any time firmware/ changes and the
# manifest needs to catch up, not just after a full rebuild. Both
# build_firmware.sh and build_firmware.bat call this same script as their
# own last step, rather than each having their own separate copy of this
# logic - keeping exactly one implementation avoids the 2 build scripts
# silently drifting apart on what a "fresh" manifest actually contains.
#
# Usage: python3 generate_manifest.py [repo_root]
#   repo_root defaults to this script's own parent directory.
# =============================================================================
import sys
import os
import re
import zlib
import json
from datetime import datetime, timezone


def read_define(path, name):
    with open(path) as f:
        text = f.read()
    m = re.search(rf"#define\s+{name}\s+(\S+)", text)
    if not m:
        raise SystemExit(
            f"generate_manifest.py: could not find #define {name} in {path} - "
            f"manifest NOT written, check this script's own regex against that "
            f"file's real current content before trusting anything else here."
        )
    return m.group(1).rstrip("UL")


def bin_info(fw_dir, name):
    path = os.path.join(fw_dir, name)
    if not os.path.exists(path):
        raise SystemExit(
            f"generate_manifest.py: expected binary {path} does not exist - "
            f"run a full build first (build_firmware.sh/.bat with no target "
            f"argument, or 'all'), manifest NOT written."
        )
    with open(path, "rb") as f:
        data = f.read()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    return {"filename": name, "size_bytes": len(data), "crc32": f"0x{crc:08X}"}


def file_info(fw_dir, name):
    path = os.path.join(fw_dir, name)
    if not os.path.exists(path):
        return None
    return {"filename": name, "size_bytes": os.path.getsize(path)}


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    fw_dir = os.path.join(root, "firmware")

    mb_h = os.path.join(root, "src", "F303-master", "boot", "bootloader_common.h")
    mb_maj, mb_min, mb_pat = (
        int(read_define(mb_h, f"BOOTLOADER_VERSION_{x}")) for x in ("MAJOR", "MINOR", "PATCH")
    )
    mb_hwid = read_define(mb_h, "THIS_HARDWARE_ID")

    ma_h = os.path.join(root, "src", "F303-master", "firmware_common.h")
    ma_maj, ma_min = (int(read_define(ma_h, f"FIRMWARE_VERSION_{x}")) for x in ("MAJOR", "MINOR"))
    ma_hwid = read_define(ma_h, "THIS_HARDWARE_ID")
    app_bin = f"URTC_V{ma_maj}.{ma_min}_F303CC.bin"

    sb_h = os.path.join(root, "src", "F303-slave", "boot", "slaveboot_common.h")
    sb_maj, sb_min, sb_pat = (
        int(read_define(sb_h, f"BOOTLOADER_VERSION_{x}")) for x in ("MAJOR", "MINOR", "PATCH")
    )
    sb_hwid = read_define(sb_h, "THIS_HARDWARE_ID")

    sa_h = os.path.join(root, "src", "F303-slave", "slave_common.h")
    sa_maj, sa_min = (int(read_define(sa_h, f"FIRMWARE_VERSION_{x}")) for x in ("MAJOR", "MINOR"))
    sa_hwid = read_define(sa_h, "THIS_HARDWARE_ID")

    manifest = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "project": {
            "name": "URTC",
            "full_name": "Universal Robot Tool Controller",
            "repository": "https://github.com/JuanenRac/URTC",
            "author": "JuanenRac (Electro Hobby 3D)",
        },
        "notes": [
            "This indexes 4 INDEPENDENT components (2 chips, each with its own "
            "bootloader and application) - version_code is only meaningful "
            "compared against an EARLIER manifest's own value for that exact "
            "same component key. Comparing version_code ACROSS different "
            "component keys means nothing.",
            "version_code = major*10000 + minor*100 + patch (patch=0 for the 2 "
            "application components) - bigger is newer, safe for simple "
            "numeric comparison. Don't compare version_string alphabetically.",
            "crc32 is computed over the real .bin file exactly as shipped here "
            "(standard CRC-32/ISO-HDLC), the same variant this project's own "
            "bootloader computes internally during a real update (see "
            "src/F303-master/boot/bootloader_flash.c's own CRC32_Update/"
            "CRC32_Finalize) - a mismatch against a freshly-downloaded copy "
            "means the file changed since this manifest was generated.",
            "hardware_id is what CAN 0x7F8/0x7F9 (version query) reports live "
            "from a real connected board - cross-check it against that live "
            "answer before flashing anything, not just this manifest's own "
            "filename-to-chip mapping.",
            "flash_region_max_bytes is the maximum this component could ever "
            "be, not this specific build's own actual size (see "
            "files.bin.size_bytes for that).",
            "Regenerated by generate_manifest.py, called automatically as the "
            "last step of a full build_firmware.sh/.bat run - hand-editing "
            "this file is pointless, it'll be overwritten the next time "
            "either script (or this one directly) runs.",
        ],
        "components": {
            "main_bootloader": {
                "display_name": "URTC Bootloader (main board)",
                "chip": "STM32F303CCT6",
                "hardware_id": mb_hwid,
                "version": {"major": mb_maj, "minor": mb_min, "patch": mb_pat},
                "version_string": f"{mb_maj}.{mb_min}.{mb_pat}",
                "version_code": mb_maj * 10000 + mb_min * 100 + mb_pat,
                "flash_address": "0x08000000",
                "flash_region_max_bytes": 32768,
                "files": {
                    "bin": bin_info(fw_dir, "URTC_BOOTLOADER.bin"),
                    "hex": file_info(fw_dir, "URTC_BOOTLOADER.hex"),
                    "elf": file_info(fw_dir, "URTC_BOOTLOADER.elf"),
                },
            },
            "main_application": {
                "display_name": "URTC Application Firmware (main board)",
                "chip": "STM32F303CCT6",
                "hardware_id": ma_hwid,
                "version": {"major": ma_maj, "minor": ma_min},
                "version_string": f"{ma_maj}.{ma_min}",
                "version_code": ma_maj * 10000 + ma_min * 100,
                "flash_address": "0x08008000",
                "flash_region_max_bytes": 114688,
                "files": {
                    "bin": bin_info(fw_dir, app_bin),
                    "hex": file_info(fw_dir, app_bin.replace(".bin", ".hex")),
                    "elf": file_info(fw_dir, app_bin.replace(".bin", ".elf")),
                },
            },
            "slave_bootloader": {
                "display_name": "URTC Expansion Slave Bootloader",
                "chip": "STM32F303CBT6",
                "hardware_id": sb_hwid,
                "version": {"major": sb_maj, "minor": sb_min, "patch": sb_pat},
                "version_string": f"{sb_maj}.{sb_min}.{sb_pat}",
                "version_code": sb_maj * 10000 + sb_min * 100 + sb_pat,
                "flash_address": "0x08000000",
                "flash_region_max_bytes": 18432,
                "files": {
                    "bin": bin_info(fw_dir, "URTC_SLAVE_BOOTLOADER.bin"),
                    "hex": file_info(fw_dir, "URTC_SLAVE_BOOTLOADER.hex"),
                    "elf": file_info(fw_dir, "URTC_SLAVE_BOOTLOADER.elf"),
                },
            },
            "slave_application": {
                "display_name": "URTC Expansion Slave Application",
                "chip": "STM32F303CBT6",
                "hardware_id": sa_hwid,
                "version": {"major": sa_maj, "minor": sa_min},
                "version_string": f"{sa_maj}.{sa_min}",
                "version_code": sa_maj * 10000 + sa_min * 100,
                "flash_address": "0x08005000",
                "flash_region_max_bytes": 55296,
                "files": {
                    "bin": bin_info(fw_dir, "URTC_SLAVE_APP.bin"),
                    "hex": file_info(fw_dir, "URTC_SLAVE_APP.hex"),
                    "elf": file_info(fw_dir, "URTC_SLAVE_APP.elf"),
                },
            },
        },
    }

    out_path = os.path.join(fw_dir, "firmware_manifest.json")
    with open(out_path, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"firmware_manifest.json regenerated at {out_path}")
    for key, comp in manifest["components"].items():
        print(f"  {key}: v{comp['version_string']}  {comp['files']['bin']['filename']}  crc32={comp['files']['bin']['crc32']}")


if __name__ == "__main__":
    main()
