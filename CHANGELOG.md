# Changelog

All notable changes to this project are documented here.

## [Unreleased]

- **The bootloader can go back to a valid application by itself after a failed update, behind a switch that is off by default:** after an aborted transfer (a timeout, a bad signature, a refused rollback) the head stayed in the bootloader until somebody switched it off and on, although the application in the main slot was untouched and valid. Building the bootloader with `URTC_BL_RETURN_TO_APP_S=<seconds>` makes it jump to the application after that many seconds with no frame on the bus and no update in progress; the default is 0 and the new code is not even compiled in. The decision is a header with its own host suite.

- **A command that arrives while the watchdog reads the clock no longer cuts the output:** the kick of each communication watchdog is written by the CAN interrupt, which could run between the read of the clock and the read of the kick; the unsigned difference then read about four billion milliseconds, the laser, drill, heaters, fans or lamp were cut and a critical error was latched until the next reset. A kick that is newer than the clock now has age zero; the host tests cover it for every watchdog and across the wrap of the counter.

- **The manual host test workflow measures the coverage of the firmware logic with gcov and fails under 93 percent:** the 58 C suites execute 93.9 percent of the lines of the 83 firmware sources they compile (the floor is the measured value, never above it); an empty floor runs the suites without the measurement.

- **The identity frame declares the bit rate of the bus (protocol version 2):** the master firmware sends the code of the rate it was built for (1 Mbit/s, or 500 kbit/s with `URTC_CAN_LEGACY_500K`) in byte 5 of `0x1BB`, `URTC_PROTOCOL_VERSION` is 2, and the uptime of that frame is 16 bits that stop at 65535 s instead of 24 bits. A tool of the previous protocol version reads the frame with a wrong uptime and no rate; the new tools read both versions. `src/common/urtc_can_ids.h` is the generated header of the new specification, `docs/CANBUS.TXT` is regenerated, and the host test covers the layout, the stop of the uptime and every rate code. The code is built only in the CI; it has not been run on a board.

## [0.4.8] - The bus runs at 1 Mbit/s, the application images carry their own header and the expansion link gets a check byte

- **The bus of the tool heads runs at 1 Mbit/s:** the master application and its bootloader calculate the bit timing (prescaler 2, 13 + 2 time quanta from the 32 MHz clock, sample point 87.5 percent, never below 75 percent, 0 ppm of rate error) from the rate, the sample point and the jump width of `spec/can_protocol.json` (one source), by default; `URTC_CAN_LEGACY_500K=1` builds the 500 kbit/s legacy option for early boards. **Compatibility note:** every board and adapter of one bus must run at the same speed, the trunk should stay under about 25 m at 1 Mbit/s (about 100 m at 500 kbit/s) with short stubs and a 120 ohm termination at each end, and boards built before this change must be rebuilt with the legacy option to stay on a 500 kbit/s bus. Host tests of the timing at both rates; nothing has run on a real bus.

- **A sealed image goes through the twins of both bootloaders:** the image the seal step makes (the header inside it filled in) is signed with the throwaway development key and flashed through the compiled twin of the master bootloader, which installs it and reports the new version, and refuses it with the CRC-32 reason when one byte of the header inside changes; the model of the slave bootloader installs a sealed slave image through the confirmed exchange and refuses the one of the other board. `tools/make_boot_sig_cases.py` writes the sealed case. Run on the host only; nothing has been flashed to a board.

- **The shared document policy check is refreshed:** `tools/_doc_policy.py` and `tools/ci_validate.py` are the current copies, which also fail when a build script changes a version and when a script switches into a virtual environment instead of running its python directly. The firmware build scripts read the version of each header with `tools/read_header_version.py` instead of the bump script, so compiling visibly never changes a version.

- **The master reaches the application registers of the expansion slave through one switch:** the writes and reads of the registers of the slave application (the order to enter the bootloader, the generic frames `0x220` and `0x221`, the paste jetting, flying probe and thermal inspection commands and the choice of the camera) now go through `ExpansionI2C_AppWriteRegister`, `ExpansionI2C_AppReadRegister` and `ExpansionI2C_AppReadRegisterWithParam`, which are the plain calls by default and the calls with the check byte of the link when `URTC_SLAVE_LINK_CRC=1`; the registers of the slave bootloader keep the plain calls. `build_firmware.sh` and `build_firmware.bat` pass `URTC_SLAVE_LINK_CRC=1` to every compile of both boards when the environment variable of that name is set (any other value but 0 is refused); the official builds leave it unset. Tested with three host suites that run the existing tests with the switch on (`can_slavebridge_link_crc`, `can_tools_link_crc`, `sensors_inspection_link_crc`, which also check which family of calls each frame uses) and a source check that no application register is reached with the plain call; the CI also compiles both boards for the target with the switch on. Not tried on a cable.

- **A capture of the thermal camera of the expansion slave is a state machine, not one blocking call:** `src/F303-slave/slave_mlx_capture.h` cuts the capture of the MLX90640 and MLX90641 into its joints (start the measurement, ask whether the frame is ready no more than once every few milliseconds, read the frame, calibrate it) and the main loop does one of them per pass, so the wait for the camera, up to about half a second at its default refresh rate, no longer holds back the watchdog refresh, the PWM tick and the failsafe that stops an output when the link is lost; a camera that is silent for three seconds is reported as an error. The camera libraries are not touched. Tested on the host with the new suite `slave_mlx_capture` (doubles for the four joints and a clock: one joint per step, the poll interval, the timeout, a clock that wraps, every failure) and with the existing slave suite, which now plays the main loop with the clock moving. What one step costs on the real chip (the read of the frame and the calibration are still one library call each) has not been measured.

- **A check byte for the application link of the expansion connector, off by default:** `src/common/urtc_link_crc.h` adds a CRC-8 (polynomial 0x07) to every write and to every answer of the I2C1 link between the master and the expansion slave; the slave built with `URTC_SLAVE_LINK_CRC=1` drops a write whose check byte does not match (the bootloader entry is never executed from a frame that fails it) and counts it in `i2c_link_crc_errors`, and the master has the matching `ExpansionI2C_SlaveWriteRegisterCrc`, `ExpansionI2C_SlaveReadRegisterCrc` and `ExpansionI2C_SlaveReadRegisterWithParamCrc`, which refuse an answer whose check byte does not cover the register that was selected and the data. Without the switch the link is byte for byte what it was. The bootloader of the slave keeps its protocol without a check. Tested on the host with the new suite `slave_link_crc` and the checked calls in `expansion_bus`; not tried on a cable.

- **The address of the application is held to its single source in more places:** the test of the flash layout now also compares the manifest generator, the maps and the readmes of the firmware and the two linker scripts with `src/common/urtc_flash_layout.h`.

- **The application images carry their own header:** 32 bytes at the fixed offset `0x200`, after the vector table, with the hardware, the version in four components, the length and the CRC-32 of the whole image. The firmware emits the slot (`src/F303-master/firmware_image_slot.c`, `src/F303-slave/slave_image_slot.c`), both linker scripts place it and fail the link if the vector table would not end before it, and `tools/seal_image.py` (called by `build_firmware.sh` and `build_firmware.bat`) fills in the length and the CRC-32 and writes the Intel HEX again from the sealed bytes. The flash map, the vector table and the length of the image do not change; the bootloaders are not changed.

