#!/usr/bin/env python3
# =============================================================================
# bump_bootloader_version.py - Apply URTC's ecosystem-wide versioning policy
# to ONE bootloader's own version header.
#
# PROJECT: URTC (Universal Robot Tool Controller)
# AUTHOR: JuanenRac (Electro Hobby 3D) - electrohobby3d@gmail.com
# LICENSE: GPL-3.0 (same as the firmware this versions - see LICENSE at repo root)
#
# POLICY (confirmed by the project owner, 21 August 2026):
#   - Application firmware version_string (FIRMWARE_VERSION_MAJOR/MINOR in
#     firmware_common.h / slave_common.h) is STATIC - it only changes when a
#     human edits it by hand. Nothing in the build ever touches it.
#   - Bootloader version (BOOTLOADER_VERSION_MAJOR/MINOR/PATCH in
#     bootloader_common.h / slaveboot_common.h) is INCREMENTAL - every real
#     build of that bootloader (a real compile+link that produces a fresh
#     .bin, not just a source edit) bumps PATCH by 1, automatically, before
#     the compiler ever reads the header. This script IS that bump: it's
#     called by build_firmware.sh/.bat as the very first step of compiling
#     each bootloader, so the resulting .bin already embeds the new version.
#   - The PATCH digit carries into MINOR the same way a car odometer carries
#     into the next digit - patch 9 is followed by patch 0 with minor+1, not
#     patch 10. Example given by the project owner: 1.1.7 -> 1.1.8 -> 1.1.9
#     -> (next build) -> 1.2.0, never 1.1.10. If MINOR itself would carry
#     past 9 the same way, it resets to 0 and MAJOR gains 1 - same rule,
#     one level up.
#
# This script only ever touches ONE bootloader header per invocation - it
# has no idea which chip it's versioning, on purpose, so the exact same
# logic serves both src/F303-master/boot/bootloader_common.h and
# src/F303-slave/boot/slaveboot_common.h without a second copy of this
# code drifting apart from the first (same reasoning generate_manifest.py's
# own header comment gives for keeping build_firmware.sh/.bat sharing this
# script rather than each re-implementing it).
#
# Usage: python3 bump_bootloader_version.py <path-to-header.h>
# Rewrites the file in place (only the 3 numeric #define values change -
# every comment, every other line, byte-for-byte untouched) and prints
# "OLD -> NEW" to stdout. Exits non-zero, with the header UNMODIFIED, if
# any of the 3 #defines can't be found - never guesses.
# =============================================================================
import re
import sys


def read_int_define(text, path, name):
    m = re.search(rf"#define\s+BOOTLOADER_VERSION_{name}\s+(\d+)", text)
    if not m:
        raise SystemExit(
            f"bump_bootloader_version.py: could not find #define "
            f"BOOTLOADER_VERSION_{name} in {path} - header NOT modified, "
            f"check this script's own regex against that file's real "
            f"current content before trusting anything else here."
        )
    return int(m.group(1))


def set_int_define(text, name, value):
    return re.sub(
        rf"(#define\s+BOOTLOADER_VERSION_{name}\s+)\d+",
        rf"\g<1>{value}",
        text,
        count=1,
    )


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: bump_bootloader_version.py <path-to-header.h>")
    path = sys.argv[1]

    with open(path) as f:
        text = f.read()

    major = read_int_define(text, path, "MAJOR")
    minor = read_int_define(text, path, "MINOR")
    patch = read_int_define(text, path, "PATCH")
    old = f"{major}.{minor}.{patch}"

    # The odometer carry: patch 9 -> 0 with minor+1; minor 9 -> 0 with major+1.
    patch += 1
    if patch > 9:
        patch = 0
        minor += 1
        if minor > 9:
            minor = 0
            major += 1

    new = f"{major}.{minor}.{patch}"

    text = set_int_define(text, "MAJOR", major)
    text = set_int_define(text, "MINOR", minor)
    text = set_int_define(text, "PATCH", patch)

    with open(path, "w") as f:
        f.write(text)

    print(f"bump_bootloader_version.py: {path}: {old} -> {new} (this build)")


if __name__ == "__main__":
    main()
