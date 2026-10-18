# Changelog - URTC (repo-wide index)

## [Unreleased]

- The three scale-search loops of the MLX90641 parameter extraction (master and slave copies) are bounded like the MLX90640 ones (MLX90641_SCALE_ITER_MAX), so a blank EEPROM dump no longer hangs the chip until the watchdog resets it at every start.
- The fault recorder record (version 2) now ends with its checksum, which covers every byte before it: the old layout put the checksum in the middle of the record and computed it over a field that was not yet set and without the last byte, so no host could validate it. The record layout, the 0x1A8 / 0x1A9 readback, the memory map of the F-RAM, the watchdog of every actuator (and the actuators that have none), the start of the bootloader listening window and the real flash regions of the bootloaders are documented in docs/CANBUS.TXT, docs/EEPROM.TXT, docs/CAN_THREAT_MODEL.TXT and docs/FIRMWARE_SIGNING.TXT.
- tools/run_host_tests.py is the single list of host C suites (12 today, 8 of them under the address and undefined-behaviour sanitizers); build_firmware.sh and build_firmware.bat both call it, the .bat through WSL on Windows, and a test fails when a tests/test_*.c suite is missing from the list.
- Comments that contradicted the code were corrected (clock source and I2C timing, pin numbers, FPU, timer alternate function, removed files) and the READMEs describe the thermal control as on/off with hysteresis, not PID, and SMART-RACK as a storage rack for tool heads.
- The master's CAN bridge to the expansion slave (0x220 write, 0x221 read) now relays only a closed list of application registers with their exact payload lengths (src/common/urtc_slave_regs.h, documented in docs/CANBUS.TXT); REG_ENTER_BOOTLOADER and every unlisted register are dropped. Covered by tests/test_can_slavebridge.c under the sanitizers. This is source only: no firmware image was rebuilt and no firmware version changed, so it takes effect with the next firmware build.
- tools/publish_release_assets.py refuses a set whose application images are signed with the public development key as an official release; with --allow-dev-key it prints the command with --prerelease. The existing 0.3.5 release is now marked as a pre-release because it carries that key.
- docs/FIRMWARE_DISTRIBUTION.md describes how the flasher and the web studio read the release assets today and how bench pre-releases are reached by tag.
- The CI workflow and the issue and pull request templates carry the URTC name, and the variable that switches the build and run scripts to their non-interactive mode is URTC_CI.
- Release signatures: tools/release_sign.py signs and verifies the assets of a release with an Ed25519 key (`<asset>.sig`, the signature of `urtc-rel/1|<file name>|<sha256>`), tools/publish_release_assets.py --sign writes them, and an official release needs the signature of every asset while a pre-release may go out without. The public key and the plan for leaving the shared development key are in docs/RELEASE_SIGNING.md; the flasher and the web studio verify the signatures they find.
- docs/FIRMWARE_DISTRIBUTION.md says how to refresh the firmware copies bundled with the flasher and the web studio from a local build.

## [0.3.5] - Firmware PATCH number over CAN and release-asset firmware distribution

- Components: main application 0.3.5, main bootloader 0.3.9, slave application 0.2.0, slave bootloader 0.2.3.

- The firmware PATCH number now travels over CAN, so a flasher can check major.minor.patch (until now only major and minor travelled). 0x7F9 and 0x7F4 already use all 8 bytes and released flashers read 0x7F6 and 0x7FA only at their exact length, so none of those is lengthened. Instead: a new companion frame 0x7E9 (installed application patch and capability flags), sent by the application and by the main bootloader right after 0x7F9; the high byte of the minor field of the end-of-update frames 0x7F4 and 0x214 may carry 0x80 | patch (always 0 in a legacy frame); and the slave version answer 0x218 grows from 2 to 5 bytes. A board that predates it never sends 0x7E9 and a flasher that predates it ignores it, so old flashers keep working against new boards and new flashers against old boards.
- Anti-rollback in both bootloaders compares major, then minor, then patch, and the patch only when the installed and the incoming version both carry one, so an image declared without a patch is never rejected against the same major.minor. The metadata page layout is unchanged (the minor word keeps the patch in its high byte; every number reported to a host uses the low byte only).
- The slave link gets a new read register, REG_QUERY_VERSION_EXT (0x08), starting with a marker byte that is never a status code; the main board reads one byte first and only then the rest, so a slave bootloader without it keeps being answered exactly as before.
- New src/common/urtc_version_wire.h (pure C, shared by the bootloaders and the application) with tests/test_version_wire.c: pack and unpack round trips, legacy decoders fed the new frames, rollback ordering. build_firmware.sh runs it in step 1b under the address and undefined-behaviour sanitizers. docs/CANBUS.TXT documents the frames and gives the host contract the flashers will follow.
- Firmware binaries (bin, elf, hex, sha256 sidecars and the generated manifests) are no longer versioned in git; they are published only as assets of the GitHub release of each version. build_firmware.sh and build_firmware.bat write them to firmware/ (git-ignored) in release mode and to build/firmware in CI mode.
- New tools/publish_release_assets.py checks the set in firmware/ (digests, versions, signatures present) and prints the exact gh release create command with the files to attach; tests/test_publish_release_assets.py covers it. docs/FIRMWARE_DISTRIBUTION.md lists the assets, the download URLs and what each consumer has to change to read them. The READMEs (8 languages) describe the new flow.
- Verified: host logic tests under the sanitizers, Python tests, the full ARM build of the four components and the repository validation. Not verified: the new frames on a physical board, the slave I2C link with a real expansion chip, and a flasher exercising the patch extension.

