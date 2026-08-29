# Changelog - URTC (repo-wide index)

## [0.2.8] - Fixed after a live ecosystem bug audit

- **`src/F303-master/firmware_can_global_pre.c`** - two stale comments
  said the 5-bit tool-ID scheme supported "0-11 = a real tool head, 12+ =
  no tool assigned" and "1-12=one of the 12 currently supported tool
  profiles". The real `ToolMode_t` enum (`firmware_common.h`) has
  supported 0-24 (25 real tool profiles, matching the README's own "25
  Plug-and-Play Automated Profiles") since the 15-tool expansion; only
  `firmware_can_global_pre.c`'s own comments never got updated, which
  would mislead a maintainer into thinking IDs 12-24 are invalid. No
  functional change - the code already handled 0-24 correctly.

## [0.2.7]

- Build version synchronized with `hydra-umc.project.json` and the repository-native version source.

## [0.2.6]

- Build version synchronized with `hydra-umc.project.json` and the repository-native version source.

## [0.2.5]

- Build version synchronized with `hydra-umc.project.json` and the repository-native version source.


This project has **4 independent version tracks**, one per compiled
component - there is no single combined "project version" that moves
them all together. Flashing a new bootloader never implies a new
application version, and vice versa. Each component's own detailed,
per-change history lives in its own file:

- Main board application firmware (`src/F303-master/`): [`src/F303-master/CHANGELOG.md`](src/F303-master/CHANGELOG.md)
- Main board bootloader (`src/F303-master/boot/`): [`src/F303-master/boot/CHANGELOG.md`](src/F303-master/boot/CHANGELOG.md)
- Expansion slave application firmware (`src/F303-slave/`, STM32F303CBT6): [`src/F303-slave/CHANGELOG.md`](src/F303-slave/CHANGELOG.md)
- Expansion slave bootloader (`src/F303-slave/boot/`): [`src/F303-slave/boot/CHANGELOG.md`](src/F303-slave/boot/CHANGELOG.md)

This file only tracks the **current version** of all 4 at a glance and
documents the versioning *policy* itself. For what actually changed in
any given version, read the component's own file above.

## Versioning policy (confirmed by the project owner)

All 4 components are **incremental** (`MAJOR.MINOR.PATCH`) - every real
build (`build_firmware.sh`/`.bat`, a real compile+link that produces a
fresh `.bin`) automatically bumps that component's own `PATCH` by 1, via
`bump_version.py` (repo root), run as the first step of compiling it -
*before* the compiler reads the header.

| Component | Header | Macro prefix |
|---|---|---|
| Main board application firmware | `src/F303-master/firmware_common.h` | `FIRMWARE_VERSION_*` |
| Main board bootloader | `src/F303-master/boot/bootloader_common.h` | `BOOTLOADER_VERSION_*` |
| Expansion slave application firmware | `src/F303-slave/slave_common.h` | `FIRMWARE_VERSION_*` |
| Expansion slave bootloader | `src/F303-slave/boot/slaveboot_common.h` | `BOOTLOADER_VERSION_*` |

Each bootloader also keeps its own copy of `FIRMWARE_VERSION_MAJOR/MINOR/
PATCH` (the version of whatever application image is currently
installed - used by the anti-rollback check in `HandleEndUpdate()`,
since the bootloader and application never build together and can't
share a header). `bump_version.py` mirrors the new version into that
copy every time the matching application is built, so the two can never
drift apart - never hand-edit either copy directly.

**The carry rule** is a base-10 "odometer" rule: if bumping `PATCH`
would take it past 9, it resets to 0 and `MINOR` gains 1 instead (and
the same rule applies one level up, from `MINOR` into `MAJOR`, if
`MINOR` would ever pass 9). For example:
`0.1.7` → `0.1.8` → `0.1.9` → `0.2.0` — never `0.1.10`.

Building only one target (`build_firmware.sh master` / `slave`) only
bumps that target's own 2 components (application + bootloader); the
other target's components are untouched, exactly as if that build
hadn't run at all.

## Binary filename convention (changed - see each component's own CHANGELOG)

All 4 compiled binaries now share ONE naming convention -
`<PREFIX>_v<MAJOR>.<MINOR>.<PATCH>.{bin,hex,elf}` - matching sibling repo
HYDRA-UMC's own `build_firmware.sh` from the start:

| Component | Filename prefix |
|---|---|
| Main board bootloader | `URTC_MAIN_BOOTLOADER` |
| Main board application firmware | `URTC_MAIN_FIRMWARE` |
| Expansion slave bootloader | `URTC_SLAVE_BOOTLOADER` |
| Expansion slave application firmware | `URTC_SLAVE_FIRMWARE` |

This REPLACES the old, inconsistent per-component policy: the 2
bootloaders and the slave application used to ship with NO version in
the filename at all (`URTC_BOOTLOADER.bin`, `URTC_SLAVE_BOOTLOADER.bin`,
`URTC_SLAVE_APP.bin`), and the main application used a different,
PATCH-less format (`URTC_V<MAJOR>.<MINOR>_F303CC.bin`). `generate_manifest.py`
now locates each real `.bin` in `firmware/` via a glob for
`<PREFIX>_v*.bin` rather than reconstructing the exact name, so it can't
drift out of sync with what `build_firmware.sh`/`.bat` actually produced.
See `VERSION_CHECKLIST.txt` (Track A3/C/E2) and each component's own
CHANGELOG for the full detail.

## Current versions

| Component | Version | Source of truth |
|---|---|---|
| Main board application firmware | 0.2.5 (incremental) | `src/F303-master/firmware_common.h` |
| Main board bootloader | 0.3.2 (incremental) | `src/F303-master/boot/bootloader_common.h` |
| Expansion slave application firmware | 0.1.2 (incremental) | `src/F303-slave/slave_common.h` |
| Expansion slave bootloader | 0.1.5 (incremental) | `src/F303-slave/boot/slaveboot_common.h` |

The bootloader numbers above will already be higher again by the time
you read this if a build has run since - that's expected, not stale
documentation to "fix"; `firmware/firmware_manifest.json` (regenerated
by every full `build_firmware.sh`/`.bat` run) always reflects the real,
current, as-built version of all 4 components, and is the actual source
of truth for "what's in `firmware/` right now" - this table is a
snapshot for a reader who just wants the versioning *scheme* explained
without cross-referencing 4 separate header files.

See [`VERSION_CHECKLIST.txt`](VERSION_CHECKLIST.txt) for the full
mechanical checklist covering every place each of these numbers appears
(binary filenames, README changelog tables, documentation identity
tags, etc.), and [`check_version_consistency.sh`](check_version_consistency.sh)
for an automated consistency check against that checklist.
