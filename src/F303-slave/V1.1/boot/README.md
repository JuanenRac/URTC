# URTC Expansion Slave Bootloader — Technical Reference

**Project:** URTC v1.1 (Universal Robot Tool Controller) - Expansion Ecosystem
**Author:** JuanenRac (Electro Hobby 3D) — electrohobby3d@gmail.com
**License:** This document is CC BY-SA 4.0; the source it describes
(`SLAVEBOOT.C` and its partitioned form) is GPL-3.0. See the repo
root README's "License and Copyright Notices" section for the full
breakdown.

This document covers `SLAVEBOOT.C` specifically — the A/B update system
that runs on the **expansion slave chip** (STM32F303CBT6, populating the
2 ADVANCED expansion board variants only) before `SLAVEAPP.C` (the
application) ever gets control. For the application firmware itself,
see `src/F303-slave/V1.1/README.md`. For the main board's own
bootloader, which this one is a smaller sibling of rather than a
variation on, see `src/F303-master/V1.1/boot/README.md`.

---

## 1. What this guarantees

Same promise as the main board's own bootloader: **a failed or
interrupted update never leaves this chip unable to run its last
known-good firmware.** Every update lands in a separate backup slot
first, gets fully verified there, and only then gets copied into the
slot that actually runs.

**What's genuinely different from the main board's own bootloader:**
this chip has no CAN peripheral at all and never talks to the outside
world directly — every byte of an update arrives over the **I2C LINK
bus** to the main board's own STM32F303CC, which is master on that bus;
this chip is a real hardware I2C **slave** on it (not bit-banged,
unlike the main board's own bit-banged expansion-bus role). The
update's ultimate origin is still the Flasher over CAN-OTA same as any
other update — it just gets relayed across this I2C link one register
write at a time before this bootloader ever sees it.

## 2. Flash layout

```
0x08000000 ─┬─ Bootloader                18 KB   (this file)
0x08004800 ─┼─ Metadata                   2 KB   (version, size, CRC32, HMAC)
0x08005000 ─┼─ Main application slot     54 KB   (SLAVEAPP.C, what actually runs)
0x08012800 ─┴─ Backup/staging slot       54 KB   (updates land here first, verified, then copied to main)
0x08020000    (end of flash, 128 KB total)
```

Scaled down from the main board's own 256 KB scheme (this chip is
STM32F303CBT6, 128 KB flash — same xB/xC density line as the main
board's own STM32F303CCT6, confirmed against the same ST datasheet
family, just less of it). The bootloader region itself (18 KB) is
larger than a first size estimate suggested — the real compiled size,
once actually linked, came in at ~13 KB; 18 KB leaves roughly 5 KB of
genuine headroom above that, not a size chosen to fit with nothing to
spare.

## 3. Update sequence

Register-based, not frame-based — the link-bus master writes/reads
ordinary I2C registers rather than CAN frames with fixed IDs. Same
underlying validation sequence as the main board's own bootloader,
different transport:

1. Master writes `REG_START_UPDATE` (declares size + HardwareID). A
   HardwareID mismatch or oversized declaration is rejected before a
   single byte is written — this chip's own HardwareID
   (`0x0303CB01`) and HMAC signing key are both deliberately different
   from the main board's own, so an image meant for one can never
   accidentally verify against the other.
2. Master writes `REG_HMAC_EXPECTED` (the full 32-byte signature in one
   transaction — I2C has no equivalent to CAN's 8-byte-per-frame
   ceiling, so this doesn't need the main board's own 4-chunk split).
3. Firmware data streams in via repeated `REG_DATA` writes (up to 32
   bytes per transaction), landing in the **backup slot only**. A
   dropped link or power failure during this phase leaves the chip
   exactly as capable of running its existing application as before —
   nothing about the main slot has been touched.
4. Master writes `REG_END_UPDATE` (CRC32 + declared version), which
   triggers verification: completeness, then CRC32, then HMAC-SHA256,
   then anti-rollback against the currently-installed version. Any
   single failure aborts here — `REG_STATUS` reports which — and the
   main slot remains untouched.