## [0.3.4] - Firmware version boundaries and side-effect-free CI builds

- Components: main application 0.3.4, main bootloader 0.3.8, slave application 0.1.9, slave bootloader 0.2.2.

- The firmware version scripts keep their own three-component odometer (0.7.9 -> 0.8.0, 0.9.9 -> 1.0.0, 9.9.9 -> 10.0.0) and no longer hand over to the shared manifest utility, which broke the build at the boundary versions; the successor rule is now a pure function covered by boundary tests that run in CI and as a step of the build script.
- CI builds (URTC_CI=1) publish firmware images under build/firmware instead of firmware/, so a validation build leaves the tracked images untouched.
- The firmware manifest and the per-image sidecars take their timestamp from SOURCE_DATE_EPOCH or, in CI, from the last commit date, so regenerating them gives identical files; generate_manifest.py accepts an optional output directory. Tests cover the timestamp rules.
- Verified: unit tests and shell syntax checks only; the ARM firmware build itself was not run.

## [0.3.3] - Lifetime thermal counters, hardened slave MLX drivers, CAN threat model

Components: main application 0.3.3, main bootloader 0.3.8, slave
application 0.1.9, slave bootloader 0.2.2.

- **Lifetime thermal counters** (`thermal_stats.c/.h` pure logic,
  `firmware_thermal_stats.c` F-RAM/CAN side): thermal faults ever latched,
  the last fault code and the total heater-on time, in their own CRC-8
  record at F-RAM address 192 (not inside `SavedState_t`, so no
  `SAVEDSTATE_VERSION` bump and no lost settings). Saved at once on a
  fault and at most once a minute otherwise; read with `0x1AA` -> `0x1AB`.
  `tests/test_thermal_stats.c` (new) runs in `build_firmware.sh` step 1b.
- **Slave MLX9064x drivers hardened**: the expansion slave's copies of
  `MLX90640_API.c/.h` and `MLX90641_API.cpp` lacked the bounded data-ready
  wait the main board's copies have (and, for the MLX90640, the scale-loop
  iteration cap; the MLX90641 loops only got it later), so a
  stuck sensor or a blank EEPROM dump could hang the slave until its IWDG
  reset it. Both chips now compile the same files, and
  `check_shared_drivers.py` (new, build step 1c) fails the build if they
  ever drift apart again.
- **`docs/CAN_THREAT_MODEL.TXT`** (new): what any node on the bus can do,
  and the installation requirements (hardware interlock independent of the
  MCU, closed bus, private signing key). Linked from `SECURITY.md`.
- READMEs (all 7 languages): new safety/signing/CAN section, binary names
  updated to the current versions.

## [0.3.2] - Thermal runaway guard, signing keys out of the tools, reset cause

- **Thermal runaway protection** (`thermal_guard.c/.h`, new, pure C) for the
  T12/hot-air loop and the 3D hotend loop in `firmware_control_thermal.c`:
  MAXTEMP on the MEASURED value (449 C T12/hot air, 310 C hotend), a heating
  watch (the temperature must really rise while the heater is on and far
  below target - catches a loose thermistor/tip reading room temperature)
  and a holding check (once at target, a sag beyond the hysteresis that
  lasts the whole hold period with the heater on). Any fault cuts the
  heater, latches `system_error_flag` and records why in
  `thermal_fault_code`. `tests/test_thermal_guard.c` (new) drives it with a
  simulated heater/block/loose sensor: healthy heat-up and hold, setpoint
  changes, a heavy soldering load and a HAL tick wrap never trip it; a loose
  sensor while heating or while holding is always caught within its window.
  Wired into `build_firmware.sh` step 1b (`build_firmware.bat` runs the host
  suites only since the shared runner of the next release).
