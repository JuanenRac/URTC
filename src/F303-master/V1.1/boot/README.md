# URTC Bootloader — Technical Reference

**Project:** URTC v1.0 (Universal Robot Tool Controller)
**Author:** JuanenRac (Electro Hobby 3D) — electrohobby3d@gmail.com
**License:** This document is CC BY-SA 4.0; the source it describes
(`BOOTLOADER.C` and its partitioned form) is GPL-3.0. See the repo
root README's "License and Copyright Notices" section for the full
breakdown.

This document covers `BOOTLOADER.C` specifically — the golden-image
A/B update system that runs before `STM32F303CC.C` (the application)
ever gets control. For the application firmware itself, see
`src/F303-master/V1.1/README.md`. For the wire-level CAN protocol
(every ID, byte layout, and DLC — including the `0x7F0`-`0x7FA` range
this bootloader owns), see `docs/CANBUS.TXT`.

---

## 1. What this guarantees

The bootloader's entire design exists to make one promise: **a failed
or interrupted update never leaves the board unable to run its last
known-good firmware.** Every update lands in a separate backup slot
first, gets fully verified there, and only then gets copied into the
slot that actually runs — the main slot is never written to directly
by an incoming update.

## 2. Flash layout

```
0x08000000 ─┬─ Bootloader                30 KB   (this file)
0x08007800 ─┼─ Metadata                   2 KB   (version, size, CRC32, HMAC - describes whatever is in the main slot)
0x08008000 ─┼─ Main application slot    112 KB   (STM32F303CC.C, what actually runs)
0x08024000 ─┴─ Backup/staging slot      112 KB   (OTA updates land here first, verified, then copied to main)
0x08040000    (end of flash, 256 KB total)
```

Metadata is written last, only after the copy to the main slot's own
read-back verification passes — so a `FirmwareMetadata_t` with a valid
magic value is itself proof the application it describes was fully
verified at some point, not just present.

## 3. Update sequence

1. Master sends `0x7F0` (application resets itself into the
   bootloader) or the board boots fresh with no valid main-slot
   metadata at all.
2. Master sends `0x7F1` (start update, declares size + HardwareID).
   A HardwareID mismatch or an oversized declaration is rejected
   before a single byte is written.
3. Firmware data (`0x7F2`) and the HMAC-SHA256 signature (`0x7F7`, 4
   chunks) stream in, page by page, into the **backup slot only**.
   Each page write gets a `0x7F3` acknowledgement; the master waits
   for it before sending the next page, which is what keeps the CAN
   receive FIFO from ever needing to hold more than a page's worth of
   frames at once. A lost connection or power failure during this
   phase leaves the board exactly as capable of running its existing
   application as before the update started — nothing about the main
   slot has been touched yet.
4. `0x7F4` (end update) triggers verification against the backup
   slot: size/completeness first, then CRC32, then HMAC-SHA256 against
   the signing key. **Any single failure aborts here** with a specific
   reason (`0x05` + a reason byte — see `docs/CANBUS.TXT`'s `0x7F5`
   documentation), and the main slot remains untouched.
5. Only once *all* checks pass does the bootloader erase the main slot
   and copy the verified backup into it, page by page, with the same
   read-back verification applied to the copy itself. A heartbeat
   frame fires after every page of this copy, not just once at the
   end, so a master watching for liveness during the (comparatively
   slow) erase-and-copy phase doesn't mistake it for a hang.
6. `SCB->VTOR` is relocated to the main slot's vector table and
   control jumps to the application — which redundantly does the same
   relocation itself as the very first line of its own `main()`,
   defensive rather than because either side alone is insufficient.

The bootloader's own version (`BOOTLOADER_VERSION_MAJOR/MINOR/PATCH`,
independent of the application's version) is reported via `0x7FA`,
alongside `0x7F9`, whenever the bootloader itself — not the
application — is the one answering a version query, since the
application has no way to introspect a currently-flashed bootloader's
version any other way.

**What this bootloader deliberately does not do:** touch flash option
bytes (no RDP2 read-protection path exists in this design at all — the
one genuinely permanent failure mode on this chip family is simply not
reachable through anything here), or accept an update addressed to a
different HardwareID (checked before erasing anything).

## 4. Startup validation

Every boot (not just after an update) runs `ApplicationIsValid()`
before deciding whether to jump: metadata's magic value, size bounds,
CRC32, and HMAC-SHA256 must all check out against what's actually
sitting in the main slot right now, not just what metadata claims.

**A board with no valid metadata at all** (a freshly SWD/JTAG-flashed
chip, which never went through the CAN update flow that writes
metadata) isn't treated as invalid outright — if the reset vector and
stack pointer at the start of the main slot look like a plausible ARM
Cortex-M image, the bootloader computes an HMAC over the whole slot
and adopts it as a new baseline. This does not, and is not meant to,
authenticate that image as coming from whoever holds the signing key —
physical SWD/JTAG access already bypasses any check a bootloader could
make. It only lets a directly-flashed image boot at all, the same way
it would with no bootloader in the picture. The HMAC check that
actually matters — gating a new image arriving over CAN against the
signing key — is unaffected by this adoption path.

