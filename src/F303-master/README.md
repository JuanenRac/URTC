# URTC Firmware — Technical Reference

**Project:** URTC (Universal Robot Tool Controller)
**Author:** JuanenRac (Electro Hobby 3D) — electrohobby3d@gmail.com
**License:** This document is CC BY-SA 4.0; the source it describes
(`src/F303-master/`, `src/F303-master/boot/`) is GPL-3.0. See the repo
root README's "License and Copyright Notices" section for the full
breakdown.

This document is the engineering-level reference for the application
firmware (`src/F303-master/`, entry point `STM32F303CC_main.c`) and the
bootloader (`src/F303-master/boot/`, entry point `bootloader_main.c`):
hardware platform, the ID-jumper tool-selection system, per-tool
peripheral wiring, the update mechanism, and the safety systems tying it
together. For the wire-level CAN protocol (every ID, byte layout, and
DLC), see `docs/CANBUS.TXT` — this document explains *why* the system is
built the way it is; `docs/CANBUS.TXT` is the byte-for-byte reference.
For pin-by-pin detail, see `docs/PINOUT.TXT` and
`docs/PINOUT_CONNECTORS.TXT`.

---

## 1. Hardware platform

| | |
|---|---|
| MCU | STM32F303CCT6, LQFP48 |
| Core | ARM Cortex-M4F (hardware FPU, single-precision) |
| Flash | 256 KB |
| RAM | 40 KB main SRAM (`0x20000000`–`0x2000A000`) + 8 KB CCM RAM at the *unrelated* alias `0x10000000` (unused by this firmware — not contiguous with main SRAM, and nothing here needs the extra 8 KB enough to deal with the split) |
| Clock | No external crystal populated. `RCC_OSCILLATORTYPE_HSI` (internal 8 MHz RC) → `/2` → PLL ×16 → **64 MHz** system clock. Confirmed by the empty BOM (no crystal) and the OSC_IN/OSC_OUT pins being reused as plain GPIO (PF0/PF1, the two lowest tool-ID bits) rather than left for a crystal. |
| CAN | Bosch bxCAN peripheral, 500 kbit/s, standard 11-bit IDs only |
| *This project's* bootloader entry | CAN command only (`0x7F0`, application resets itself into this project's own bootloader) or fresh chip (no valid application) — see section 4a below for what the physical **BOOT** button actually gates instead |

### Flash layout (fixed, shared knowledge between bootloader, application, and both PC tools)

```
0x08000000 ─┬─ Bootloader              32 KB   (this project's own bootloader)
0x08008000 ─┼─ Main application slot  112 KB   (this application firmware, what actually runs)
0x08024000 ─┴─ Backup/staging slot    112 KB   (OTA updates land here first)
0x08040000    (end of flash, 256 KB total)
```

Every one of these addresses is a compile-time constant duplicated in
four independent places that all have to agree: `bootloader_common.h`,
the linker scripts (`STM32F303CCTx_BOOTLOADER.ld` / `_APP.ld`), and
`URTC Flasher`'s own `flasher_config.py` (its own `BOOTLOADER_FLASH_ADDR` /
`APP_FLASH_ADDR` / `*_MAX_SIZE` constants, overridable via
`urtc_config.json` if this is ever adapted to a different partition
scheme or chip variant - see that project's own repository,
github.com/JuanenRac/URTC-FLASHER, now independent of this one).

---

## 2. Tool identification — the 5-bit jumper matrix

Which of the 12 tool profiles this board behaves as is decided by **five
GPIO pins read once at boot**, never re-read afterward:

| Bit | Signal | Pin | Weight |
|---|---|---|---|
| 4 (MSB) | ID4 | PC13 | 16 |
| 3 | ID3 | PB7 | 8 |
| 2 | ID2 | PB4 | 4 |
| 1 | ID1 | PF1 | 2 |
| 0 (LSB) | ID0 | PF0 | 1 |

All five are configured as inputs with **internal pull-up enabled**, and
are **active-low**: a jumper shorting the pin to GND sets that bit to 1;
an open (no jumper) pin reads as 0. `Read_ToolID()` builds the 5-bit
value bit by bit:

```c
if (HAL_GPIO_ReadPin(ID0_PORT, ID0_PIN) == GPIO_PIN_RESET) id |= 0x01;
if (HAL_GPIO_ReadPin(ID1_PORT, ID1_PIN) == GPIO_PIN_RESET) id |= 0x02;
if (HAL_GPIO_ReadPin(ID2_PORT, ID2_PIN) == GPIO_PIN_RESET) id |= 0x04;
if (HAL_GPIO_ReadPin(ID3_PORT, ID3_PIN) == GPIO_PIN_RESET) id |= 0x08;
if (HAL_GPIO_ReadPin(ID4_PORT, ID4_PIN) == GPIO_PIN_RESET) id |= 0x10;
active_tool = (ToolMode_t)id;
```