- **`0x111` grows to DLC 6** (backward compatible): byte 4 =
  `thermal_fault_code`, byte 5 = `last_reset_cause` (RCC reset flags latched
  at boot, bit0 = the IWDG fired). See `docs/CANBUS.TXT`.
- **Black box**: each sample's `flags` now carries the thermal fault code in
  bits 2-3 (no record layout change).
- **Signing keys**: the bootloaders take their HMAC key from
  `URTC_SIGNING_KEY_FILE` at build time (`firmware_signing.py`, new); without
  it they build with the PUBLIC development key and say so, and
  `URTC_RELEASE=1` refuses that. Every build now publishes `<bin>.sha256` and
  `<bin>.manifest.json`, with the precomputed `hmac_sha256` for the two
  application images, so URTC-FLASHER and URTC-WEB-STUDIO only transport a
  signature and no longer carry the key. See `docs/FIRMWARE_SIGNING.TXT`
  (also the measured flash budget and the plan for Ed25519).

## [0.3.1] - Real host-native test harness for the general CAN dispatch table

- **`firmware_can_dispatch_routing.c/.h`** (new) - the tool-ID -> handler
  routing decision from `firmware_can_dispatch.c`'s own `HAL_CAN_RxFifo0MsgPendingCallback`
  switch, extracted into a pure, HAL-free function. `tests/` previously
  covered the MLX90640 sensor API on host but nothing exercised this
  routing table at all - a mistake in it (a tool silently routed to the
  wrong handler) had nothing to catch it before it reached real hardware.
  `firmware_can_dispatch.c`'s own switch is unchanged behavior, just
  restructured to call through this table; a `_Static_assert` block keeps
  the pure table's hardcoded tool-ID literals honest against the real
  `ToolMode_t` enum, failing the real on-target build loudly if they ever
  drift.
- **`tests/test_can_dispatch_routing.c`** (new) - drives every real
  `ToolMode_t` value (0-25) plus deliberately-unassigned IDs (26-31)
  against the extracted table on host `gcc`, no F303 board needed. Wired
  into `build_firmware.sh`'s existing host-test step alongside the
  MLX90640 suite.

## [0.3.0] - Black-box CAN readback, MLX9064x host tests, and a hang fixed

- **Black-box fault-recorder CAN readback** (`firmware_blackbox.c`,
  `firmware_can_global_pre.c`) - the F-RAM capture mechanism itself
  (last ~10s of samples before a fault, flushed once per fault
  occurrence) shipped earlier with readback deliberately left for a
  separate pass. New global command pair `0x1A8` (request) / `0x1A9`
  (response), right after the existing `0x1A6`/`0x1A7` sensor-variant
  query: `rxData[0]` selects a 32-byte chunk (22 chunks cover the full
  ~678-byte record), answered as 4 consecutive 8-byte CAN frames on
  `0x1A9` - the exact same chunked-transfer convention
  `firmware_can_thermalinspection.c`'s own `SendChunkFrames()` already
  established for its oversized MLX9064x pixel chunks. A pure, raw
  export: no magic/checksum validation on the way out (a host
  reconstructs `BlackBoxRecord_t` from the concatenated chunks and
  validates it itself), and it never mutates the ring buffer or the
  flushed record. An out-of-range chunk index or a failed F-RAM read
  both mean silence, same as an unconfigured-sensor chunk request
  elsewhere in this firmware. Verified with a real `arm-none-eabi-gcc`
  build (master target, 17/17 steps passed).
- **Host logic tests for the MLX9064x sensor API** (new `tests/`). The
  Melexis `MLX90640_API.c` is portable C (it includes `<math.h>` and the
  I2C driver header, never the STM32 HAL), so this project's own
  additions to it can now be exercised on the host with plain `gcc`,
  against a fake I2C driver, with no F303 board attached:
  `tests/test_mlx90640_api.c` + `tests/fake_mlx90640_i2c.c` +
  `tests/test_runner.h`, wired into `build_firmware.sh` as step "1b"
  (skipped cleanly if no host C compiler is present). 9 assertions:
  `MLX90640_SynchFrame` / `MLX90640_GetFrameData` on a stuck bus return
  the bounded-poll `DATA_READY_TIMEOUT_ERROR` after a bounded number of
  reads (never spin), a real I2C NACK mid-poll is surfaced as itself,
  the happy path returns a subpage number, a `0x7FFF` frame word is
  rejected as `FRAME_DATA_ERROR`, `MLX90640_DumpEE` pulls the full
  832-word window, `MLX90640_GetSubPageNumber` is a pure accessor, and
  `MLX90640_ExtractParameters` on an all-zero EEPROM RETURNS a defined
  bad-pixel error instead of hanging. Verified compiling and passing
  under `gcc 14.2 -std=c11 -Wall -Wextra` and re-run through the full
  `arm-none-eabi-gcc` firmware build.
