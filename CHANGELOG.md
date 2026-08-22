# Changelog - URTC (repo-wide index)

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
`1.1.7` → `1.1.8` → `1.1.9` → `1.2.0` — never `1.1.10`.

Building only one target (`build_firmware.sh master` / `slave`) only
bumps that target's own 2 components (application + bootloader); the
other target's components are untouched, exactly as if that build
hadn't run at all.

## Current versions

| Component | Version | Source of truth |
|---|---|---|
| Main board application firmware | 1.1.8 (incremental) | `src/F303-master/firmware_common.h` |
| Main board bootloader | 1.2.7 (incremental) | `src/F303-master/boot/bootloader_common.h` |
| Expansion slave application firmware | 1.0.7 (incremental) | `src/F303-slave/slave_common.h` |
| Expansion slave bootloader | 1.1.0 (incremental) | `src/F303-slave/boot/slaveboot_common.h` |

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