5. Only once every check passes does the bootloader erase the main slot
   and copy the verified backup into it, page by page, with read-back
   verification on the copy itself. `REG_PROGRESS` reports 0-100
   through this phase — this chip can't push a heartbeat proactively
   the way the main board's own bootloader does over CAN (an I2C slave
   only ever answers when asked), so the link-bus master polls this
   register instead of being told.
6. `SCB->VTOR` relocates to the main slot's vector table and control
   jumps to the application, which redundantly repeats the same
   relocation as the first line of its own `main()` — same defensive
   reasoning as the main board's own bootloader/application pair.

## 4. Startup validation

Identical logic to the main board's own `ApplicationIsValid()` and
`JumpToApplication()` — same metadata checks, same stack-pointer-in-
valid-RAM verification (this chip's own 2 real RAM regions: 40 KB SRAM
at `0x20000000`, 8 KB CCM at `0x10000000`, confirmed against the same
xB/xC datasheet family as the main board), same SWD/JTAG-adoption path
for a directly-flashed chip with no update-flow metadata yet, same
interrupted-copy resume on the next boot. The reasoning transfers
unchanged from CAN to I2C; nothing about *why* these checks exist is
transport-specific.

## 5. Entering this bootloader

The application (`SLAVEAPP.C`) resets into this bootloader on
`REG_ENTER_BOOTLOADER` (magic payload `0xB0 0x07 0x1D 0x5A`, same
constant the main board's own application checks for its own CAN
`0x7F0`) — see `src/F303-slave/V1.1/README.md` section 4 for the
application-side handler. There is no separate physical BOOT button on
this chip's own board the way the main board has one; `BOOT0` is
present (pulled low by default, same convention) for SWD-level recovery
only, not a day-to-day entry path.

## 6. Files

### Monolithic form

| File | Purpose |
|---|---|
| `SLAVEBOOT.C` | Everything: I2C1 (slave)/flash init, the update state machine, both validation paths, and `JumpToApplication()`. |

### Partitioned form (`partitioned/`)

Same logic split across 8 files by subsystem, built and linked side by
side with the monolithic form on every change, same "no patch
perception" dual-maintenance rule as the main board's own bootloader/
firmware.

| File | Purpose |
|---|---|
| `slaveboot_main.c` | Entry point: clock/I2C1/IWDG init, the I2C1 slave "listen mode" interrupt handling (`HAL_I2C_AddrCallback` and friends) that implements the register protocol, and `main()`'s own boot sequence. |
| `slaveboot_common.h` | Shared types, `#define`s (flash addresses/sizes, the `REG_*` register map, version constants), and `extern` declarations. |
| `slaveboot_crypto.c` / `.h` | The same from-scratch SHA-256/HMAC-SHA256 implementation as the main board's own bootloader, reused near-verbatim (it's pure algorithm, nothing chip-specific) - RFC 4231-verified. This chip's own `HMAC_KEY` is defined here, deliberately different from the main board's own. |
| `slaveboot_flash.c` / `.h` | Page erase/write/verify, the backup-to-main region copy, and metadata read/write - same algorithm as the main board's own, `update_progress_percent` in place of an active CAN heartbeat push. |
| `slaveboot_protocol.c` / `.h` | The update state machine (`REG_START_UPDATE` through `REG_END_UPDATE`), both startup validation paths, and `JumpToApplication()` itself. |

## 7. Build system

`gcc-arm-none-eabi`, producing `.bin`/`.hex`/`.elf` for both forms, same
toolchain the main board already uses (this chip shares CMSIS device
files with the main board's own STM32F303CC — xB/xC density line, same
`stm32f303xc.h`/`startup_stm32f303xc.s`, only the linker script's own
`LENGTH` differs). Every edit bumps this bootloader's own
`BOOTLOADER_VERSION_PATCH`, independent of both the main board's own
bootloader version and this chip's own application firmware version.

---

For the I2C-link register protocol this bootloader answers to, see
`slaveboot_common.h`'s own `REG_*` definitions directly (no separate
wire-protocol document exists yet for this chip the way `docs/CANBUS.TXT`
does for the main board - worth creating if this protocol grows much
further). For pin-by-pin hardware detail, see `docs/PINOUT_SLAVE.txt`.
For the application firmware this bootloader hands control to, see
`src/F303-slave/V1.1/README.md`.