- **The check of the signature records also reads the header inside the image:** `tools/verify_image_records.py` refuses a sealed image whose header does not hold or states another hardware or version than its file name; the record itself is made over the sealed file exactly as before.

- **Three host test suites for the header:** the shared vectors against `urtc_image_header.h` and the two slots against the headers of the firmware, with the seal step tested in Python (the vector table untouched, the same bytes every time, a refusal for another board, another version or a binary with no slot).

- **The tree of the repository lists `mypy.ini`.**

## [0.4.7] - The Spanish README says "grabar" instead of the calque "flashear": the same word as the window of the programs. (and 23 more changes)

- **The Spanish README says "grabar" instead of the calque "flashear":** the same word as the window of the programs.

- **The manifest says which version of the shared contracts it needs:** `min_common` in `urtc.project.json` is the oldest version of URTC-COMMON among the copies this repository carries, the value the updater compares with what is installed (a warning only). `tools/min_common.py` of URTC-COMMON checks it and a new copy brings it up to date.

- **A HardFault leaves its registers behind (switch `URTC_CRASH_DUMP`, off by default):** `crash_dump.h` / `crash_dump.c` hold a 48-byte record (magic, crash counter, the PC, LR and xPSR of the stacked frame, CFSR, HFSR, MMFAR, BFAR, the address of the frame, the cause of the reset, a kind and a CRC-32) in a `.noinit` variable that the start-up code does not clear. With the switch on, the fault handler still cuts every output first, exactly as before, and then copies the registers into the record and waits for the watchdog; the application checks the record at start-up (kept after a watchdog or software reset, emptied after a power-on or when it fails its CRC) and a new request, `0x1BC` operation 0, is answered with six frames of `0x1BD` carrying the record byte for byte. A stack pointer outside the RAM gives a record of kind "no frame" with only the fault registers. With the switch off the handler, the linker layout and the identifiers the firmware answers are the ones of today. New host suites `crash_dump` (CRC-32, bytes against a model written with struct and zlib, the capture, every start-up rule, every flipped bit, a fixed-seed fuzz) and `crash_dump_glue` (the handler's capture function and the readback, built with the switch on).

- **More host tests for the bit timing, the weld pulse timing and the slave write reassembly:** limits and cuts of every function (sample points 500 and 900, the largest arguments, every cut point of every fixed-length register, the stale time at the limit and across the wrap of the tick counter), sweeps over the common clocks and rates, and fuzz runs with a fixed seed against a shadow of the expected bytes. The pulse timer now refuses a clock that is not a whole multiple of 10 kHz instead of producing a pulse of the wrong length (the main loop still ends the pulse).

- **The copy of the image record tools is refreshed:** the shared module that builds and reads the signature record of an image is at the current version of URTC-COMMON again.
- **The expansion slave can reassemble a register write that arrives in pieces:** `slave_write_assembler.h` (pure logic) collects the pieces of a link write in order, tells when the write is complete for its register, drops a half-written command after a timeout and counts what it dropped, and never lets the bootloader entry run from an incomplete payload. The link uses it only when built with `URTC_SLAVE_WRITE_REASSEMBLY=1` (off by default: the link behaves as before). Host tests cover short writes, a late piece, overflow and expiry.

- **The CAN bit timing can be calculated, and its clock checked:** `can_bit_timing.h` finds the prescaler and segments of an exact bit rate and sample point from a peripheral clock, gives the oscillator error the setting tolerates (the two conditions of the CAN specification) and a diagnostic that tells whether an internal oscillator or a crystal is good enough. `URTC_CAN_BT_FROM_CONFIG=1` (with `URTC_CAN_BITRATE_BPS`, `URTC_CAN_SAMPLE_PERMILLE`, `URTC_CAN_SJW_TQ`) makes the application and the master bootloader use it; `URTC_USE_HSE=1` (with `URTC_HSE_HZ`) runs the PLL from an external crystal and falls back to the internal oscillator if the crystal does not start. Both are off by default: the fixed 500 kbit/s setting and the internal oscillator stay.

- **The weld and soldering pulse has configurable limits and an optional hardware timer:** `weld_pulse_timing.h` holds the duration clamp (`URTC_WELD_PULSE_MAX_MS`, default 2000, and `URTC_WELD_PULSE_MIN_MS`, default 0; a maximum above the hard limit of 5000 ms does not compile) and the arithmetic of a one-pulse timer. With `URTC_WELD_PULSE_HW_TIMER=1` timer 17 ends the pulse in its own interrupt, whatever the main loop is doing; the loop tick stays as the second way to end it. Off by default, so the timing is the current one.

- **The pins of the I2C bus of the expansion slave sensors come from configuration:** `URTC_SLAVE_I2C2_PORT`, `URTC_SLAVE_I2C2_PINS` and `URTC_SLAVE_I2C2_AF` (or the preset `URTC_SLAVE_I2C2_PRESET=1`, PA9 and PA10) replace the pins that were written in `slave_main.c`; the default stays PB10 and PB11 and a host test checks it.

- **The two bootloaders must carry the same SHA-256 code:** `check_shared_drivers.py` (step of both build scripts) now also compares the SHA-256 core of the master and the slave bootloader once comments are dropped, next to the Melexis drivers it already compared, so a fix made to one copy cannot be forgotten in the other.

- **The clock of each I2C peripheral of the expansion slave is enabled before its init:** in the bootloader and in the application the clock was enabled after `HAL_I2C_Init`, which a chip ignores; the fake HAL now counts an init made with the clock off and the start-up tests assert zero of them.

- **The bootloader and the application of the expansion slave ship together:** both are cut in the same version and must not be mixed with the ones of another version, because the bootloader now listens for 600 ms before it enters an application, takes its interrupt work out of the I2C handler and the application starts its thermal sensor when the master chooses it. The RAM of the slave stays declared as 40 KB of SRAM plus 8 KB of CCM in both linker scripts and the bootloader accepts a stack top in either region.

- **The thermal sensor of the expansion slave is initialised when the master chooses it:** at start-up the variant is always none, so the sensor was never initialised; the write of the variant register now requests an init that the main loop runs, not the interrupt. A host test reads the sensor bus before and after.

- **The thermal capture of a sensor wired to the main board runs in the main loop:** the CAN receive handler only marks it requested (`ThermalInspection_Service` runs it after a watchdog refresh) and a status query answers busy from the request, so the interrupt no longer spends most of a second inside the capture.

- **The hardware configuration survives a change of the saved-state layout:** the expansion board, the free tool, the serial label and the thermal sensor have a record of their own in F-RAM (address 944, magic, version, CRC-8), written when they change, restored when the saved state is of another version, and blanked with it by the erase command; `docs/EEPROM.TXT` has the map.

- **The bootloader of the expansion slave answers the master, listens before it enters an application and keeps heavy work out of the I2C interrupt:** the address callback now arms the receive for a write by the master (the HAL gives the direction of the master, so the two branches were swapped) in the bootloader and in the application of the slave; a valid application is entered only after a listening window (`SLAVEBOOT_LISTEN_WINDOW_MS`, 600 ms, `0` turns it off) and an update that starts inside it is never cut by it; END and the confirmation are only queued by the interrupt and run in the main loop (`QueueEndUpdate`, `QueueConfirm`, `ServiceDeferred`), the 200 ms pause before the reset no longer uses `HAL_Delay` inside the interrupt, and writes are ignored while a step runs. Host tests cover the queue, the pause, the window across the wrap of the millisecond counter, and the model of the slave bootloader reproduces each case with the code before the fix as an option. Not built for the board; the checks on a board stay in the bench list.

- **The master bootloader can take the signed command that rotates the signing key, off by default:** `bootloader_protocol.c` now reads the 15 chunks of `0x7EC` (index with the high bit set), decides them with `urtc_key_rotation_cmd.h`, verifies images only with the keys valid at that moment (the current one, and the previous one during the window), answers operation `05` of `0x7EA` with the status and the ids and announces the valid ids after a version query. The build option `URTC_BL_ROTATION_COMMAND` is `0` by default: then every key of the table verifies as before, the chunks are ignored and operation `05` is not answered. The state is kept in RAM, so a reset goes back to the current key alone. Two new host suites (48 in all) run the same tests with the option on and off, with signed commands made by `tools/make_boot_sig_cases.py`: window, expiry, replay, wrong signer, unknown key, early close, reset and an addressed head that is not selected. Reversible; nothing is built for the board.

- **Host suite for the signed rotation command:** `tests/test_key_rotation_cmd.c` holds `src/common/urtc_key_rotation_cmd.h`, the decision a bootloader takes on the command that starts a rotation of the signing key, to the shared vectors (46 suites). The notes of `0x7EC` in the CAN tables describe the command. No bootloader uses it yet.

- **The copies of the shared CAN tables are refreshed:** `docs/CANBUS.TXT`, `src/common/urtc_can_ids.h` and the fuzz corpus now include the key rotation status frames (operation `05` of `0x7EA` and its seven-byte answer on `0x7EB`). The bootloaders do not send them yet.

- **A host suite for the rotation of the signing key (45 host suites):** `tests/test_key_rotation.c` holds `src/common/urtc_key_rotation.h` (which fingerprints a bootloader accepts and announces while a rotation is in progress, with a configurable window) to the shared vectors of URTC-COMMON. No bootloader uses it yet and nothing is built for the board; the documented suite counters say 45.

- **The documented counters of the host tests match what the CI runs:** the README (all eight languages), the structure picture and the source map now say 44 host suites (40 under the sanitizers, 4 plain) and a coverage of 94.6 percent of the lines of the 81 firmware sources the suites compile (3 of 84 need the board), as measured by the CI.
- **The repository check compares the manifest with what can be computed:** `tools/ci_validate.py` now also runs the shared `tools/sync_manifests.py` offline, which compares the shipped version, the newest released heading of this changelog, the presence of tests and the claimed level of evidence with the manifest. It is part of the shared set of tools, so the copy is pinned by the lock.
- **A host model of the bootloader of the expansion slave, `tools/slave_bootloader_twin.py`, and its tests:** the master's bootloader has a twin that is its own C compiled for the host; the slave's cannot be built whole that way (its handlers sit on the HAL of the I2C peripheral), so this one is a Python model written from `src/F303-slave/boot/*.c`: the register protocol, the update states, the confirmation by the master, the rollback floor, the append-only metadata log, the resume of an interrupted copy, the ten-second silence rule and the jump rules, over a flash that only clears bits until a page is erased and that can lose the power at any operation. `python tools/slave_bootloader_twin.py --check` runs the scenarios (a confirmed update; a flipped CRC, a wrong digest, a missing chunk, a foreign board; no confirmation, a wrong tag, a replayed one, one without the counter, an image altered after the check; a lower counter, an older version, an older patch; a silent transfer; about 400 power cuts spread over a whole update, none leaving a broken application or a lower floor) and prints `SLAVE_TWIN=PASS`. No compiler is needed, so it runs on any PC and is a named step of the CI. `tests/test_slave_bootloader_twin.py` holds the model to the source: the layout, the registers, the status codes, the reasons, the versions, the timing and the buffer size are read back from the C files, so a change in the firmware makes the model fail until it is updated.
- **The model reproduces four properties of the slave bootloader as it is built, so a bench test can confirm or fix them:** the address callback arms a receive on the HAL direction constant that describes the master reading, so over a real HAL a write by the master is answered as a read and no register is reachable (`direction_fixed=True` models the inverted test); the design assumes the receive-complete callback fires on a STOP before the 33-byte buffer is full, and if the HAL reports it as an error (`short_write="hal_error"`) the register pointer, START, END and the counter are lost; a valid application is entered at once after every reset, so the bootloader is not reachable on a chip that holds one (`listen_window_ms` models the window a fix would add); and the update steps (erasing 27 pages, hashing the slot twice, the copy) plus a 200 ms delay run inside the I2C event callback, where the delay cannot end when the SysTick is below that interrupt, as in the default of the HAL, and the watchdog resets the chip instead. Nothing in the firmware changed; `docs/BENCH_TESTS.md` says what the model covers and that bench tests 1 to 15 stay as they are.

## [0.4.6] - The distribution notes say that the clients read the official release only: docs/FIRMWAREDISTRIBUTION.md and docs/RELEASESIGNING.md no longer describe a file that names a bench release to use while there is no official one;... (and 5 more changes)

- **The distribution notes say that the clients read the official release only:** `docs/FIRMWARE_DISTRIBUTION.md` and `docs/RELEASE_SIGNING.md` no longer describe a file that names a bench release to use while there is no official one; the flasher and the web studio take the newest official release and reach a bench pre-release only by its tag.
- **The Python sources are type checked in CI, and the workflow runs the shared baseline action:** `mypy.ini` (mypy 2.4.0, the same version as URTC-DEVTOOLS) checks `tools/`, `tests/` and the root scripts, leaving out the copies of the shared picture tools and of the image-record reference; `fail()` in `tools/ci_validate.py` is typed as never returning and the installed-identity check of the bootloader twin reads the value once. The steps every repository runs first (Python, project validation, light build check, compilation of the sources) now come from `.github/actions/urtc-baseline/action.yml`, the canonical copy of URTC-DEVTOOLS, and `tools/devtools.lock.json` records it with set version 0.0.3.
- **Every image signature record is verified with no key before a release is published:** `tools/verify_image_records.py` rebuilds, from the file name and the bytes of each application image, the header the record covers (the header is never part of the flashed image), decides as the bootloader would against the release trust table of `keys/image_trust.json` (key, hardware, version, length, CRC-32, SHA-256, counter floor) and requires the bootloader of the set to hold exactly that table; `tools/verify_release_set.py` runs it, so the release workflow and the local signing step refuse a record that was cut, altered or made for another board or by another key. The Ed25519 reference is the shared copy in `tools/image_record`, checked by its lock file. Host tests cover a good set, a changed bit, the other board, a foreign key, a counter below the floor, a malformed record and a wrong bootloader table.
- **The simulation bench is a named step of the CI and the manifest says `simulator`:** the virtual board (the real receive path of the application, with the address and undefined-behaviour sanitizers, fed the frames of the specification and 3000 seeded random ones) and the twin of the bootloader (the real bootloader of the master taking a signed update and refusing a bad signature, an unknown key, a counter below the floor and a foreign board) now run as their own step, as well as inside the unit tests, and `docs/BENCH_TESTS.md` says what they cover and what only a board can show. The flasher runs unchanged against the twin in its own CI. The bootloader of the slave and the timing of the interrupts are not simulated.
- **No long comment blocks left in the firmware:** the last four narrative blocks (the stack-pointer range note and the jump sanity check in `bootloader_protocol.c`, the deferred MLX capture note in `slave_i2c_sensors.c` and the MLX dispatch note in `slave_i2c_sensors.h`) are reduced to the reason that matters; only comments change, the code is the same, and the budget of `tools/c_quality.json` goes from 4 to 0.
- **The two build scripts link the object files in the same order:** `build_firmware.sh` took the glob `*.o` as bash sorts it (by bytes, capitals first: `MLX90640_API.o`, `MLX90641_API.o`, `MLX90642.o` and `STM32F303CC_main.o` before every `firmware_*.o`) while `build_firmware.bat` left the wildcard to the linker, which sees the folder without distinguishing capitals, so the same sources gave images that differ in layout. Both now sort the names without distinguishing capitals: the shell script with `LC_ALL=C sort -f` into arrays (`list_objects`, `link_objects`), the batch script with `dir /b /on` into response files for the linker (`:write_objlist`, forward slashes), for the objects of the component and for those of the HAL. The comparison of the two scripts (`.github/workflows/windows-build-compare.yml`) is the check.

## [0.4.5] - The full build check compiles only on CI or when allowed: tools/buildtest.py prints BUILDTEST=SKIPPED (exit status 2) for the stacks that compile unless it runs on a CI runner or ALLOWLOCALBUILD=1 says so; the light check is... (and 25 more changes)

- **The full build check compiles only on CI or when allowed:** `tools/build_test.py` prints `BUILD_TEST=SKIPPED` (exit status 2) for the stacks that compile unless it runs on a CI runner or `ALLOW_LOCAL_BUILD=1` says so; the light check is not locked.

- **The host C suites and the virtual boards compile only on CI or when allowed:** `tools/run_host_tests.py`, `tools/c_quality.py` (the conversion check), `tools/fuzz_virtual_board.py` and `tools/bootloader_twin.py` ask `tools/local_build.py` first; on a plain PC they say SKIPPED (exit status 2), never passed. `ALLOW_LOCAL_BUILD=1` allows them, a CI runner always may, and the build scripts of the repository set it themselves because running one is the explicit way to build. Tests of the lock added.

- **`build_firmware.bat` ends with the right exit code:** it compared `%FAIL%` after `endlocal`, when the variable no longer exists, so the last line was a syntax error and the script returned 255 even after a build with no failure; the result is now worked out before `endlocal`. Found by the comparison workflow of the two build scripts, whose run on a Windows runner built the four components with 21 steps passed.
- **The build check of the repository is one text for the whole family, with a light mode that CI runs:** `tools/build_test.py` is the same file in every repository of the family (taken from the shared set); what used to differ between its seven variants is data in an optional `tools/build_test.json`. `python tools/build_test.py --check` looks at the manifest, the stack and the files the full check needs and compiles the Python sources, builds and runs nothing, and the workflow of the repository now runs it after the validation of the project.
- **The two build scripts are compared by the images they make:** the manual workflow `.github/workflows/windows-build-compare.yml` builds the four components with `build_firmware.sh` on Linux and with `build_firmware.bat` on a Windows runner, from the same commit and the same release of the Arm GNU toolchain, and `tools/compare_builds.py` fails when an image is missing on one side or differs by a single byte (the first different offset is named). It holds no key. The first result is that the four images of the two scripts differ (the first different byte of each is in the log), so the comparison is a report and not a gate: the step is marked failed and written in the summary of the run, and the run stays green until the images are equal. Generating the steps of the batch script from one source waits for that.
- **The board sends its protocol version, last reset cause, firmware version and uptime:** `health_profile.[ch]` gain `Health_EncodeInfo` and `firmware_features.c` sends the new frame `0x1BB` (eight bytes) after every tenth health frame, only while the health feature runs; the layout is held by the specification and by the same bytes in `tests/test_health_hsi.c`. Built and run only in the continuous integration.
- **The expansion slave bootloader takes its register numbers, status codes, failure reasons, link address and hardware identity from the generated header:** `boot/slaveboot_common.h` includes `urtc_can_ids.h` and names each value after the macro of the specification, as the main bootloader does for its identifiers; the values are the same, so the image does not change.
- **The programmed serial number of the main bootloader is written by a signed command and kept where a recycled page keeps it (off by default):** with `URTC_BL_PROGRAMMABLE_SERIAL` set to 1 at build time the bootloader obeys `0x7EE` (twelve chunks: target, key id, new serial, counter, Ed25519 signature) and answers on `0x7EF`. The key must be one of the trust table, the signature must name the unique ID of this head (so a command made for another head fails) and the counter must be higher than the stored one (so a replay changes nothing). The record is kept in two 16-byte slots after the 28 records of the log of the metadata page, magic last, and `bootloader_flash.c` writes the newest one again first when it erases that page to recycle the log. With the default (0) the record is not read and the command answers "not built with it". Host tests: the command against 17 shared vectors (valid, other head, other key, altered, replay, no usable serial, switch, running update), the answers and the flash content of the bootloader built both ways, a restart that answers with the programmed serial, the recycling of the page more than once, and a program that fails at each of the eight steps of a write. The expansion slave has no address of its own on the bus: it is reached through the master. The workshop tool is `tools/serial_command.py` of URTC-FLASHER.
- **An official release carries the image signature record of every application image:** `tools/publish_release_assets.py` adds `<image>.imgsig` (the 76-byte Ed25519 record as 152 hexadecimal digits on one line) to the set, signs it like any other asset (`<image>.imgsig.sig`), refuses an official set that lacks one or holds a file that is not a record, and lets a bench pre-release go out without; `tools/verify_release_set.py` repeats the check before the release workflow publishes. `docs/RELEASE_SIGNING.md` describes the whole path from the build artifact to the published set.
- **The register and the frame of the SHA-256 for the expansion slave have names without HMAC:** `REG_DIGEST_EXPECTED` (register `0x02`) and `URTC_CAN_ID_SLAVE_DIGEST_CHUNK` (`0x212`) replace `REG_HMAC_EXPECTED` and `URTC_CAN_ID_SLAVE_HMAC_CHUNK`; the numbers and the bytes on the bus are the same. The generated header keeps the old macros as aliases, `HandleDigestExpected` replaces `HandleHmacExpected`, and `docs/CANBUS.TXT` is regenerated.
- **The main bootloader can be addressed by serial number when several heads share a bus:** `bootloader_protocol.c` keeps the node state of `urtc_bl_select.h` (serial folded from the 96-bit unique ID), answers the commands on `0x7EA` (probe, select, release, unique ID) with `0x7EB`, and `bootloader_main.c` drops the single-head frames `0x7F0` to `0x7FF` while another head is selected. With nobody addressing it, or a blank serial, every frame is obeyed exactly as before. The programmable serial record is not read yet. `tests/test_bootloader_boot.c` replays a probe, a selection of another serial, of its own serial, a release and a blank serial.
- **The main bootloader takes the identifiers of the CAN exchange of the update from the generated header:** `boot/bootloader_common.h` includes `urtc_can_ids.h` and names each `CAN_ID_*` after the macro of the specification instead of repeating the number, so the numbers cannot drift; the compiled image is the same. The slave bootloader and the status codes still spell their numbers.
- **The expansion slave bootloader keeps a signed rollback counter and holds no key:** the metadata page of the slave is now an append-only log (28 records of 72 bytes, the layout of the main bootloader) whose record carries the floor of the signed counter; an image whose record has a counter below the floor is refused with reason `ROLLBACK` even when its version is newer, an equal counter installs, a higher one raises the floor in the very record that marks the copy as pending, and a compiled floor (`URTC_SLAVE_COUNTER_FLOOR`) stays if the page is ever blank. The main board reads the floor from the new register `0x0B` before it starts, refuses a record below it, and writes the counter of the verified record to `0x0C` before it relays END; the confirmation message is now 80 bytes (`URTC-SLV-CONF-V2`, with the counter), so a confirmation cannot be reused for another counter. The HMAC key and the HMAC routine are removed from the slave bootloader, together with the build flag, the `slave=` line of the key file and the registry entry; the manifest of the slave application carries no HMAC and a release build passes without any key file. Host tests: every case above plus a power cut at each half-word of a record and the page recycled after 28 records. Not run on a board; the version of the slave bootloader is raised when the batch is published.
- **Both tool families that talk to the slave need the new frames:** a flasher must send the record in `0x21A` and a SHA-256 in `0x212` (see `docs/SLAVE_BOOT_CONFIRMATION.md`).
- **The expansion slave installs an image only when the master confirms it:** the slave bootloader has no room for the Ed25519 verifier (13676 of 18432 bytes, about 7 KB short) and its application stays at the same address. The master now verifies the signature record of the slave image (`firmware_slave_attest.[ch]`, the shared verifier compiled into the application, the keys read from the trust table of its own bootloader) and the slave only checks size, hardware id, CRC-32, a plain SHA-256 and the version rule, then waits ten seconds in status `0x08` for a 32-byte confirmation (`urtc_slave_confirm.h`) bound to a nonce of the session and to the image. No confirmation, a wrong tag, a replay or an image altered after the checks fails the update closed (reason `0x06` or `0x03`) with the main slot untouched. The record travels on the new CAN message `0x21A`; the `0x212` frames carry a SHA-256 instead of an HMAC, so a flasher that still sends an HMAC can no longer update the slave. The slave trusts the master through the link bus; `docs/SLAVE_BOOT_CONFIRMATION.md` states the limits.
- **Host tests for the confirmation:** the update suite of the slave bootloader replays a valid confirmation, an absent one, a wrong tag, a confirmation used twice, a replay from an earlier session, a confirmation for another image or version, an image altered after the checks and after a power cut, and a pending copy resumed from a changed backup slot; the bridge suite runs the real verifier on signed test cases (valid, no record, damaged record, record of another key or hardware id, altered or short data, closed or damaged trust table, rotation).
- **The manual firmware build takes the trust table as an input and measures the size of the bootloaders:** the `flavour` input of `.github/workflows/firmware-build.yml` (`dev`, the default, or `release`) replaces the rule that every image must carry the development key; the build now fails unless the main bootloader holds exactly the trust table of its flavour (`tools/image_trust.py check`), and the release table stays closed until the image-signing key exists. A new step runs `tools/bootloader_size.py`: it reads the ELF of each bootloader with `arm-none-eabi-size`, takes the flash and RAM regions from its linker script, writes a table in the job summary and fails when a bootloader leaves less free space than the margins of `tools/bootloader_size.json`.
- **The main bootloader verifies an Ed25519 record against a table of public keys and holds no secret:** `bootloader_trust.[ch]` decodes the trust table that the linker places in the section `.trust_keys` at offset `0x200` of the binary (generated from `keys/image_trust.json` by `tools/image_trust.py headers`, in a `dev` flavour for bench builds and a `release` flavour that is the closed table, no key, until the image-signing key is provisioned, so an official bootloader built today installs nothing); `bootloader_ed25519.c` compiles the shared verifier with the other files and feeds the watchdog while it runs. The record (key id, signed rollback counter, signature) comes in 11 frames on `0x7EC`; it signs the 28-byte image header that the frames of the update describe, the SHA-256 of the image and the counter, so a changed image, version, board or counter breaks it. The policy is `ed25519_only` with no HMAC path; the bootloader announces the id of its key on `0x7ED`. The HMAC key and its functions are gone from the main bootloader (`firmware_signing.py` keeps the development HMAC key only for the bootloaders of the earlier generation, and `cflags master` now picks the table, `-DURTC_TRUST_DEV` or the release one, instead of injecting a key). The expansion slave bootloader is unchanged until the size of the verifier is measured against its region (`docs/FIRMWARE_SIGNING.TXT`, section 5).
- **The metadata of the main bootloader is an append-only log with a generation number and a signed rollback counter:** one record of 72 bytes (generation, state, CRC-32) is appended per change and the valid record with the highest generation is the state, so a power cut during a write leaves the previous state in force; the page is erased only when its 28 records are used. The state now keeps the floor of the signed counter and the SHA-256 of the application (it replaced the HMAC) which the check at every start compares. An image whose counter is below the floor (or the one compiled in from the public file) is refused with reason `0x05`, and the authorised downgrade never overrides it.
- **Host tests for all of it:** the update suite replays valid, unknown, revoked, closed-table, wrong-board, wrong-version and wrong-counter records, the 11 chunks in any order, every single-bit change of a table, the log across a power cut at each of the 36 halfwords of a record, the full page and its compaction; `bootloader_trust_release` holds the release table (closed while no key exists, never the development key); the twin of the bootloader takes signed updates, refuses the bad ones and enforces the floor. The throwaway test keys and the signed cases come from `tools/make_boot_sig_cases.py` (`tests/boot_sig_cases.[hj]`). `docs/BENCH_TESTS.md` lists the bench test 14 for the board (not run).
- **The public trust table of the bootloaders as a source file, with the check that a compiled bootloader holds it:** `keys/image_trust.json` lists the public keys a bootloader accepts to sign an application image (a `dev` flavour with a public development key and a `release` flavour that stays empty until the image-signing key is provisioned), the fixed offset of the table in each bootloader and the default policy `ed25519_only`. `tools/image_trust.py` validates the file, builds the table bytes, prints the key ids the local release tool pins and checks that a binary holds exactly that table at the fixed offset; `tests/test_image_trust.py` covers it, including a flipped bit anywhere in the table. Nothing is compiled into a bootloader yet.
- **The release workflow only verifies and publishes; the signing is local:** `.github/workflows/release.yml` no longer builds, no longer signs and holds no key (the secrets `URTC_SIGNING_KEYS` and `URTC_RELEASE_ED25519_PEM` are gone from it). The maintainer builds and signs on their machine and uploads the signed set as an unpublished draft (`tools/publish_release_assets.py --sign --draft` prints the command); the manual, `main`-only, approval-gated workflow downloads the draft, runs the new `tools/verify_release_set.py` (tag, digests, manifests, versions, production-signed images, the `.sig` of every asset against the public key of `keys/registry.json`, no stray file) and only then publishes a release made from the verified files, checks it again once published and removes the draft. Nothing is published if anything fails. `docs/RELEASE_SIGNING.md`, `docs/FIRMWARE_DISTRIBUTION.md` and `docs/KEYS.md` describe the steps; new tests cover the verifier with throwaway keys and the shape of the workflow. Nothing was published and no version changes.
- **A host suite for the Ed25519 verification and the addressing by serial number of the bootloaders:** `src/common/urtc_ed25519.c`, `urtc_auth.h` and `urtc_bl_select.h` (copies of the shared parts, checked by the lock file) are held to RFC 8032, to the shared vectors of the decision to install a signed image and to the shared scenarios of one bootloader answering the serial-number frames, in the new `bootloader_auth` suite. Nothing is built for the board and no bootloader calls these parts yet.
- **The bootloader parts and their host test follow the shared design:** `urtc_bl_select.h` gains the programmable serial (a record of three words in the metadata that takes priority over the serial folded from the unique ID, and `urtc_bl_node_init_prog`), `urtc_auth.h` names the default policy (`ed25519_only`), and the host suite replays the new scenarios, including whole buses with a duplicated serial. Nothing is built for a board and no bootloader uses these parts yet.
- **`docs/CANBUS.TXT` and the generated identifiers describe `0x7EA` to `0x7ED`,** and say that `0x7FA` is exactly the three bytes of the version.
- **`docs/BENCH_TESTS.md` lists thirteen bench tests for the firmware** (slave I2C clock order and direction, linker RAM size, local bus pins, start-up with a stored sensor, saved-state migration, power cut during an update, version readout and anti-rollback, closed register list, addressing by serial number, signature policy, fault dump and watchdog, head telemetry), each with setup, steps and expected result. None has been run on a board yet.

## [0.4.4] - Quality checks of the C sources, the flash layout written once, the data of both build scripts from one file, the constants of both bootloaders held to the specification and the twin of the bootloader for host tests

- **Quality checks of the C sources that the compiler of the board does not make:** `tools/c_quality.py` runs the host compiler with `-Wconversion` over every source the host suites build (a budget of zero warnings), cppcheck over the firmware folders (warnings, performance and portability findings, with the one judged finding suppressed and its reason written next to it) and a limit on the number of long comment blocks; what is left out (the vendored sensor drivers, held byte for byte by `check_shared_drivers.py`), the flags and the limits are in `tools/c_quality.json`. `build_firmware.sh` and `build_firmware.bat` run it after the host suites (a missing cppcheck is a warning on a developer machine and a failure when `URTC_CI=1`) and the CI installs cppcheck. Making the sources clean needed explicit casts at about forty places (the PWM scalings, the page arithmetic of the bootloaders, the rendering of numbers, the CAN length bytes); none changes a value, but the bootloader and application sources changed, so the next build of the images takes them. The fake HAL of the host tests now defines the size of a flash page the way the real one does.
- **The flash layout is written once:** `src/common/urtc_flash_layout.h` holds the addresses and sizes of the master and of the slave; both bootloader headers and the vector table setting of the application take them from it instead of spelling the number three times, and `tests/test_flash_layout.py` holds the four linker scripts to the header, because a linker script cannot include it.
- **The data of the two build scripts comes from one file:** `tools/build_plan.json` holds the pinned vendor sources, the compiler and linker flags and the list of HAL modules; `tools/gen_build_scripts.py` writes them into `build_firmware.sh` and `build_firmware.bat` between markers and `--check` fails when either script differs, so Windows and Linux can no longer build with different flags. The steps themselves are still written once for each shell.
- **The constants of both bootloaders are held to the specification:** `tests/test_spec_macros_master.c` and `tests/test_spec_macros_slave.c` (two suites, 40 in all) compile `boot/bootloader_common.h` and `boot/slaveboot_common.h` next to `src/common/urtc_can_ids.h`, which is written from `spec/can_protocol.json`, and fail the build when an identifier of the update exchange, a register of the link bus, a status code, a reason of a failed verification, the hardware identity, the address of the slave, the size of a flash page or the length of the signature differs from the specification. The bootloaders still spell the numbers out themselves; taking them from the header is a change of their source, and so of their version, for the next build of the images.
- **The twin of the bootloader:** `tests/virtual_bootloader.c` is the bootloader of the master as it is (`bootloader_main.c` with only its `main()` renamed, `bootloader_protocol.c`, `bootloader_flash.c`, `bootloader_crypto.c` and the display driver) compiled for the host with the address and undefined-behaviour sanitizers behind an SLCAN-style text port, like the virtual board of the application. Its window of 600 ms, its heartbeat, its ten-second timeout, the flash that only lets bits go from 1 to 0 until a page is erased and the reset into the new image are the real ones; only the jump into the application is replaced (the run ends with a `J` line). `tools/bootloader_twin.py` builds it (inside WSL on Windows), drives it the way a flashing tool does (start, four signature chunks, data, end) and checks a signed update, a flipped bit in the signature, a foreign hardware id and a transfer left silent for ten seconds (`--check`); `--build-only` prints the command that runs it, which `urtc-virtual-board --firmware-repo DIR --firmware-bootloader` bridges to, so the flasher can be tried against the code that accepts or refuses an image. The fake HAL gained one hook, called when the receive FIFO is polled and empty, where the twin takes the next line of its port.
- **One executor for the host tests of the firmware, shared with the other firmware repositories:** `tools/run_host_tests.py` (the same text in `URTC`, `URTC-SMART-RACK` and `URTC-VISION-TOOL`, kept in the shared set) builds and runs the suites that `tools/host_tests.json` lists. Where `cmake` and `ctest` are installed (CI and WSL) CMake builds every suite and `ctest` runs it, with at most two compilations at the same time; elsewhere it compiles and runs each suite itself, with the same flags. The sanitizers, the static analysis, the coverage floor and the run through WSL are options of the same tool. `build_firmware.sh` and `build_firmware.bat` call it, so the order of the suites, the flags and the floor are written once.
- **The suites are data:** the list that lived in the Python of `tools/run_host_tests.py` is now `tools/host_tests.json`; nothing changes in what is built, run, analysed or measured.

## [0.4.3] - The free-tool text of the EEPROM and ECOVIA notes follows the specification, the version checklist keeps its table of versions by itself, the CAN reference documents the reply of the tool rack and the shared set carries the test launchers

- **The free-tool text of `docs/EEPROM.TXT` and `docs/ECOVIA.TXT` follows the specification:** the jumper readings 0-24 name the 25 tool profiles (not 12), the selection of the free register is 1-25 (26-255 and a blank F-RAM mean no tool), `0x1A2` is only the write and needs at least one byte, and the query is `0x1A3`, whose payload is ignored. Text only: the firmware is unchanged.

- **The version checklist keeps its table of versions by itself:** `tools/version_checklist_state.py` writes, between two marker lines of `VERSION_CHECKLIST.txt`, the project version and every firmware component with the file that holds it, from `urtc.project.json`; `bump.py` runs it after raising a version and `--check` tells when the block is out of date.

- **The lights of Project status follow the evidence of the manifest:** the manifest says how far the repository has been shown to work (`evidence`: `host-test`, `simulator`, `bench` or `field`) and the validator fails when a light is above what that evidence and the maturity allow (`tools/status_lights.py`), so a green that never ran on real hardware cannot come back.

## [0.4.2] - The CAN identifiers as named macros, extended and remote frames ignored, the release workflow telling the flasher and the web studio, and the universal README

- **Fixed: an extended or remote CAN frame is no longer taken for a command.** The receive callback acted on `rxHeader.StdId` without looking at the frame type; for an extended frame the HAL leaves `StdId` as it was (the identifier of the frame before) and a remote request carries whatever the mailbox held, so either could repeat the previous command, including one that actuates and one that feeds a communication watchdog. Only a standard data frame is dispatched now (the bus activity LED still counts any frame); a host test sends an extended frame with a leftover identifier of the laser command and a remote request and checks that no handler runs. This is source: a firmware image has to be built to carry it.
- **The firmware names its CAN identifiers with macros:** the 114 identifier literals of the application (comparisons, sends and the companion frames) are now the `URTC_CAN_ID_` macros of `src/common/urtc_can_ids.h`, a copy of the header URTC-COMMON generates from the specification (with its lock file), and `urtc_version_wire.h` takes its identifiers and patch constants from the same header. A test fails on a new literal or on a hand edit of the copy. The behaviour is unchanged: all 38 host suites pass under the sanitizers. This is source: a firmware image has to be built to carry it.
- **The release workflow can tell the flasher and the web studio:** after it publishes a release it sends a `repository_dispatch` event (`urtc-firmware-released`, with the tag and the commit) to URTC-FLASHER and URTC-WEB-STUDIO, whose scheduled workflows already listen for it. It needs a repository secret named `URTC_DISPATCH_TOKEN` that the owner creates (a token with write access to the contents of those two repositories; `docs/RELEASE_SIGNING.md` has the command); while the secret is missing the step prints a notice and is skipped, so the CI and the release stay green.
- **Related Projects lists the URTC family:** the section names the repositories of URTC by group, each with one short line about what it is for, and, in one short line each, the sister projects (independent ecosystems of the same author that share the base and the tools), in the eight languages. It no longer says that URTC is part of another ecosystem.
- **The README follows the universal layout in the eight languages:** the sections are in the fixed order with the fixed names (the title no longer carries a version), the Architecture and the Repository Structure are drawn from `docs/architecture.json` and `docs/structure.json`, the tool catalogue, the OLED, the expansion connector and the status LED are the parts of Features, the thermal guard and the signing notes are Security & Safety, the CAN update sequence is Interfaces, and the first set-up over SWD/JTAG is Quick start. The annotated tree of the files is `docs/SOURCE_MAP.md`. The versions of the four components are described without explaining how they are raised, in the README and in the changelogs.
- **The layout of the README is checked in the CI:** the validator runs `tools/universal_readme_check.py` and lists, as a warning, what differs from the universal layout of the README.
- **Project status is the second section of the README,** right after At a glance, in the eight languages.
- **The full CI runs every night:** a scheduled workflow runs the complete baseline again on the default branch and opens (or updates) one issue when it fails, so a change in a runner image, a toolchain or a pinned dependency is found the next day instead of with the next unrelated change.
- **A build or a test run no longer installs software by itself:** when the ARM toolchain is missing, `build_firmware.sh` installs it only with a person at the terminal (sudo asks for the password); in CI or in any non-interactive run it stops and says what to install. The Linux distribution used by the Windows test runner can be chosen with `URTC_WSL_DISTRIBUTION` (default Ubuntu-24.04).

## [0.4.1] - Every firmware source host-tested, a generated tool table and CAN reference, the slave patch number and a bootloader session rule

### Firmware and protocol

- **The tool table of `docs/TOOLS.TXT` is generated from the specification:** a table of the 25 profiles (identifier, name, kind, range, watchdog, expansion board) between markers at the end of the document, written by `tools/generate_tools_doc.py` of URTC-COMMON from `spec/tools.json`; the digest in the marker finds a hand edit, and `tests/test_tools_doc.py` also holds the sections of the prose to the order of the identifiers. The prose of each tool is unchanged.
- **The master adds the low byte of the slave minor to `0x222`:** the answer to `0x221` is cut at 8 bytes and stops at the high byte of the minor number of the slave application, so no frame carried the low byte and the programs showed `0.?.5`. The companion is now 3 bytes (register, patch, low byte of the minor, byte 8 of the register); a program that reads two bytes still gets the patch, and a master or a slave from before the byte behaves as it did. `urtc_slave_app_patch_frame_build` takes the third byte; the `can_slavebridge` and `version_wire` suites check the byte, a minor of 0 and the full range, and `docs/CANBUS.TXT` is regenerated from the specification.
- **The multinode node is held to shared scenarios:** `tests/vectors/multinode_scenarios.h` (written by URTC-COMMON from `conformance/multinode_scenarios.json`, whose expectations are typed by hand from the design) carries 14 scenarios of one node (listening, hearing another node, selection and its lapse, a board on the same address, safe stop, the node command, discovery spacing, a reported fault, the wrap of the millisecond counter); the `multinode` suite replays them against `multinode.c` step by step. The same file is replayed against the Python model of the nodes in URTC-COMMON.
- **The bootloader answers the readback and the downgrade authorisation only in the session the application started:** when the application accepts `0x7F0` it leaves a mark in a backup register of the RTC block; the bootloader reads it once at start-up, clears it, and ignores `0x7FE` and `0x7FD` in any other session while a valid application is installed (a board with none keeps both, for its recovery). `URTC_BOOT_RESTRICT_UNSAFE_COMMANDS=0` at build time answers them in every session again. The decision is `src/common/urtc_boot_session.h`; `tests/test_bootloader_boot.c` plays a power-up, a session started by `0x7F0` and a board with no application, and `tests/test_can_global_pre.c` checks that only an accepted `0x7F0` leaves the mark. The backup register is to be confirmed on a board.
- **The slave application sends its patch number:** the tenth byte of `REG_APP_VERSION`, reserved until now, carries `FIRMWARE_VERSION_PATCH` (1 to 127). The master reads the whole register for `0x221`, answers with the same bytes as before and, when that byte is a patch number, follows with the new `0x222` companion frame (register `0x01`, patch); a byte of 0, what an older slave application sends, is ignored and no frame follows. The bootloader of the slave already reports the patch of the application it holds in its extended version register (`0x218`). Host suites: `slave`, `can_slavebridge`, `version_wire`.
- **The reference half of `docs/CANBUS.TXT` is generated from the specification in URTC-COMMON:** the index of the identifiers, the field layouts with their examples, the tool profiles, the enumerations, the constants, the magic payloads, the registers of the expansion slave, the procedures and the multinode frames now sit between marker lines at the end of the document (`tools/generate_canbus_doc.py` of URTC-COMMON writes them; the digest of each block is in its marker). The prose above them is untouched and stays hand-written. `tests/test_canbus_doc.py` fails when a generated block is edited by hand, and the shared tests of URTC-COMMON fail when a block is not what the specification produces, so a new identifier reaches the document by one run of the tool.
- **Every firmware source is now compiled by a host suite (13 new suites, 38 in all; 95 percent of the lines, up from 88):** the bit-banged I2C and SPI of the expansion connector and the ADS1115 behind a model of the bus that follows START, STOP, the clocks and the acknowledge (`expansion_bus`); the status LED and the camera ring, with the WS2812 stream checked for all 256 values (`led`); the OLED driver, the font and every tool screen, read back from a model of the display (`oled_render`); the interrupt handlers and the safe state of the multi-head module (`interrupts`); the clock tree, CAN bit rate, I2C timing, SPI clock, timers, PWM frequencies, ADC and the level of every pin per tool (`init`); the three thermal-camera transports, the MLX90642 driver and application on a device model, and the MLX90640 driver beyond its start-up (register round trips, trigger, frame validation, broken-pixel correction in both modes, a synthetic calibration whose temperatures are finite, monotonic and reproducible) and the MLX90640 and MLX90641 applications (`mlx_bus`, `mlx_api`, and the same two on the copies the expansion slave carries); the expansion slave end to end (`slave`: PWM, the I2C link registers, the converter and the cameras, the start-up and the idle shut-off); the main of both bootloaders (`bootloader_boot`, `slaveboot_main`); and the real main of the application run from the power-up for all 25 tools (`firmware_main`: reset cause, tool identification, boot sequence, telemetry frames, the bus-error recovery, the faster blink on a latched error, the AOI strobe, the night mode, the status LED). The CI floor goes from 80 to 90. Nothing in `src/` changed, no firmware was built and none of this has run on a board.
- **The fake HAL grew with it** (`tests/host_hal/`): pins with hooks on every read and write, I2C master and slave transfers, the SPI with DMA, the interrupt entry points, the start-up structures of the clocks, CAN, I2C, SPI, DMA, timers and ADC with the constants the arithmetic needs, a watchdog hook that lets a test act in the middle of a main loop, and `fake_i2c_bus.c`, a device-level model of the expansion I2C (a register file, a 16-bit register file, the word memory of the MLX90640 and MLX90641 and the opcode protocol of the MLX90642) that answers the bit-banged bus and the hardware I2C alike. `run_host_tests.py` learned two things for them: a suite may rename the `main()` of a source (`-D`) and may switch off one sanitizer check (the one the Melexis driver trips with a left shift of a negative value).
- **Fixed:** the 5x7 font of the application gets its missing `/` glyph (and shapes for the comma and the minus), so every glyph sits at the place of its own character: `0` is drawn as a 0 and `A` as an A; the small font of the bootloader draws a real `0`; the double-size text of the bootloader display lands at the column asked for (and is cut at the edge of the line instead of wrapping), and its progress bar fills up to the frame at 100 percent. **Still to confirm on a board:** both the expansion slave and its bootloader start a receive when the HAL announces `I2C_DIRECTION_RECEIVE` and a transmit otherwise, which is the opposite of the master's point of view the HAL documents; and the version answer of the slave application leaves its tenth byte (reserved) at zero instead of the patch number, which needs a decision about the contract with the master.
- **Host tests for the logic around the hardware: 8 new suites and a coverage floor.** Compiled against the fake HAL (extended with the ADC and a `fake_state.c` that defines the shared state of `firmware_common.h`) and run under the sanitizers and `gcc -fanalyzer`: `can_tools` (the 13 tool handlers: lengths, the boot-sequence gate, clamps, timer scaling, the direction-change rule of the steppers, the replies), `control_thermal` (the heater loops: ADC mapping, open and shorted sensors, ceilings of 445 and 300 C, hysteresis, stuck heater, the three guard faults, the thermistor table, the PID switch), `blackbox` (the recorder replays the scenarios URTC-COMMON writes from its own model and the 678 bytes in the F-RAM must match, with the CAN readback chunk by chunk), `identify` (jumpers 0..31, the free configuration record, the inert state), `can_global_post` (SPI passthrough, diagnostic line, the erase with its magic payload, the LED override), `sensors_inspection` (telemetry, the debounced end-stop, the thermal camera routing and its 32-byte chunks, the lifetime thermal statistics). 25 suites in all (two more for the bootloader of the expansion slave, below); the suites that keep static state run each scenario in a child process.
- **The bootloader of the expansion slave is host-tested too:** `slaveboot_crypto` (the same RFC 4231 and padding-boundary vectors as the main board's) and `slaveboot_update` (the update as the I2C registers drive it: start, expected HMAC, data in pieces of 1 to 32 bytes, end; size and hardware-id validation, CRC32, signature, the five rollback cases, flash errors at the erase, the program and the copy, the resume of an interrupted copy, the adoption of an image with no metadata, the two version answers). 97 percent of `slaveboot_flash.c`, 84 percent of `slaveboot_protocol.c`.
- **Fixed in the slave bootloader:** `HandleStartUpdate` now parses the START into locals and validates it before it touches `update_total_size` and the hardware id, as the main board's bootloader does: a rejected START mid-transfer leaves the transfer underway intact and it still completes.
- The two companion links that do not travel on the CAN bus are documented as big-endian in `docs/CANBUS.TXT` (version 2 of the rack serial frame and of the vision head sensor frame; version 1 was little-endian and is refused), and the shared vectors copied into `tests/vectors/` are the current ones.
- The detached signatures of the firmware images (`firmware/*.sig`) are ignored by git like the images they sign: they are release assets, and the old ones that were tracked are no longer kept in the repository.
- **The firmware is held to the shared vectors:** `tests/vectors/urtc_fw_vectors.h`, written by URTC-COMMON from its conformance vectors, is compiled into three host suites: the minor and patch fields and the version ordering (`urtc_version_wire.h`), the payload that resets into the bootloader on `0x7F0` (and that no other payload of the vectors does) and the payload that authorizes a downgrade on `0x7FD`; before, only the three programs read those vectors.

### Tests, builds and checks

- **A static check on every push:** `ruff.toml` (unused names, exception chaining and syntax errors, the rules the flasher and the tester already ran) and a step of the CI that runs `ruff check .` with a pinned ruff; it found two re-raises inside `except` without a cause (`release_sign.py`, `generate_manifest.py`, now `from None`) and an unused import in a test, fixed; the unused font variables of the documentation image generator are left as they are.
- **`tools/run_host_tests.py --coverage-floor N`** builds the suites with gcov counters and fails under that share of executed lines of the firmware sources they compile (95 percent measured over all 78 sources); `build_firmware.sh` and `.bat` pass `URTC_COVERAGE_FLOOR`, and the CI sets 90.
- The `TEST_ASSERT` macro of the host tests comes from URTC-COMMON (`tests/vectors/urtc_test.h`, copied with `vendor_copy.py --set firmware_test`) instead of being written again in `tests/test_runner.h`; the suites are unchanged.
- The CI holds the build tooling tests to a coverage floor of 35 percent of `tools/` (measured 43 on a machine without the repositories next to this one).

### Documentation

- **The README files carry sections written once in URTC-COMMON:** "Compatibility", in the eight languages, between marker comments before the license heading (`tools/readme_sections.py` of URTC-COMMON writes and checks them; the rest of each README is hand-written as before).

### Other changes

- **Fixed:** in the inert state (no tool, or an unreadable free configuration) the pin PB6, shared by the stepper enable and the laser interlock, is written once and ends high: the stepper driver is disabled. Before, it was written high and then low, which left the driver enabled. The laser stays off because nothing raises its power without the laser tool selected. The start-up pin configuration agrees.

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

- The firmware version scripts keep their own three-component scheme and no longer hand over to the shared manifest utility, which broke the build at the boundary versions; the successor rule is now a pure function covered by boundary tests that run in CI and as a step of the build script.
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
