#!/usr/bin/env python3
# =============================================================================
# bump_version.py - increment ONE component's own version macros (bootloader
# OR application) in its header, in place, before that component gets
# compiled. Supersedes bump_bootloader_version.py, which only ever handled
# the 2 bootloaders - application firmware is incremental too now.
#
# PROJECT: URTC (Universal Robot Tool Controller)
# AUTHOR: JuanenRac (Electro Hobby 3D) - electrohobby3d@gmail.com
# LICENSE: GPL-3.0 (same as the firmware this versions - see LICENSE at repo root)
#
# POLICY: all 4 components (2 application firmwares, 2 bootloaders) are
# incremental - every real build of a component (a compile+link that
# produces a fresh .bin, not just a source edit) bumps that component's own
# PATCH by 1, automatically, before the compiler ever reads its header.
# "Odometer" carry: patch += 1; if patch > 9: patch = 0, minor += 1; if
# minor > 9 (checked unconditionally, so a minor already at 9 for any other
# reason still carries): minor = 0, major += 1.
#
# Usage: python3 bump_version.py <header_path> <MACRO_PREFIX> [<mirror_header>]
#   header_path    path to the component's own header (bootloader_common.h /
#                  slaveboot_common.h for a bootloader; firmware_common.h /
#                  slave_common.h for an application).
#   MACRO_PREFIX   "BOOTLOADER_VERSION" or "FIRMWARE_VERSION" - touches only
#                  that macro family's own _MAJOR/_MINOR/_PATCH #defines.
#   mirror_header  OPTIONAL. Only meaningful when MACRO_PREFIX is
#                  FIRMWARE_VERSION: each bootloader keeps its own copy of
#                  FIRMWARE_VERSION_MAJOR/MINOR/PATCH (the version of
#                  whatever application image is currently installed - used
#                  by ApplicationIsValid()'s first-boot-adoption path and by
#                  the anti-rollback check in HandleEndUpdate, see
#                  bootloader_protocol.c) rather than #including the
#                  application's own header, since the two never build
#                  together. Without this argument that copy can silently
#                  drift from the real application version - which is
#                  exactly what happened before this script existed:
#                  bootloader_common.h's own copy sat at FIRMWARE_VERSION_MINOR
#                  0 while firmware_common.h (the real application) had
#                  already moved to 1. When mirror_header is given, this
#                  script writes the SAME new MAJOR.MINOR.PATCH into that
#                  file's own FIRMWARE_VERSION_* macros right after bumping
#                  the primary header, so the two can never drift apart
#                  again as long as every real app build goes through here.
#
# Prints the new "MAJOR.MINOR.PATCH" to stdout on success (nothing else, so
# a caller can capture it directly); exits non-zero with a message on stderr
# if the expected #defines aren't found in EITHER file - never writes a
# partial/inconsistent header, and never writes the primary header if the
# mirror write would fail.
# =============================================================================
import sys
import re


def get_macro(text, path, prefix, name):
    m = re.search(rf"#define\s+{prefix}_{name}\s+(\d+)", text)
    if not m:
        print(
            f"bump_version.py: could not find #define {prefix}_{name} in {path} "
            f"- nothing written, check this script's own regex against that "
            f"file's real current content before trusting anything else here.",
            file=sys.stderr,
        )
        sys.exit(1)
    return int(m.group(1))


def set_macro(text, path, prefix, name, value):
    new_text, n = re.subn(
        rf"(#define\s+{prefix}_{name}\s+)\d+",
        rf"\g<1>{value}",
        text,
        count=1,
    )
    if n != 1:
        print(
            f"bump_version.py: expected exactly 1 occurrence of #define "
            f"{prefix}_{name} in {path}, found {n} - nothing written.",
            file=sys.stderr,
        )
        sys.exit(1)
    return new_text


def main():
    if len(sys.argv) not in (3, 4):
        print("usage: bump_version.py <header_path> <MACRO_PREFIX> [<mirror_header>]", file=sys.stderr)
        return 1
    path, prefix = sys.argv[1], sys.argv[2]
    mirror_path = sys.argv[3] if len(sys.argv) == 4 else None

    with open(path) as f:
        text = f.read()

    major = get_macro(text, path, prefix, "MAJOR")
    minor = get_macro(text, path, prefix, "MINOR")
    patch = get_macro(text, path, prefix, "PATCH")

    # The odometer carry rule itself - see this file's own header comment.
    patch += 1
    if patch > 9:
        patch = 0
        minor += 1
    if minor > 9:
        minor = 0
        major += 1

    text = set_macro(text, path, prefix, "MAJOR", major)
    text = set_macro(text, path, prefix, "MINOR", minor)
    text = set_macro(text, path, prefix, "PATCH", patch)

    mirror_text = None
    if mirror_path is not None:
        with open(mirror_path) as f:
            mirror_text = f.read()
        # Validate all 3 macros exist in the mirror BEFORE writing anything,
        # so a broken mirror never leaves the primary header bumped alone.
        get_macro(mirror_text, mirror_path, prefix, "MAJOR")
        get_macro(mirror_text, mirror_path, prefix, "MINOR")
        get_macro(mirror_text, mirror_path, prefix, "PATCH")
        mirror_text = set_macro(mirror_text, mirror_path, prefix, "MAJOR", major)
        mirror_text = set_macro(mirror_text, mirror_path, prefix, "MINOR", minor)
        mirror_text = set_macro(mirror_text, mirror_path, prefix, "PATCH", patch)

    with open(path, "w") as f:
        f.write(text)
    if mirror_path is not None:
        with open(mirror_path, "w") as f:
            f.write(mirror_text)

    print(f"{major}.{minor}.{patch}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
