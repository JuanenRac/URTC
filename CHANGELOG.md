# Changelog

All notable changes to this project are documented here.

## [Unreleased]

- **The multinode node is held to shared scenarios:** `tests/vectors/multinode_scenarios.h` (written by URTC-COMMON from `conformance/multinode_scenarios.json`, whose expectations are typed by hand from the design) carries 14 scenarios of one node (listening, hearing another node, selection and its lapse, a board on the same address, safe stop, the node command, discovery spacing, a reported fault, the wrap of the millisecond counter); the `multinode` suite replays them against `multinode.c` step by step. The same file is replayed against the Python model of the nodes in URTC-COMMON.
- **The bootloader answers the readback and the downgrade authorisation only in the session the application started:** when the application accepts `0x7F0` it leaves a mark in a backup register of the RTC block; the bootloader reads it once at start-up, clears it, and ignores `0x7FE` and `0x7FD` in any other session while a valid application is installed (a board with none keeps both, for its recovery). `URTC_BOOT_RESTRICT_UNSAFE_COMMANDS=0` at build time answers them in every session again. The decision is `src/common/urtc_boot_session.h`; `tests/test_bootloader_boot.c` plays a power-up, a session started by `0x7F0` and a board with no application, and `tests/test_can_global_pre.c` checks that only an accepted `0x7F0` leaves the mark. The backup register is to be confirmed on a board.
- **The slave application sends its patch number:** the tenth byte of `REG_APP_VERSION`, reserved until now, carries `FIRMWARE_VERSION_PATCH` (1 to 127). The master reads the whole register for `0x221`, answers with the same bytes as before and, when that byte is a patch number, follows with the new `0x222` companion frame (register `0x01`, patch); a byte of 0, what an older slave application sends, is ignored and no frame follows. The bootloader of the slave already reports the patch of the application it holds in its extended version register (`0x218`). Host suites: `slave`, `can_slavebridge`, `version_wire`.
- **The reference half of `docs/CANBUS.TXT` is generated from the specification in URTC-COMMON:** the index of the identifiers, the field layouts with their examples, the tool profiles, the enumerations, the constants, the magic payloads, the registers of the expansion slave, the procedures and the multinode frames now sit between marker lines at the end of the document (`tools/generate_canbus_doc.py` of URTC-COMMON writes them; the digest of each block is in its marker). The prose above them is untouched and stays hand-written. `tests/test_canbus_doc.py` fails when a generated block is edited by hand, and the shared tests of URTC-COMMON fail when a block is not what the specification produces, so a new identifier reaches the document by one run of the tool.
- **Every firmware source is now compiled by a host suite (13 new suites, 38 in all; 95 percent of the lines, up from 88):** the bit-banged I2C and SPI of the expansion connector and the ADS1115 behind a model of the bus that follows START, STOP, the clocks and the acknowledge (`expansion_bus`); the status LED and the camera ring, with the WS2812 stream checked for all 256 values (`led`); the OLED driver, the font and every tool screen, read back from a model of the display (`oled_render`); the interrupt handlers and the safe state of the multi-head module (`interrupts`); the clock tree, CAN bit rate, I2C timing, SPI clock, timers, PWM frequencies, ADC and the level of every pin per tool (`init`); the three thermal-camera transports, the MLX90642 driver and application on a device model, and the MLX90640 driver beyond its start-up (register round trips, trigger, frame validation, broken-pixel correction in both modes, a synthetic calibration whose temperatures are finite, monotonic and reproducible) and the MLX90640 and MLX90641 applications (`mlx_bus`, `mlx_api`, and the same two on the copies the expansion slave carries); the expansion slave end to end (`slave`: PWM, the I2C link registers, the converter and the cameras, the start-up and the idle shut-off); the main of both bootloaders (`bootloader_boot`, `slaveboot_main`); and the real main of the application run from the power-up for all 25 tools (`firmware_main`: reset cause, tool identification, boot sequence, telemetry frames, the bus-error recovery, the faster blink on a latched error, the AOI strobe, the night mode, the status LED). The CI floor goes from 80 to 90. Nothing in `src/` changed, no firmware was built and none of this has run on a board.
- **The fake HAL grew with it** (`tests/host_hal/`): pins with hooks on every read and write, I2C master and slave transfers, the SPI with DMA, the interrupt entry points, the start-up structures of the clocks, CAN, I2C, SPI, DMA, timers and ADC with the constants the arithmetic needs, a watchdog hook that lets a test act in the middle of a main loop, and `fake_i2c_bus.c`, a device-level model of the expansion I2C (a register file, a 16-bit register file, the word memory of the MLX90640 and MLX90641 and the opcode protocol of the MLX90642) that answers the bit-banged bus and the hardware I2C alike. `run_host_tests.py` learned two things for them: a suite may rename the `main()` of a source (`-D`) and may switch off one sanitizer check (the one the Melexis driver trips with a left shift of a negative value).
- **Fixed:** the 5x7 font of the application gets its missing `/` glyph (and shapes for the comma and the minus), so every glyph sits at the place of its own character: `0` is drawn as a 0 and `A` as an A; the small font of the bootloader draws a real `0`; the double-size text of the bootloader display lands at the column asked for (and is cut at the edge of the line instead of wrapping), and its progress bar fills up to the frame at 100 percent. **Still to confirm on a board:** both the expansion slave and its bootloader start a receive when the HAL announces `I2C_DIRECTION_RECEIVE` and a transmit otherwise, which is the opposite of the master's point of view the HAL documents; and the version answer of the slave application leaves its tenth byte (reserved) at zero instead of the patch number, which needs a decision about the contract with the master.
- **The README files carry sections written once in URTC-COMMON:** "Compatibility", in the eight languages, between marker comments before the license heading (`tools/readme_sections.py` of URTC-COMMON writes and checks them; the rest of each README is hand-written as before).
- **A static check on every push:** `ruff.toml` (unused names, exception chaining and syntax errors, the rules the flasher and the tester already ran) and a step of the CI that runs `ruff check .` with a pinned ruff; it found two re-raises inside `except` without a cause (`release_sign.py`, `generate_manifest.py`, now `from None`) and an unused import in a test, fixed; the unused font variables of the documentation image generator are left as they are.
- **Host tests for the logic around the hardware: 8 new suites and a coverage floor.** Compiled against the fake HAL (extended with the ADC and a `fake_state.c` that defines the shared state of `firmware_common.h`) and run under the sanitizers and `gcc -fanalyzer`: `can_tools` (the 13 tool handlers: lengths, the boot-sequence gate, clamps, timer scaling, the direction-change rule of the steppers, the replies), `control_thermal` (the heater loops: ADC mapping, open and shorted sensors, ceilings of 445 and 300 C, hysteresis, stuck heater, the three guard faults, the thermistor table, the PID switch), `blackbox` (the recorder replays the scenarios URTC-COMMON writes from its own model and the 678 bytes in the F-RAM must match, with the CAN readback chunk by chunk), `identify` (jumpers 0..31, the free configuration record, the inert state), `can_global_post` (SPI passthrough, diagnostic line, the erase with its magic payload, the LED override), `sensors_inspection` (telemetry, the debounced end-stop, the thermal camera routing and its 32-byte chunks, the lifetime thermal statistics). 25 suites in all (two more for the bootloader of the expansion slave, below); the suites that keep static state run each scenario in a child process.
- **The bootloader of the expansion slave is host-tested too:** `slaveboot_crypto` (the same RFC 4231 and padding-boundary vectors as the main board's) and `slaveboot_update` (the update as the I2C registers drive it: start, expected HMAC, data in pieces of 1 to 32 bytes, end; size and hardware-id validation, CRC32, signature, the five rollback cases, flash errors at the erase, the program and the copy, the resume of an interrupted copy, the adoption of an image with no metadata, the two version answers). 97 percent of `slaveboot_flash.c`, 84 percent of `slaveboot_protocol.c`.
- **Fixed in the slave bootloader:** `HandleStartUpdate` now parses the START into locals and validates it before it touches `update_total_size` and the hardware id, as the main board's bootloader does: a rejected START mid-transfer leaves the transfer underway intact and it still completes.
- **`tools/run_host_tests.py --coverage-floor N`** builds the suites with gcov counters and fails under that share of executed lines of the firmware sources they compile (95 percent measured over all 78 sources); `build_firmware.sh` and `.bat` pass `URTC_COVERAGE_FLOOR`, and the CI sets 90.
- **Fixed:** in the inert state (no tool, or an unreadable free configuration) the pin PB6, shared by the stepper enable and the laser interlock, is written once and ends high: the stepper driver is disabled. Before, it was written high and then low, which left the driver enabled. The laser stays off because nothing raises its power without the laser tool selected. The start-up pin configuration agrees.
- The `TEST_ASSERT` macro of the host tests comes from URTC-COMMON (`tests/vectors/urtc_test.h`, copied with `vendor_copy.py --set firmware_test`) instead of being written again in `tests/test_runner.h`; the suites are unchanged.
- The CI holds the build tooling tests to a coverage floor of 35 percent of `tools/` (measured 43 on a machine without the repositories next to this one).
- The two companion links that do not travel on the CAN bus are documented as big-endian in `docs/CANBUS.TXT` (version 2 of the rack serial frame and of the vision head sensor frame; version 1 was little-endian and is refused), and the shared vectors copied into `tests/vectors/` are the current ones.
- The detached signatures of the firmware images (`firmware/*.sig`) are ignored by git like the images they sign: they are release assets, and the old ones that were tracked are no longer kept in the repository.
- **The firmware is held to the shared vectors:** `tests/vectors/urtc_fw_vectors.h`, written by URTC-COMMON from its conformance vectors, is compiled into three host suites: the minor and patch fields and the version ordering (`urtc_version_wire.h`), the payload that resets into the bootloader on `0x7F0` (and that no other payload of the vectors does) and the payload that authorizes a downgrade on `0x7FD`; before, only the three programs read those vectors.

## [0.4.0] - Optional functions behind switches, health frame, heater PID and a virtual board

- **Optional firmware functions, each behind a switch that is off by default** (`0x1AE` writes it, `0x1AF` reads it; it acts after the next power-up and is kept in its own F-RAM record, address 976): a trapezoid and S-curve planner for the stepper (`motion_profile.c`; the `0x120` frame may now carry a cruise speed and a profile, and the step generator does not use the plan yet, which needs a timing bench); a health frame once a second (`0x1B8`: CAN error counters, bus-off count, oscillator trim, longest main-loop pass, and chip temperature and supply when the platform provides them); a trim of the internal oscillator against the CAN bit time (`hsi_trim.c`; the measurement and the register write need a bench); and a PID with anti-windup and relay autotune for the soldering iron and hot-air heaters (`pid_control.c`, gains in F-RAM address 992, frames `0x1B9` and `0x1BA`; the 3D printer hotend keeps the bang-bang control). They add about 7 KB of code, so check the free flash when a release is built. Tested on the host, the PID against a simulated tip; nothing has run on a board.
- **A virtual board** (`tests/virtual_board.c`): the real receive callback, routing table, global handlers before the error gate, fault recorder, thermal counters and optional functions, compiled for the host with the address and undefined-behaviour sanitizers behind an SLCAN-style text port. `tools/fuzz_virtual_board.py` sends it the valid, invalid and random frames made from the protocol specification by URTC-COMMON (`tests/fuzz/fuzz_corpus.txt`) for seven combinations of tool and optional functions, and checks that it never stops, that it only transmits identifiers a board may send and that it resets into the bootloader exactly when it receives the magic frame. It runs in the unit tests of the CI.
- **`tools/keys.py` and `docs/KEYS.md`**: the keys of the URTC family in one place (public values and where each is embedded, never a secret) and `keys verify`, which fails when the public key embedded in a program or a bootloader differs from the registered one; the CI runs it (repositories that are not checked out beside it are skipped; run it with `--strict` before a release).
- The notes of a release carry a compatibility table (protocol, firmware and programs) taken from URTC-COMMON: `tools/changelog_section.py VERSION --compat`, used by the release job.
- `docs/CANBUS.TXT` and `docs/EEPROM.TXT` describe the new frames and the new F-RAM records.
- The README lists URTC-GATEWAY, the new gateway in front of the CAN bus, among the related projects.

## [0.3.9] - The first official release: signed images, a protected release job and cleaner host tests

- The host tests started through WSL no longer put their binaries inside the repository: `tools/run_host_tests.py` translates a Windows `--build-dir` for WSL and refuses one on a Linux host; the output folder named like a Windows path that had been versioned by mistake is removed from git, and such names are ignored.
- `tools/ci_validate.py` fails when a versioned file has the name of a Windows path (a colon, the character a file system uses to store one, or a first folder named like a drive or a user profile): a path handed to WSL without translation once put build output inside a repository. The same check (`tools/_tracked_names.py`) is in every URTC repository.
- **The protected release job** (`.github/workflows/release.yml`): started by hand with the tag, only from `main`, inside the `release` environment (required reviewers; the keys are secrets of that environment and are readable only after the approval). It checks the tag is the version of the commit and not published, runs the baseline, builds the firmware with the production bootloader keys, signs every asset with the key of the releases, publishes an official release with the notes of the CHANGELOG section (`tools/changelog_section.py`), downloads what it published and verifies it, and removes the key files at the end. The keys go only into environment variables of one step and files of mode 600; tests keep the file from echoing a key, from using a trigger other than the manual one and from using an action that is not pinned by commit.
- **Fixed: an official release could never be published.** The build writes `signing_key_kind: "release"` for a key read from `URTC_SIGNING_KEY_FILE`, but `tools/publish_release_assets.py` accepted only the name `production`, so a set built with a real key was refused as official. Both names are accepted now; `dev` and anything else are still refused. `--list-file` and `--tag-file` write the assets and the kind of release for a job that publishes them.
- `docs/RELEASE_SIGNING.md` explains how to publish an official release from your own machine (no secret in GitHub) or from the protected job, and the creation of the bootloader keys (`~/.urtc-keys/signing_keys.txt`, next to the key of the releases). The flasher and the web studio no longer keep a copy of the images in git: they read the release and verify it.
- `docs/CANBUS.TXT` reserves the identifiers `0x260`-`0x26F` for URTC-SMART-RACK (command in eight bytes, big-endian); nothing in this firmware answers them.
- The `build-test` scripts use the interpreter named by `URTC_PYTHON` (which URTC-UPDATER sets to its own) and, without it, the first `python3` or `py -3` found.
- The dashboard notice is sent only after the CI of the push finished green (`workflow_run`).
- The banner of the README is the animated ecosystem banner (`images/URTC_BANNER.svg`).
- The bootloader linker script and the manifest agree on 30 KB of code (the last 2 KB of its slot hold the boot metadata), and a test compares them with the metadata address.
- The firmware manifest records the SHA-256 of the `.hex` and `.elf` images as well as the `.bin`, and `.gitattributes` keeps the images binary so a line-ending conversion cannot change a digest.
- `docs/CANBUS.TXT` says 25 tool profiles, the number the protocol defines.
- The README of each language lists URTC-UPDATER among the related projects.
- **Static analysis in the host tests**: after the logic suites pass, `tools/run_host_tests.py` compiles every firmware source they use once more with `gcc -fanalyzer` (null dereference, use after free, double free, leaks, out-of-bounds writes) and fails on any warning; the 15 sources are clean today. It runs in `build_firmware.sh`, `build_firmware.bat` (through WSL) and therefore in CI, is skipped with a note when the host compiler is not gcc 10 or newer, and `--no-analyze` turns it off. Three tests cover which files are analysed and with which flags.
- The CI workflow no longer carries the steps for languages the repository does not use, and installs the newlib packages the firmware build needs.
- The manifest declares how far the project has been shown to work (`evidence`: host-test) and `tools/ci_validate.py` checks the value.

## [0.3.8] - Several tool heads on one CAN bus, behind a switch that is off by default

- Main application 0.3.8 implements the node side of the multinode extension (URTC-COMMON `docs/MULTINODE.md`): the node address from the five ID jumpers, discovery (`0x400` answered with `0x420` plus the address), `SELECT` (`0x401`) with a 2 s lapse, a status frame every second (`0x440` plus the address), `SAFE_STOP` (`0x402`) and a read-only `NODE_CMD` for the status query. Only the selected node obeys the single-head identifiers (the bootloader entry and the version query included), sends single-head telemetry or actuates. Two boards on one address are detected, put their tool in the safe state and refuse commands. A node that hears nobody for 1.5 s after start behaves as the one head on the bus, so a host that does not know the extension keeps working with a single head.
- The extension is OFF by default. The switch is a persistent record of its own in F-RAM at address 960 (magic, version, enabled, instance, CRC-8; documented in `docs/EEPROM.TXT`), read once at boot: a blank, erased, zeroed, damaged or missing record reads as off, `SavedState_t` is unchanged and the erase command does not touch it. It is changed with the new `0x1AC` (four-byte magic, enable, instance), read back with `0x1AD`, takes effect at the next power-up and, while the extension is on, is obeyed only by the selected node. `docs/CANBUS.TXT` describes both frames and has an appendix on the extension.
- With the switch off the receive path is the single-head path: the new hook returns at once, the telemetry gate is always open and nothing is sent. `tests/test_multinode.c` (a thirteenth host suite, under the address and undefined-behaviour sanitizers) proves it for all 2048 identifiers and five ways of being off, and covers listening and legacy mode, selection and its lapse, the rule that an unselected node of every tool profile lets no actuation, bootloader or configuration frame through, discovery timing, status, address conflict, `SAFE_STOP`, `NODE_CMD`, the enabling command with wrong magic, wrong length and a failing chip, and the millisecond counter wrapping.
- Implemented in the firmware and tested on the host only; it has not been tried on boards, and no host program uses it yet. This is source: a firmware image has to be built to carry it.

## [0.3.7] - Explicit version bump and manifest schema 1.1

- Compiling no longer changes any version: build_firmware.sh and build_firmware.bat only read the component versions. Raising one is the explicit command `python bump.py` (all four components, or the named ones; `--dry-run` and `--list` available), which also mirrors an application version into its bootloader and keeps urtc.project.json in step. bump_version.py gained a read-only `--current` mode.
- urtc.project.json uses manifest schema 1.1: it declares the deployment target `mcu` and the four independently versioned components (main and slave, application and bootloader) with their MCU target and the header that carries each version; tools/ci_validate.py checks that every declared version matches its header.
- Every licence statement and source header says GPL-3.0-or-later.
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

## [0.3.6]

- Build version synchronized with `urtc.project.json` and the repository-native version source.

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

## Earlier history

### Binary filename convention (changed - see each component's own CHANGELOG)

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

### Current versions

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
