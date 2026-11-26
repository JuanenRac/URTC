# The stored version rule (an image without a patch number)

The command, its signature, the answer and what the relaxed mode does not cover are described in URTC-COMMON (`docs/VERSION_COMPAT.md`). This page says how
the firmware of this repository carries it. **All of it is off by default**: with the three switches at 0 none of the code is compiled.

## Build switches

| Switch | Component | What it adds |
|---|---|---|
| `URTC_BL_VERSION_COMPAT_COMMAND=1` | main bootloader | reads the command and the query on `0x7EE`, keeps the mode, applies it at the end of an update. Needs `URTC_BL_META_AB=1` |
| `URTC_APP_VERSION_COMPAT_RELAY=1` | main application | reads the stored mode and tells the slave before the END of a slave update it vouched for |
| `URTC_SLAVE_VERSION_COMPAT=1` | slave bootloader | register `0x0D`, for one update session only |

Set them in the environment of `build_firmware.sh` or `build_firmware.bat`. The official release workflow sets none of them.

## Where the mode is kept

In the main board, as 16 bytes at offset 128 of whichever metadata page holds the newest record (`src/common/urtc_vc_annex.h`). The record itself does not
change, so a bootloader without the feature reads the pages as before; when it rewrites a page the mode is gone, which is the strict rule again. The 16 bytes
are programmed before the magic of the page, so a power cut leaves the old record with the old mode or the new record with the new one.

The counter that stops a command from being sent twice lives in the same 16 bytes. It starts again from 0 only where the 16 bytes are gone: the metadata was
erased with a programmer, or a bootloader without the feature wrote the pages. Both need access to the board.

The slave keeps nothing.

## What was and was not tried

Host programs only (`bootloader_version_compat`, `slaveboot_version_compat`, `can_slavebridge_version_compat` and the same behaviour with the switches off),
including a flash failure at every step of the writes. Nothing was tried on a board.

## To try on a board

1. Build the three components with the switches on, flash them with a programmer, install an application with a patch number (x.y.1 or more).
2. Send the query: the answer must say strict, nothing stored. Send an x.y.0 image without the patch: refused as a rollback.
3. Send a signed accept command: answer ok. Power the board off and on, query again: the mode is still there. The x.y.0 image without the patch installs.
4. Send the same command again: refused as a replay. Send a command with one byte changed: refused as a bad signature.
5. Send a signed strict command: the image without the patch is refused again.
6. Cut the power while a command is being written (repeat a number of times): the board starts, the application runs and the mode is the old or the new one.
7. With the head on strict and then on accept, repeat step 2 and 3 for a slave update. Check that the master writes register `0x0D` (link analyser) only in accept
   mode and that the slave forgets it after a new start of the update.
8. Flash the released bootloaders back and check that the board starts and keeps its serial and rollback counter.
