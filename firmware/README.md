# URTC Firmware — Technical Reference

This document is the engineering-level reference for `STM32F303CC.C` (the
application) and `BOOTLOADER.C` (the bootloader): hardware platform, the
ID-jumper tool-selection system, per-tool peripheral wiring, the update
mechanism, and the safety systems tying it together. For the wire-level
CAN protocol (every ID, byte layout, and DLC), see `CANBUS.TXT` — this
document explains *why* the system is built the way it is; `CANBUS.TXT`
is the byte-for-byte reference. For pin-by-pin detail, see `PINOUT.TXT`
and `PINOUT_CONNECTORS.TXT`. For how these binaries are actually compiled
and linked from source, see `BUILD_REPORT.md`.

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
| *This project's* bootloader entry | CAN command only (`0x7F0`, application resets itself into `BOOTLOADER.C`) or fresh chip (no valid application) — see section 4a below for what the physical **BOOT** button actually gates instead |

### Flash layout (fixed, shared knowledge between bootloader, application, and both PC tools)

```
0x08000000 ─┬─ Bootloader              32 KB   (BOOTLOADER.C)
0x08008000 ─┼─ Main application slot  112 KB   (STM32F303CC.C, what actually runs)
0x08024000 ─┴─ Backup/staging slot    112 KB   (OTA updates land here first)
0x08040000    (end of flash, 256 KB total)
```

Every one of these addresses is a compile-time constant duplicated in
four independent places that all have to agree: `BOOTLOADER.C` itself,
the linker scripts (`STM32F303CCTx_BOOTLOADER.ld` / `_APP.ld`), and
`tools/flasher/urtc_flasher.py`'s own `BOOTLOADER_FLASH_ADDR` /
`APP_FLASH_ADDR` / `*_MAX_SIZE` constants (overridable via
`urtc_config.json` if this is ever adapted to a different partition
scheme or chip variant).

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
`ECOVIA.TXT` for the full ID↔tool table) and 12–31 fall through to
"no tool assigned" — every actuator forced safe, every CAN command for
this profile silently ignored, but the board otherwise still boots,
still answers version/active-tool queries, and still shows a clear
warning on the OLED rather than doing anything undefined.

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
   `PINOUT_CONNECTORS.TXT`).
2. **Peripheral init** (`MX_ADC_Init`, `MX_TIM1_DrillLaserFan_Init`,
   `MX_TIM2_HotendFan_Init`, `MX_TIM3_Full_Init`) — only what the active
   tool needs gets clocked and configured at all; an unused peripheral is
   never touched.
3. **The main loop's CAN dispatch** — every incoming command is gated by
   `switch (active_tool)`; a command addressed to a tool ID this board
   isn't currently configured as is silently ignored by design, not an
   error condition (per `CANBUS.TXT`).

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
depth by `PINOUT_CONNECTORS.TXT` and `CANBUS.TXT`: the soldering iron and
3D-printer hotend are true **bang-bang thermal control**, not PID in the
classical sense — ±2 °C hysteresis, a hard 450 °C/300 °C ceiling
respectively, ADC-fault detection (a reading outside 15–4090 counts is
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
   `CANBUS.TXT`'s `0x7F5` documentation) and the main slot remains
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
for recovery, but a level below anything `BOOTLOADER.C` itself controls:

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
    at `0x08000000` - which on this board is always `BOOTLOADER.C`. This
    is the only path this project's own firmware ever sees, and
    everything described in sections 3-4 above happens downstream of
    this.
  - **`BOOT0` = 1 (button held at reset):** boot into the STM32's
    **factory-programmed System Memory bootloader** instead - ST's own
    ROM code, entirely separate from anything in this repository,
    supporting recovery over USB DFU or UART depending on the exact part.
    This project's `BOOTLOADER.C` never runs at all in this mode, and
    knows nothing about it.

In short: `0x7F0` (the CAN command) gets the *application* to voluntarily
jump into *this project's own* `BOOTLOADER.C` - useful for a completely
normal, healthy board that just needs a new firmware image. The **BOOT**
button is a hardware-level escape hatch one step below that: relevant if
the flash content itself is suspect enough that you need ST's own
factory recovery tool instead, which this project's CAN protocol can't
reach by definition (it hasn't loaded yet at that point). `PC13` (ID4)
and `BOOT0` are electrically independent nets on this board (confirmed
against the netlist), so pressing **BOOT** never interferes with tool
identification.

---

## 5. CAN protocol — summary (see `CANBUS.TXT` for the full byte-level reference)

- **500 kbit/s, standard 11-bit IDs.** No extended (29-bit) IDs are used
  anywhere in this protocol.
- **ID ranges:** `0x095` (max-priority scan-probe event) · `0x100`/`0x110`/`0x111`
  (global commands, valid regardless of active tool) · `0x120`–`0x179`
  (per-tool commands/telemetry) · `0x7F0`–`0x7FA` (bootloader/version).
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

## 6. Build system

Toolchain, HAL/CMSIS sourcing, linker scripts, and the one real
compile-time bug this project's own build process caught (a switch-
statement scope violation, invisible to any manual code review and only
caught by an actual compiler) are documented in full in `BUILD_REPORT.md`.
In short: `arm-none-eabi-gcc`, STM32CubeF3's HAL/CMSIS pulled from ST's
own GitHub repos, and hand-written linker scripts matching the flash
layout in section 1 above exactly.

---

## 7. Related reference files

| File | Covers |
|---|---|
| `CANBUS.TXT` | Every CAN ID, byte layout, and DLC - the authoritative wire protocol reference |
| `ECOVIA.TXT` | Full ID↔tool table and pin-mutation logic |
| `PINOUT.TXT` | Complete MCU pinout, block by block |
| `PINOUT_CONNECTORS.TXT` | Physical connector pinouts (CONN_DRILL, CONN_SEN, CONN_EXPANSION, etc.) |
| `BOM.TXT` | Bill of materials, cross-checked against the Eagle netlist |
| `BUILD_REPORT.md` | How these binaries are actually compiled and linked from this source |
| `AUDIT_v1.0_SIMULATION.md` | Full v1.0 code/consistency audit and simulation report |
| `docs/JTAG_SWD_FEASIBILITY.md` | SWD/JTAG programming feasibility study |