- **`MLX90640_ExtractParameters` no longer hangs on a degenerate EEPROM
  dump.** The vendor scale-normalisation loops
  (`while (temp < K) { temp *= 2; scale++; }` in
  `ExtractAlphaParameters` / `ExtractKtaPixelParameters` /
  `ExtractKvPixelParameters`) never terminated when `temp` was exactly
  `0` - every value in a corrupt or unpopulated `DumpEE` reads as `0`,
  and `0 * 2` stays `0` below the threshold forever, so a bad read at
  sensor bring-up hung the F303 the same way the data-ready polling used
  to. New `MLX90640_SCALE_ITER_MAX` (64) caps each loop far past any
  real calibration EEPROM's own convergence (well under 20 steps), so
  the maths is byte-identical for a real sensor; a garbage dump now
  falls through with zeroed params and `ExtractDeviatingPixels` (run
  last) still returns the honest `-MLX90640_BROKEN_PIXELS_NUM_ERROR`.
  On-target thermal-accuracy validation against a real MLX9064x head is
  still pending hardware, but the maths for a valid dump is unchanged by
  construction (the cap is never reached).
- **`build_firmware.sh`** - the manifest regeneration step (step 8,
  `firmware_manifest.json`) was skipped whenever `URTC_CI=1` (the
  mode `tools/build_test.py` always runs under), but the earlier cleanup
  step deletes that same file unconditionally on every run. A CI/
  build-test run therefore left a previously-committed real
  `firmware_manifest.json` permanently missing from the working tree.
  Regenerating it just describes the artifacts the run itself built -
  not a versioning or CHANGELOG mutation, so it now runs regardless of
  CI mode. Verified with 2 real WSL/arm-none-eabi-gcc builds: the
  manifest now regenerates correctly under `URTC_CI=1`, and the
  4 real firmware/bootloader binaries rebuild with consistent, plausible
  sizes (no corruption reproduced in this pass).
- README (all 7 languages) still titled itself "(v0.2)" and referenced
  `URTC_MAIN_FIRMWARE_v0.2.9.*` in its flashing walkthrough - both stale
  against this same `[0.3.0]` release. Corrected to "(v0.3)" and
  `URTC_MAIN_FIRMWARE_v0.3.0.*`, matching `firmware_common.h`'s own
  real `FIRMWARE_VERSION_MAJOR/MINOR/PATCH` and this project's own
  manifest. A real build-test confirmed the actual current filename
  (`URTC_MAIN_FIRMWARE_v0.3.0.bin`, produced by a live
  `arm-none-eabi-gcc` build) before this doc fix; the freshly rebuilt
  binaries themselves were discarded afterward rather than committed,
  since regenerating them was never this fix's own goal.

## [0.2.9] - Removed dangling references to private tracking documents

Found on code review: `VERSION_CHECKLIST.txt`,
`check_version_consistency.sh` and `src/F303-master/firmware_can_electromagnet.c`
referenced three private tracking documents by name - none of
these files exist anywhere in this repo (confirmed - not even untracked on
disk), so these were dangling pointers to private tracking documents, not to
anything a reader could actually open. `VERSION_CHECKLIST.txt`'s own stale
checklist ITEMS for those documents are removed outright
(there is no such file to check on a version bump); the real technical
reasoning in the firmware comment (why the electromagnet has no comms-loss
watchdog) is unchanged and stays self-sufficient without the dangling pointer.

Also carries a `urtc.project.json` maturity correction
(`production` -> `functional`) that had been sitting uncommitted from
earlier work, unrelated to this fix, bundled here rather than left
stranded.

Verified with `bash check_version_consistency.sh` (0 failures, same 4
pre-existing warnings as before this change - missing SVG assets and
`bump_bootloader_version.py`, neither related to this fix).

## [0.2.8] - fixed after a code review

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

- Build version synchronized with `urtc.project.json` and the repository-native version source.

## [0.2.6]

- Build version synchronized with `urtc.project.json` and the repository-native version source.

## [0.2.5]

- Build version synchronized with `urtc.project.json` and the repository-native version source.


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