giving 32 possible values, of which 0–11 map to real tool profiles (see
`docs/ECOVIA.TXT` for the full ID↔tool table), 31 (`0x1F`, all five
jumpers installed) is reserved as a "free configuration" address rather
than a 13th direct mapping - see section 6 below for the full
mechanism - and 12–30 fall through to "no tool assigned" — every
actuator forced safe, every CAN command for this profile silently
ignored, but the board otherwise still boots, still answers version/
active-tool queries, and still shows a clear warning on the OLED rather
than doing anything undefined.

**Reading it live:** `URTC Tester`'s connection panel shows all five
pins as individual indicators (decoded straight from the same byte the
`0x110`/`0x111` query already returns — no separate query needed, since
that byte *is* the raw 5-bit value) alongside the resulting tool number,
directly against this same table.

Two of these five pins double as something else on this chip if no
jumper were ever going to be needed there — PF0/PF1 are the same pins
that would otherwise be OSC_IN/OSC_OUT (moot, since no crystal is
populated), and PC13 is one of the few pins on this chip with its own
independent power domain characteristics, though nothing here depends on
that. All five are simple digital inputs as far as this firmware is concerned.

---

## 3. Tool profile architecture — how `active_tool` gates everything

`active_tool` is a single global, set once at boot and never changed
again during normal operation (a power cycle is the only way to change
which tool a board behaves as — this is a hardware jumper setting, not
something the CAN bus can override). Three things key off it, all inside
`main()`'s boot sequence, in this order:

1. **`MX_GPIO_Post_Init()`** — configures only the GPIO pins the active
   tool actually needs as outputs (motor step/dir/enable, PWM pins,
   interlocks). Everything else affecting the *same physical pins* for
   *other* tools is deliberately left untouched at this stage, since
   several pins are shared across mutually-exclusive tool roles (PB3 in
   particular — stepper STEP output for six tools, LM393 comparator
   input for vacuum pickup, generic probe/endstop input otherwise; see
   `docs/PINOUT_CONNECTORS.TXT`).
2. **Peripheral init** (`MX_ADC_Init`, `MX_TIM1_DrillLaserFan_Init`,
   `MX_TIM2_HotendFan_Init`, `MX_TIM3_Full_Init`) — only what the active
   tool needs gets clocked and configured at all; an unused peripheral is
   never touched.
3. **The main loop's CAN dispatch** — every incoming command is gated by
   `switch (active_tool)`; a command addressed to a tool ID this board
   isn't currently configured as is silently ignored by design, not an
   error condition (per `docs/CANBUS.TXT`).

### Per-tool peripheral reference

| ID | Tool | Actuation | Timer/PWM | Sensing | Comm watchdog |
|---|---|---|---|---|---|
| 0 | Soldering Iron | T12 heater (PA1) | bang-bang, no PWM timer | ADC1_IN1 (PA0) thermocouple | 250 ms |
| 1–3, 6–7 | Dispensers/Screwdriver/Grippers | Generic stepper (STEP/DIR/ENN) | TIM3 step-tick (~500 steps/s ceiling) | none | none (one-shot moves) |
| 4 | Vacuum Pickup | — | — | ADC1_IN11 (PB0) + PB3 (LM393 digital) | — |
| 5 | Drill (BL4260) | Brake (PB9), direction (PA4) | TIM1_CH1/PA8 @ 20 kHz | Tachometer via EXTI3/PA3 | none (brake on 0 speed) |
| 8 | AOI Inspection | Ring LED strobe | — | PB3 endstop | none |
| 9 | Laser Engraver | Interlock (PB6) | TIM1_CH1/PA8 @ 20 kHz | PB3 endstop | 250 ms |
| 10 | 3D Printer | Hotend heater (PA1) + stepper + 2 fans | TIM1 @ 25 kHz (layer fan), TIM2/PA5 @ 25 kHz (hotend fan) | ADC1_IN11 (PB0) NTC | 250 ms (hotend), 1000 ms (layer fan), stall-detect (hotend fan) |
| 11 | Scan Probe | — | — | PB3 via EXTI3, max CAN priority | — |
| 12+ | *(unassigned)* | all actuators forced safe | — | — | — |

Two of these deserve a specific safety note, both already covered in
depth by `docs/PINOUT_CONNECTORS.TXT` and `docs/CANBUS.TXT`: the soldering iron and
3D-printer hotend are true **bang-bang thermal control**, not PID in the
classical sense — ±2 °C hysteresis, a hard 445 °C/300 °C ceiling
respectively (445, not 450, for the soldering iron specifically - the
ADC's own measurable range tops out close enough to 450 that the
current-vs-target shutoff comparison could never actually trigger at
exactly 450, leaving the heater with no way to command itself off; see
the safety-ceiling comment in `firmware_control_thermal.c` for the full
reasoning), ADC-fault detection (a reading outside 15–4090 counts is
treated as a disconnected/shorted sensor, not "very cold" or "very hot"),
and a stuck-heater detector (output commanded off for 3 s but temperature
still rising more than 5 °C anyway → declared a critical fault, not left
to keep monitoring silently).

