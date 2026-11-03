# Bench tests for the firmware

Nothing in this file has been run on a real board yet. Each test below lists the
setup, the steps and the expected result so that the first run on hardware can
be recorded as pass or fail without interpretation. Until a row of the result
column is filled in with a real run, the behaviour it covers is only proven by
the host tests and the simulators, not on silicon.

Setup common to all tests: one master board, one tool head with a slave chip
(STM32F303CB), a CAN adapter that speaks SLCAN, a bench supply with a current
limit, a logic analyser on the I2C lines, and the firmware images built from the
commit under test (note the commit and the `0x7E9` version in the result).

| # | Area | Result |
|---|------|--------|
| 1 | Slave I2C clock enable order | not run |
| 2 | Slave I2C receive direction and callback | not run |
| 3 | Slave RAM size in the linker script | not run |
| 4 | Slave local bus pins (I2C2) | not run |
| 5 | Slave start-up with a stored sensor variant | not run |
| 6 | Saved-state migration after an update | not run |
| 7 | Power cut during a firmware update | not run |
| 8 | Version readout and anti-rollback | not run |
| 9 | Closed list of slave registers | not run |
| 10 | Bootloader addressing by serial number (several heads) | not run |
| 11 | Ed25519 and HMAC policy of the bootloader | not run |
| 12 | Hard fault dump and watchdog | not run |
| 13 | Head telemetry through the rack | not run |
| 14 | Ed25519 record, trust table and metadata log of the main bootloader | not run |

## 1. Slave I2C clock enable order

Why: the slave application and its bootloader enable the I2C peripheral clock
after the HAL initialisation call, which can leave the peripheral unclocked.

Steps: flash the slave with the change that enables the clock first; probe SCL
and SDA while the master scans the bus.

Expected: the slave acknowledges its address on the first scan after reset, with
no retry. With the old order the first scan may fail.

## 2. Slave I2C receive direction and callback

Why: the address callback arms the receive with the HAL direction constant that
describes the master's direction, which is the inverse of what the slave must
arm, and the design assumes the receive-complete callback fires on STOP.

Steps: write 1, 4 and 8 bytes from the master to the slave; read them back
through a status register; repeat with a repeated START.

Expected: the byte count seen by the slave equals the bytes sent in all cases;
no byte is lost or duplicated; a transfer shorter than the buffer still
completes.

## 3. Slave RAM size in the linker script

Why: the scripts declare 40 KiB of RAM for a chip whose data sheet gives 32 KiB.

Steps: set the length to 32K, link both the application and the bootloader,
read the map file; run the application with the stack at the new top.

Expected: the link succeeds with room to spare, the stack top is `0x20008000`,
and the board runs a thermal capture without a fault.

## 4. Slave local bus pins (I2C2)

Why: the documentation and the master side disagree about whether PB10 and PB11
offer an I2C alternate function on this chip.

Steps: read the alternate-function table of the data sheet for the exact part,
probe PB10 and PB11 while the slave reads the sensor.

Expected: the sensor answers on the pins that the firmware drives, or the
firmware and the pinout document are changed to the pins that do carry I2C.

## 5. Slave start-up with a stored sensor variant

Why: the sensor driver is initialised before the master has told the slave which
sensor variant is fitted, so the variant is still "none" at that moment.

Steps: fit each supported thermal sensor in turn, power-cycle the head without
the master configuring it, read the first frame.

Expected: the sensor is initialised on the first boot with the stored variant;
no frame is requested from an uninitialised sensor.

## 6. Saved-state migration after an update

Why: raising the saved-state version discards the whole stored record.

Steps: store a non-default configuration with the old image, update to the new
image that carries a higher saved-state version, read the configuration back.

Expected: the one-time hardware settings survive the update; only fields that
did not exist before take their defaults.

## 7. Power cut during a firmware update

Why: the backup slot and the staged copy must leave the board bootable whatever
instant the supply is lost.

Steps: start an update; cut the supply at about 10, 30, 50, 70 and 90 percent of
the transfer and during the final copy (use the bench supply's programmable
output or a relay); restore power each time and read the version through `0x7E9`.

Expected: after every cut the board boots either the old or the new complete
image, never a mix; an interrupted update can be restarted without a programmer.
Record the percentage and the version after each cut.

## 8. Version readout and anti-rollback

Steps: read `0x7E9` on a board with the new firmware; try to flash the previous
release.

Expected: the readout shows major, minor and patch matching the image; the
downgrade is refused by the flasher and by the bootloader.

## 9. Closed list of slave registers

Steps: from the CAN side, read registers inside and outside the closed list, and
registers `0x220` and `0x221`; send a zero-length read.

Expected: only listed registers answer; everything else and the zero-length read
are rejected with the documented error code.

## 10. Bootloader addressing by serial number

Steps: connect two or more heads in application mode, query the bootloader with
the probe frame (`0x7EA`), select one by its serial, update only that one.

Expected: exactly one head is selected at a time, the others stay silent, and
the release frame (`0x7EB` to `0x7ED` group) returns all of them to normal.

## 11. Ed25519 and HMAC policy of the bootloader

Steps: load images signed with Ed25519 only, HMAC only and both; load an image
with a broken signature.

Expected: with the default policy (`ed25519_only`) only the Ed25519 image is
accepted; a broken signature is refused and the previous image keeps running.

## 12. Hard fault dump and watchdog

Steps: force a hard fault (a debug command or a deliberate bad pointer), reset,
read the dump kept in the no-init area; stop feeding the watchdog.

Expected: the dump names the faulting address and registers and survives the
reset; the watchdog resets the board within its configured period, and the clock
source (internal oscillator or crystal) matches the build.

## 13. Head telemetry through the rack

Steps: connect a head to the rack, change the soldering iron and hot-air set
points, unplug the head.

Expected: the two questions and the set point frames are exchanged as in the
protocol document, counters advance, and an unplugged head is reported as lost
within the configured time.

## 14. Ed25519 record, trust table and metadata log of the main bootloader

Steps, all with a main board that has the development bootloader flashed over SWD (its key is public, so any bench machine can sign an image for it; the seed is published with the shared reference):
1. Update with a valid record: the board reaches VERIFY OK, resets, and the version query answers the new version; the `0x7ED` frame holds the key id of the table.
2. Flip one bit of the record, then of the image, then change the version declared in `0x7F4`: each update is refused with reason 0x03 (or 0x02 for the image) and the main slot is untouched.
3. Send a record signed by another key: refused with 0x03. Flash a bootloader whose table lists that key as `next`: it installs.
4. Flash the official bootloader (closed table): every update, even with a valid development record, is refused with 0x03 and `0x7ED` is silent.
5. Install counter 5, then try counter 3 with a newer version: refused with 0x05, also after `0x7FD`; counter 7 installs.
6. Cut the power at several points of an update: during the data, during the verification (the SHA-256 and the Ed25519 take seconds), during the copy and during a metadata write. After each cut the board boots the old or the new application, never a damaged one; a cut during the copy resumes it at the next start.
7. Install 14 versions in a row (28 records) and one more: the page is compacted and the board still boots the newest; repeat with a power cut at the compaction (the expected result is a first-boot adoption with the compiled floor).
8. Measure with the debugger the time of one verification and the stack watermark, and read the size of the three bootloaders in the build log of Actions.

Expected: every refusal leaves the previous application running; the watchdog never fires during a verification (the verifier feeds it every 8 bits of the scalar).

Status: not run on a board.