`JumpToApplication()` repeats the stack-pointer and reset-vector
checks independently right before the jump itself, as a second,
narrower gate immediately ahead of an action that can't be undone
once taken. If it fails, the function simply returns — the bootloader
carries on listening on CAN rather than jumping into something it's
no longer confident about.

## 5. The two physical switches — BOOT and RESET

Easy to conflate with "enter this project's own bootloader," but both
buttons actually do something more fundamental, a level below anything
`BOOTLOADER.C` itself controls:

- **RESET** pulls `NRST` low — an ordinary hardware reset, equivalent
  to a power cycle. Execution restarts from the reset vector, which
  then follows the exact same `BOOT0`-dependent path described below.
- **BOOT** pulls the `BOOT0` pin high (pin 44, tied to this pushbutton
  and a 10 KΩ pull-down, so it defaults low — "normal" — whenever the
  button isn't actively held). This pin is read by the STM32's own
  boot ROM **before any of this project's code runs at all**:
  - **`BOOT0` = 0 (default):** boot from main Flash at `0x08000000` —
    which on this board is always `BOOTLOADER.C`. Everything in this
    document happens downstream of this path.
  - **`BOOT0` = 1 (held at reset):** boot into the STM32's
    **factory-programmed System Memory bootloader** instead — ST's own
    ROM code, entirely separate from anything in this repository,
    supporting recovery over USB DFU or UART. `BOOTLOADER.C` never
    runs at all in this mode and knows nothing about it.

In short: `0x7F0` gets a healthy *application* to voluntarily jump
into *this project's own* `BOOTLOADER.C`. The **BOOT** button is a
hardware-level escape hatch one step below that — relevant if flash
content is suspect enough to need ST's own factory recovery tool,
which this project's CAN protocol can't reach by definition (it
hasn't loaded yet at that point). `PC13` (ID4) and `BOOT0` are
electrically independent nets on this board, so pressing **BOOT**
never interferes with tool identification.

## 6. OLED diagnostics

The bootloader drives the same physical OLED as the application
(hardware I2C2, `PA9`/`PA10`, confirmed against DS9118 Table 14 - see
`docs/PINOUT.TXT`), with its own minimal font renderer and a handful of
fixed status screens:
"UPDATING" with a live progress readout while backup-slot pages
stream in, "VERIFYING" during the CRC32/HMAC checks, "COPYING" during
the backup-to-main copy, a plain success screen, and a failure screen
carrying the same reason code sent over CAN. None of this is required
for an update to succeed over CAN — it's a bench-debugging aid, not a
dependency of the protocol itself.

## 7. Files

### Monolithic form

| File | Purpose |
|---|---|
| `BOOTLOADER.C` | Everything: CAN/flash/I2C/OLED init, the update state machine, both validation paths, and `JumpToApplication()`. Compiled with `-x c` (its `.C` extension would otherwise be treated as C++ by some toolchains). |

### Partitioned form (`partitioned/`)

The same logic split across 10 files by subsystem, built and linked
side by side with the monolithic form on every change — not generated
from it, not a deprecated alternative to it. `.rodata` (every constant
table: font glyphs, the HMAC signing key, status strings) comes out
byte-for-byte identical between the two forms; the small `.text`
difference is fully explained by the monolithic compiler's own
cross-function inlining opportunities within a single translation
unit.

| File | Purpose |
|---|---|
| `bootloader_main.c` | Entry point: clock/CAN/I2C init, `main()`'s own boot sequence and update-listening loop, and every HAL interrupt callback. Named distinctly from the monolithic `BOOTLOADER.C` specifically to avoid the two being confused for each other while browsing - matches the firmware's own `STM32F303CC_main.c` naming for its equivalent entry point. |
| `bootloader_common.h` | Shared types, `#define`s (flash addresses/sizes, CAN IDs, the version constants), and `extern` declarations every other module needs — including the signing key, defined once in `bootloader_crypto.c` and referenced everywhere else, rather than a private copy compiled into each file that touches it. |
| `bootloader_crypto.c` / `.h` | The from-scratch SHA-256/HMAC-SHA256 implementation (not a library), verified against the official RFC 4231 HMAC-SHA256 test vector before ever being wired into the update flow. Also where `HMAC_KEY` itself is defined. |
| `bootloader_flash.c` / `.h` | Page erase/write/verify primitives, the backup-to-main region copy, and metadata read/write. |
| `bootloader_oled.c` / `.h` | The bootloader's own minimal font renderer and the fixed status screens described in section 6. |
| `bootloader_protocol.c` / `.h` | The CAN update state machine (`0x7F0`-`0x7FA`), both startup validation paths, and `JumpToApplication()` itself. |

## 8. Build system

`gcc-arm-none-eabi`, producing `.bin`/`.hex`/`.elf` for both forms.
Every edit to either form bumps `BOOTLOADER_VERSION_PATCH` (rolling
over to `MINOR`+1 after 9) — the filename never changes, only the
compiled-in version constants, since that's what a version query over
CAN actually reports.

---

For the wire-level CAN protocol this bootloader answers to, see
`docs/CANBUS.TXT`. For pin-by-pin hardware detail, see
`docs/PINOUT.TXT` and `docs/PINOUT_CONNECTORS.TXT`. For the
application firmware that this bootloader hands control to, see
`src/F303-master/V1.1/README.md`.