---

## 4. Bootloader architecture — golden-image A/B update

The bootloader's entire design exists to make one guarantee: **a failed
or interrupted update never leaves the board unable to run its last
known-good firmware.** It does this with a backup-then-verify-then-copy
sequence, never writing directly to the main slot:

1. Master sends `0x7F0` (application resets itself into the bootloader)
   or the board boots fresh with no valid main-slot metadata.
2. Master sends `0x7F1` (start update, declares size + HardwareID).
   HardwareID mismatch or an oversized declaration is rejected before a
   single byte is written.
3. Firmware data (`0x7F2`) and the HMAC-SHA256 signature (`0x7F7`, 4
   chunks) stream in, page by page, into the **backup slot only** — the
   main slot is never touched during this phase, so a lost connection or
   power failure here leaves the board exactly as capable of running its
   existing application as before the update started.
4. `0x7F4` (end update) triggers verification against the backup slot:
   size/completeness, CRC32, then HMAC-SHA256. **Any single failure
   aborts here** with a specific reason (`0x05` + a reason byte — see
   `docs/CANBUS.TXT`'s `0x7F5` documentation) and the main slot remains
   untouched.
5. Only once *all* checks pass does the bootloader erase the main slot
   and copy the verified backup into it, with the same read-back
   verification on the copy itself.
6. `SCB->VTOR` is relocated to the main slot's vector table and control
   jumps to the application (which redundantly does the same relocation
   itself as the very first line of its own `main()` — defensive, not
   because either side alone is insufficient).

The bootloader's own version (`BOOTLOADER_VERSION_MAJOR/MINOR/PATCH`,
independent of the application's version) is reported via `0x7FA`,
alongside `0x7F9`, whenever the bootloader itself — not the application —
is the one answering a version query, since the application has no way
to introspect a currently-flashed bootloader's version any other way.

**What the bootloader deliberately does not do:** touch flash option
bytes (no RDP2 read-protection path exists in this design at all — the
one genuinely permanent failure mode on this chip family is simply not
reachable through anything this bootloader does), or accept an update
addressed to a different HardwareID (checked before erasing anything).

---

### 4a. The two physical switches — BOOT and RESET

The board carries two pushbuttons, silkscreened **BOOT** and **RESET**,
that are easy to conflate with "enter this project's own bootloader" but
actually do two more fundamental, chip-level things - genuinely useful
for recovery, but a level below anything this project's own bootloader
itself controls:

- **RESET** pulls `NRST` low - an ordinary hardware reset, equivalent to
  a power cycle. Execution restarts from the reset vector, which then
  follows the exact same BOOT0-dependent path described below. Nothing
  project-specific about this one; every STM32 dev board has the
  equivalent.
- **BOOT** pulls the `BOOT0` pin high (confirmed against the schematic
  netlist: `BOOT0` is pin 44, tied to this pushbutton and a 10 KΩ
  pull-down - so it defaults low, i.e. "normal", whenever the button
  isn't actively held). This pin is read by the STM32's own boot ROM
  **before any of this project's code runs at all**, and it decides
  between two completely different things:
  - **`BOOT0` = 0 (button not held, the default):** boot from main Flash
    at `0x08000000` - which on this board is always this project's own
    bootloader. This is the only path this project's own firmware ever
    sees, and everything described in sections 3-4 above happens
    downstream of this.
  - **`BOOT0` = 1 (button held at reset):** boot into the STM32's
    **factory-programmed System Memory bootloader** instead - ST's own
    ROM code, entirely separate from anything in this repository,
    supporting recovery over USB DFU or UART depending on the exact part.
    This project's own bootloader never runs at all in this mode, and
    knows nothing about it.

In short: `0x7F0` (the CAN command) gets the *application* to voluntarily
jump into *this project's own* bootloader - useful for a completely
normal, healthy board that just needs a new firmware image. The **BOOT**
button is a hardware-level escape hatch one step below that: relevant if
the flash content itself is suspect enough that you need ST's own
factory recovery tool instead, which this project's CAN protocol can't
reach by definition (it hasn't loaded yet at that point). `PC13` (ID4)
and `BOOT0` are electrically independent nets on this board (confirmed
against the netlist), so pressing **BOOT** never interferes with tool
identification.

---

## 5. CAN protocol — summary (see `docs/CANBUS.TXT` for the full byte-level reference)

- **500 kbit/s, standard 11-bit IDs.** No extended (29-bit) IDs are used
  anywhere in this protocol.
- **ID ranges:** `0x095` (max-priority scan-probe event) · `0x100`/`0x110`/`0x111`/
  `0x190`/`0x191`/`0x192` (global commands, valid regardless of active tool) ·
  `0x120`–`0x179` (per-tool commands/telemetry) · `0x180`–`0x183`
  (expansion SPI passthrough + DIAG0 query) · `0x1A0`–`0x1A5` (expansion
  board type, free tool configuration, peripheral type + device serial
  number - also global, also valid regardless of active tool) ·
  `0x7F0`–`0x7FA` (bootloader/version).
- **Big-endian byte order** for every multi-byte numeric field, throughout.
- **Telemetry is push, not poll:** tools with sensors broadcast their
  readings unsolicited, on a fixed period (typically 150 ms), rather than
  waiting to be asked.
- **Communication watchdogs are per-actuator, not global:** losing CAN
  contact cuts *just* the actuator whose watchdog expired (heater, laser,
  a specific fan), not the whole board - matching the "safe" GPIO states
  `MX_GPIO_Post_Init` establishes at boot for whatever isn't currently
  in active use.

---

## 6. Parameter persistence — FM24CL64B F-RAM (shared I2C2)

A 64Kbit I2C F-RAM, directly soldered to the board (no connector), holds
a periodically-updated snapshot of the active tool's setpoints and the
global LED/OLED settings — so a sudden power loss doesn't leave "what was
this board doing" as unknowable as the loss itself was unplanned.

**F-RAM, not EEPROM:** pin/protocol-compatible with a serial I2C EEPROM of
the same capacity (same control-byte/address scheme - it's a drop-in
replacement by design), but the underlying memory technology is
genuinely different in ways that matter here. Confirmed against the
official datasheet: writes complete at bus speed with no internal write
cycle to wait for ("NoDelay" writes - by the time a new bus transaction
could be shifted in, the previous write is already done, unlike an actual
EEPROM's multi-millisecond write cycle), and endurance is rated in the
trillions of cycles rather than an EEPROM's typical ~1 million.

**Why I2C2, not a bus of its own:** confirmed against ST's own datasheet
(DS9118), the STM32F303CC has **only two I2C peripherals total** ("up to
two I2Cs") — there's no I2C3 on this density variant at all, unlike some
larger STM32F3 parts. Of those two, only I2C2 has a usable pin route in
this design (PA9/PA10, AF4) - I2C1's own AF-mapped pin pairs on this chip
are all already committed to other functions with no free pins to
relocate anything to, so I2C1 sees no use here at all. That leaves I2C2
as the one hardware I2C bus available, already carrying the OLED - the
F-RAM shares it rather than getting a bus of its own. `CONN_EXPANSION`'s
own I2C need is met a different way entirely: bit-banged on PB10/PB11
(the same approach already used for that connector's SPI), since neither
of this chip's two hardware I2C peripherals had a pin pair left over for
it. Address `0x50` (A0/A1/A2 tied to GND) doesn't collide with the OLED's
own fixed `0x3C`.

**What gets saved:** the temperature setpoint (soldering iron or 3D
printer nozzle - only one is ever relevant for a board's fixed
`active_tool`), drill speed/direction, laser power/interlock, 3D-printer
fan speeds, the status/ring LED colors, OLED night mode, whether a
critical error was active at the moment of the last save, which
`CONN_EXPANSION` variant is installed, the free-tool-configuration
selection (only consulted when the ID-jumper reading is 31 - see below),
and a host-assigned device serial number (purely a label for telling
multiple boards apart on one CAN bus - see section 9's `EEPROM.TXT`
reference for the full byte layout of all of these). A CRC-8 checksum
(polynomial 0x07, the CRC-8/SMBUS variant - not the OTA update's
HMAC-SHA256, proportionate to what this actually needs to catch: an
uninitialized chip or an interrupted write, not a security boundary)
guards against trusting a corrupted or never-written record.

**Written periodically, not on every change** — checked every 500ms,
actually written only when something differs from what's already on the
chip, and even then rate-limited to at most once every 3 seconds. This
isn't protecting a limited write-cycle budget the way it would need to
for a real EEPROM - the F-RAM's endurance is effectively unlimited for
anything this board would ever do. The interval is kept anyway for a more
mundane reason: if a setpoint were ever adjusted rapidly (a UI slider
being dragged, say), there's no reason to write every single
intermediate value instead of just the one it settles on.

**Deliberately does not auto-resume anything hazardous.** On boot, the
saved record is loaded into RAM and made queryable over CAN (`0x190`/
`0x191`) — but a heater setpoint, laser power, or motor command is never
re-applied automatically. Doing so would mean a power blip (or a
deliberate power cycle for entirely unrelated reasons) could silently
resume a potentially hazardous operation with nobody watching, which
would undermine every communication watchdog described elsewhere in
this document. The safe, passive settings (LED colors, OLED mode) ARE
restored directly, since there's no hazard in remembering what color an
LED was. Recovering a hazardous setpoint, if actually wanted, is left as
a deliberate decision for whatever's on the other end of `0x190`/`0x191`
to make - not something this board decides on its own initiative.

Two of this board's shared pins have no dedicated RAM variable tracking
their current value (drill direction, laser interlock) - the CAN
handlers write them straight to their GPIO pin. The save routine reads
these back directly via `HAL_GPIO_ReadPin` (which reflects what's
actually being driven for a pin configured as an output, not just an
external voltage) rather than adding new tracking variables and touching
those existing handlers.

**Erasing it (`0x192`):** a magic-payload command (same reasoning as
`0x7F0`'s own magic trigger - a corrupted or malformed frame shouldn't
be able to wipe a real saved record) overwrites the saved region with
`0xFF` and clears the in-RAM copy immediately, not just on the next
boot. Only the application handles this, not the bootloader - both PC
tools expose it: `URTC Tester` as a standalone diagnostic button, `URTC
Flasher` as an optional checkbox that runs it before a normal CAN update
(while the application is still up, since that's a requirement, not a
convenience). See `docs/CANBUS.TXT` for the exact frame format.

---

## 6a. Free tool configuration and peripheral info - the two newest F-RAM fields

**Free tool configuration** exists to give the 5-bit ID-jumper matrix
(section 2 above) a 13th, dynamic option alongside its 12 direct
mappings: reading 31 (`0x1F`, every jumper installed) doesn't select a
fixed tool - it tells `Identify_PhysicalTool()` to instead consult a
F-RAM register (`free_tool_selection`, 0 = none selected, 1-12 = one of
the 12 supported profiles, stored as id+1 so 0 can unambiguously mean
"nothing chosen" without colliding with tool 0, the soldering iron) and
resolve the real tool from there. Set via CAN (`0x1A2`/`0x1A3` - see
`docs/CANBUS.TXT`; `URTC Flasher` is the only tool that writes it,
`URTC Tester` only reads it back) and persisted the same way every other
F-RAM field is (this section, above).

This required a genuine boot-order change to make possible: the F-RAM
shares I2C2 with the OLED, and I2C2 normally isn't initialized until
`OLED_Init()` runs, well after `Identify_PhysicalTool()` and the
tool-dependent GPIO/timer/ADC setup that follows it. `MX_I2C2_Init_Early()`
(just the peripheral+pin setup, none of the OLED-specific probing) is
called right before `Identify_PhysicalTool()` instead, specifically so
that function can read the F-RAM directly when the reading is 31 -
before anything tool-dependent has run yet, avoiding the much larger
risk of resolving the tool in a second pass after some of that setup
already happened for whatever tool the board *isn't* actually
configured as. `OLED_Init()` itself calls the same early-init function
rather than duplicating its own copy of the same setup.

If the F-RAM is unreadable, blank, or `free_tool_selection` is 0 or out
of range, the board falls back to exactly the same fail-safe state as
any other unrecognized ID-jumper reading (12-30): `TOOL_INVALID`, every
actuator forced to its safe default, the "CHECK ID JUMPERS" OLED screen.

**Peripheral type and device serial number** solve a different problem:
several URTC boards on the same CAN bus, all running identical firmware
with identical default settings, are otherwise completely
indistinguishable to a host - every query gets answered by every board
simultaneously under the same CAN ID, a genuine bus-level collision, not
just an inconvenience. Peripheral type is a fixed identity constant
(`URTC_PERIPHERAL_TYPE`, always `0x03` for this firmware - not a F-RAM
field at all, nothing to persist since it never changes for a given
build). Device serial number (0-255, default 0) is the field that
actually lives in F-RAM - a purely host-assigned label this firmware
never reads for any decision of its own, answered on request
(`0x1A4`/`0x1A5`) so a host can tell boards apart once each has been
assigned a distinct value (typically done with one board connected at a
time via `URTC Flasher`, before wiring several onto a shared bus). This
establishes the identification boards need to be distinguishable at
all - it doesn't by itself change how the rest of this protocol
addresses commands; every existing CAN ID is still answered by every
board that receives it regardless of serial number.

See `docs/EEPROM.TXT` sections 5 and 6 for the complete byte-level
mechanism of both, and `docs/CANBUS.TXT` for the exact frame formats.

---

## 7. Build system

Toolchain, HAL/CMSIS sourcing, linker scripts, and the one real
compile-time bug this project's own build process caught (a switch-
statement scope violation, invisible to any manual code review and only
caught by an actual compiler) - `arm-none-eabi-gcc`, STM32CubeF3's
HAL/CMSIS pulled from ST's own GitHub repos, and hand-written linker
scripts matching the flash layout in section 1 above exactly.

---

## 8. Source file layout (`src/F303-master/`)

One file (or small group of files) per subsystem. Every `.c` file below
has a matching `.h` of the same name unless noted otherwise; only the
`.c` is listed where that's the case, to keep this table one row per
subsystem rather than two.

| File(s) | Purpose |
|---|---|
| `STM32F303CC_main.c` | Global definitions and `main()` itself - the entry point tying every module below together. |
| `firmware_common.h` | Shared types, `#define`s, and cross-module `extern` declarations - every other module includes this. No matching `.c`. |
| `firmware_init_clocks.c` | System clock configuration (HSE/PLL, peripheral clock sources). |
| `firmware_init_gpio.c` | GPIO pin configuration, including the shared-pin reconfiguration by `active_tool` described in section 3 above. |
| `firmware_init_can.c` | CAN peripheral init and filter configuration. |
| `firmware_init_timers.c` | Timer peripheral init (PWM channels, step-pulse timer). |
| `firmware_init_buses.c` | I2C2 (hardware), DMA, and SPI1 init. |
| `firmware_identify.c` | Physical tool identification via the 5-bit ID jumper matrix - runs once at boot. |
| `firmware_expansion_spi.c` | `CONN_EXPANSION`'s own bit-banged SPI bus - the expansion driver's own passthrough (TMC2209/TMC5160A). |
| `firmware_expansion_i2c.c` | `CONN_EXPANSION`'s own bit-banged I2C bus - see section 6 above for why this is bit-banged rather than a hardware peripheral. Also owns `ExpansionI2C_SlaveWriteRegister/ReadRegister/ReadRegisterWithParam` - the high-level functions `firmware_can_slavebridge.c` builds on to reach the expansion slave chip. |
| `firmware_font.c` | OLED font glyph table. |
| `firmware_oled_driver.c` | Low-level I2C2 transactions to the SSD1306/SSD1315 OLED controller, plus `MX_I2C2_Init_Early()` - the I2C2 peripheral+pin setup split out on its own so `firmware_identify.c` can use it before `OLED_Init()` would otherwise run (see section 6a above). |
| `firmware_render.c` | OLED screen composition (tool screens, status strip, error/splash screens) built on top of the driver above. |
| `firmware_led.c` | WS2812B status + ring LED bit-bang driver. |
| `firmware_control_thermal.c` | Soldering iron and 3D hotend PID control loops (also covers Hot Air Rework's own heater, which shares this exact mechanism), including the independent safety ceilings. |
| `firmware_control_sensors.c` | Vacuum pickup and endstop sensor reading (also covers Flying Probe's own basic-mode ADC reading). |
| `firmware_telemetry_watchdog.c` | Periodic CAN telemetry transmission and the per-tool communication watchdogs. |
| `firmware_fram.c` | FM24CL64B F-RAM read/write primitives. |
| `firmware_persistence.c` | Saved-state load/save logic built on top of the F-RAM primitives above. |
| `firmware_can_dispatch.c` | The top-level `switch(active_tool)` that every tool-specific CAN handler below lives inside of. No matching `.h`. |
| `firmware_can_global_pre.c` | Global CAN commands answered regardless of `active_tool`, ahead of the critical-error gate (lighting, version/status queries, expansion-board config, free tool configuration, peripheral type + device serial number). No matching `.h` - declarations live in `firmware_can_global.h`. |
| `firmware_can_global_post.c` | Global CAN commands gated behind the critical-error check (SPI passthrough, F-RAM erase). No matching `.h` - shares `firmware_can_global.h`. |
| `firmware_can_soldering.c` | Soldering iron tool CAN handler (`0x130`). |
| `firmware_can_drill.c` | Drill tool CAN handler (`0x140`). |
| `firmware_can_laser.c` | Laser engraver tool CAN handler (`0x160`). |
| `firmware_can_aoi.c` | AOI inspection tool CAN handler (`0x150`). |
| `firmware_can_motion.c` | Shared motion-tool CAN handler (`0x120`) - paste/liquid dispenser, screwdriver, both grippers, SMT Pick&Place, and Large-Format Vacuum Gripper (7 tools sharing the same plain-stepper protocol). |
| `firmware_can_printer3d.c` | 3D printer tool CAN handler (`0x170`/`0x171`/`0x173`). |
| `firmware_can_electromagnet.c` | Heavy-Duty Electromagnet handler (`0x1B0`) - plain on/off, T12 as GPIO rather than PWM. |
| `firmware_can_weldpulse.c` | Shared timed-pulse handler for Spot Welder (`0x1C0`) and Ultrasonic Welder (`0x200`). |
| `firmware_can_uvcuring.c` | UV Curing Head handler (`0x1D0`) - real TIM1_CH1 PWM, shared with the drill/laser/layer fan. |
| `firmware_can_hotair.c` | Hot Air Rework handler (`0x1E0`) - sets `target_temperature` into the same thermal loop the soldering iron uses, plus its own blower PWM. |
| `firmware_can_crimping.c` | Crimping Actuator handler (`0x1F0`) - drives the expansion board's own driver (`EXP_TMC_STEP/DIR/EN`) instead of the onboard one. |
| `firmware_can_slavebridge.c` | CAN-to-I2C bridge to the expansion slave chip (`0x210`-`0x221`) - OTA relay and generic application-register access. Not gated by `active_tool`; see this file's own header comment for why. |
| `firmware_can_pastejetting.c` | Solder Paste Jetting handler (`0x230`/`0x231`) - thin wrapper over the bridge above. |
| `firmware_ads1115.c` | Direct ADS1115 driver for the Basic+ADS1115 expansion board (`expansion_board_type==5`) - reaches the chip straight over the bit-banged expansion I2C bus, no slave chip involved. Built on the same primitives `firmware_expansion_i2c.c` already exposes. |
| `firmware_can_flyingprobe.c` | Functional Testing Head handler (`0x240`-`0x242`) - dual-path: `expansion_board_type==5` talks to `firmware_ads1115.c` directly, `==3`/`4` relays through the expansion slave chip instead. Same CAN IDs either way. The basic onboard-ADC reading path (doc #7's own third option) lives in `firmware_control_sensors.c`. |
| `firmware_can_thermalinspection.c` | PCB Advanced Inspection handler (`0x250`-`0x255`) - same dual-path shape as Flying Probe above: `expansion_board_type==6` talks to whichever of `melexis_mlx90640/`, `melexis_mlx90641/`, or `melexis_mlx90642/` matches `mlx_sensor_variant` directly, `==3`/`4` relays through the expansion slave chip. Pixel chunks are split across multiple CAN frames per chunk on either path, since a chunk is larger than one frame's own payload. |
| `firmware_interrupts_can.c` | `HAL_CAN_RxFifo0MsgPendingCallback` - the CAN receive ISR every command above runs inside of. |
| `firmware_interrupts_timer.c` | Step-pulse generation ISR (TIM3) and its own concurrency-safe `steps_remaining` handling - resolves onboard vs. expansion-board driver pins based on `active_tool` (see Crimping Actuator above). |
| `firmware_interrupts_gpio.c` | EXTI (touch probe / FG pulse counting) ISR, including the probe-impact CAN message. |
| `firmware_interrupts_fault.c` | `HardFault_Handler` and related fault-safe-state logic. |

`melexis_mlx90640/`, `melexis_mlx90641/`, and `melexis_mlx90642/` are
subfolders, not single files - see section 8a below for what lives in
each and why they're split out from the flat list above.

Plus `STM32F303CCTx_APP.ld` (the linker script defining this application's
place in flash - see section 1's flash layout table) and this README,
both alongside the source files above.

**Adding a new tool profile** means writing that tool's own
`firmware_can_<toolname>.c/.h` with a `Handle_CAN_<ToolName>(void)`
function, then adding one `case` to the switch in
`firmware_can_dispatch.c` calling it - nothing else in that file
changes, and no other module needs to know the new tool exists unless
it specifically interacts with it. 25 tool IDs are assigned today (12
from the original release, 13 more from a later expansion); the ID
scheme has room for a handful more before needing another expansion.

---

## 8a. `melexis_mlx90640/`, `melexis_mlx90641/`, `melexis_mlx90642/` - this board's own direct MLX9064x support

Only relevant when `expansion_board_type==6` (Basic+MLX9064x, no slave
MCU) - the sensor is wired straight onto this board's own bit-banged
expansion I2C bus, so this board's own STM32F303CC has to talk to it
itself, the same job the expansion slave chip does on its side of the
Advanced variants. `mlx_sensor_variant` (`MLX_VARIANT_90640`/`90641`/
`90642`) decides which of these 3 folders `firmware_can_thermalinspection.c`
actually calls into - all 3 have full direct-path support today, mirroring
this project's own coverage on the expansion slave chip.

**`melexis_mlx90640/`** (32×24, 768px):

| File | Purpose |
|---|---|
| `MLX90640_API.h` / `.c` | Melexis's own official library (Apache-2.0, plain C), vendored unmodified - identical copy to the expansion slave chip's own `src/F303-slave/melexis_mlx90640/` (verified byte-for-byte before vendoring here, not a separate download). |
| `MLX90640_I2C_Driver.h` | This library's own platform-transport interface. |
| `firmware_mlx90640_transport.c` | This board's own implementation of that 5-function transport, built on `firmware_expansion_i2c.c`'s own bit-banged primitives. |
| `firmware_mlx90640_app.c` / `.h` | Application-level capture and chunk-serving logic - 48 chunks (768 pixels at 16 per chunk). |
| `LICENSE_MELEXIS_APACHE2.0` | The library's own unmodified Apache-2.0 license text. |

**`melexis_mlx90641/`** (16×12, 192px):

| File | Purpose |
|---|---|
| `MLX90641_API.h` / `.cpp` | Melexis's own official library (Apache-2.0), vendored with only one change: an `extern "C"` linkage wrapper added to the header (documented inline in the file itself) so this project's own C firmware can call it without C++ name-mangling. No logic changed. |
| `MLX90641_I2C_Driver.h` | This library's own platform-transport interface - same `extern "C"` treatment as the API header above. |
| `firmware_mlx90641_transport.c` | This board's own implementation of that 5-function transport interface, built on `firmware_expansion_i2c.c`'s own bit-banged primitives. |
| `firmware_mlx90641_app.c` / `.h` | Application-level capture and chunk-serving logic - 12 chunks (192 pixels at 16 per chunk, this sensor's own lower resolution vs. the other two). |
| `LICENSE_MELEXIS_APACHE2.0` | The library's own unmodified Apache-2.0 license text. |

**Why `melexis_mlx90641/` is the one C++ code in an otherwise all-C
project:** Melexis's own official MLX90641 library is written in C++
(unlike its MLX90640 and MLX90642 siblings, both plain C) - confirmed
against the real upstream repository before assuming otherwise. Rather
than hand-porting non-trivial calibration math to C (real risk of a
subtle error with no way to verify it against the original), this
project's own build process compiles `MLX90641_API.cpp` with
`arm-none-eabi-g++` (`-fno-exceptions -fno-rtti -fno-unwind-tables
-fno-threadsafe-statics`, keeping the embedded footprint minimal) and
links the result together with every other, plain-C object using `g++`
as the final linker driver - verified with a symbol-table check that
nothing ends up C++-name-mangled across that boundary before ever
trusting the real firmware build.

**`melexis_mlx90642/`** (32×24, 768px, onboard temperature calculation):

| File | Purpose |
|---|---|
| `MLX90642.h` / `.c`, `MLX90642_depends.h` | Melexis's own official library (Apache-2.0, plain C), vendored unmodified - identical copy to the expansion slave chip's own `src/F303-slave/melexis_mlx90642/`. |
| `firmware_mlx90642_transport.c` | This board's own implementation of a genuinely simpler 3-function transport (`I2CRead`/`I2CWrite`/`Wait_ms`) - this library's own `Config`/`I2CCmd`/`WakeUp` are already fully implemented inside the vendored `MLX90642.c` itself, each building its own exact byte buffer using this sensor's own documented opcodes and calling the generic `I2CWrite` below, so there's nothing left for a platform driver to guess at. |
| `firmware_mlx90642_app.c` / `.h` | Application-level capture logic - genuinely simpler than the other 2 sensors' own equivalents, since this sensor calculates temperature onboard itself rather than expecting the host to run a calibration library against a raw EEPROM dump (no `DumpEE`/`ExtractParameters`/`CalculateTo` sequence here at all). Uses `MLX90642_GetFrameData()` rather than the simpler `MLX90642_GetImage()` specifically so both raw and calibrated chunks can be served from one capture, matching the other 2 sensors' own chunk-serving shape. This sensor's own temperature scale (degC×50, confirmed against Melexis's own example code) is rescaled to this project's own `MLX_TEMP_SCALE` (degC×100) convention, so a host reading `REG_MLX_CALIBRATED_CHUNK` never needs to know which sensor actually answered it. |
| `LICENSE_MELEXIS_APACHE2.0` | The library's own unmodified Apache-2.0 license text. |

---

## 9. Related reference files

| File | Covers |
|---|---|
| `docs/CANBUS.TXT` | Every CAN ID, byte layout, and DLC - the authoritative wire protocol reference |
| `docs/EEPROM.TXT` | Complete F-RAM byte layout, including free tool configuration and peripheral info (sections 5-6) |
| `docs/ECOVIA.TXT` | Full ID↔tool table and pin-mutation logic |
| `docs/TOOLS.TXT` | High-level catalog of all 25 tools - what each does and which peripherals (main board and/or expansion board) it uses, without pin-level detail |
| `docs/PINOUT.TXT` | Complete MCU pinout, block by block |
| `docs/PINOUT_CONNECTORS.TXT` | Physical connector pinouts (CONN_DRILL, CONN_SEN, CONN_EXPANSION, etc.) |
| `BOM/BOM.TXT` | Bill of materials, cross-checked against the Eagle netlist |
