#!/usr/bin/env python3
# =============================================================================
# URTC - check that the vendored Melexis drivers are identical on both chips
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
# GPL-3.0-or-later - see LICENSE
# =============================================================================
# The main board (src/F303-master/) and the expansion slave (src/F303-slave/)
# each compile their own copy of the Melexis MLX9064x drivers - the two
# builds use different include paths and HALs, so the copies stay separate
# files. They must not drift apart, though: they once did, and only the
# master's copy had the bounded data-ready wait and the scale-loop iteration
# cap, so the slave could spin forever on a stuck sensor or a blank EEPROM
# dump until its IWDG reset it. Run by build_firmware.sh/.bat (step 1c); exits
# 1 and names every file that differs.
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
MASTER = os.path.join(ROOT, "src", "F303-master")
SLAVE = os.path.join(ROOT, "src", "F303-slave")
VENDOR_DIRS = ("melexis_mlx90640", "melexis_mlx90641", "melexis_mlx90642")


def shared_files():
    """Every file in the slave's vendor folders (the slave only carries the
    vendor driver itself; the master adds its own *_app/*_transport glue)."""
    for folder in VENDOR_DIRS:
        for name in sorted(os.listdir(os.path.join(SLAVE, folder))):
            yield os.path.join(folder, name)


def same_content(a, b):
    """Equal byte for byte once line endings are normalised: a checkout may
    hold the same file with LF on one chip and CRLF on the other."""
    with open(a, "rb") as fa, open(b, "rb") as fb:
        return fa.read().replace(b"\r\n", b"\n") == fb.read().replace(b"\r\n", b"\n")


def main():
    different = [rel for rel in shared_files()
                 if not same_content(os.path.join(MASTER, rel), os.path.join(SLAVE, rel))]
    for rel in different:
        print(f"DIFFERS: src/F303-master/{rel} vs src/F303-slave/{rel}")
    if different:
        print("Copy the fixed version to both chips (the master's is the reference).")
        return 1
    print(f"{sum(1 for _ in shared_files())} shared Melexis driver files identical on master and slave")
    return 0


if __name__ == "__main__":
    sys.exit(main())
