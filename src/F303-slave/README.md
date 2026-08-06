# URTC Expansion Slave Firmware — Technical Reference

**Project:** URTC (Universal Robot Tool Controller) - Expansion Ecosystem
**Author:** JuanenRac (Electro Hobby 3D) — electrohobby3d@gmail.com
**License:** This document is CC BY-SA 4.0; the source it describes
(`src/F303-slave/`) is GPL-3.0. The Melexis MLX90640 library this
firmware links against (`melexis/`) is Apache-2.0 - see that folder's
own `LICENSE_MELEXIS_APACHE2.0` and the repo root README's "License and
Copyright Notices" section for the full breakdown.

This document is the engineering-level reference for the application
firmware (`src/F303-slave/`, entry point `slave_main.c`) running on the
**expansion slave chip** - populated on the 2 ADVANCED expansion board
variants only (Advanced+TMC2209, Advanced+TMC5160A); the 2 BASIC
variants carry no MCU at all. For the main board's own firmware, which
this is a companion to rather than a variant of, see
`src/F303-master/README.md`. For pin-by-pin hardware detail, see
`docs/PINOUT_SLAVE.txt`.

---

## 1. Hardware platform

| | |
|---|---|
| MCU | STM32F303CBT6, LQFP48 |
| Core | ARM Cortex-M4F (hardware FPU, single-precision) - the MLX90640 calibration math (section 3 below) leans on this directly |
| Flash | 128 KB |
| RAM | 40 KB main SRAM (`0x20000000`) + 8 KB CCM at `0x10000000` (unused by this firmware, same as the main board's own firmware leaves its own CCM unused - nothing here needs the extra 8 KB enough to deal with the split) |
| Clock | No external crystal populated - `RCC_OSCILLATORTYPE_HSI` → `/2` → PLL ×16 → **64 MHz**. Deliberate design choice, not a placeholder: this chip does no CAN (the main board's own reason for needing HSE's tighter accuracy), only I2C, which tolerates HSI comfortably - see `slave_main.c`'s own top-of-file note, flagged for confirmation same as any hardware assumption in this project. |
| I2C1 | LINK bus to the main board's own STM32F303CC - this chip is SLAVE here, real hardware I2C (not bit-banged) |
| I2C2 | LOCAL sensor bus - this chip is MASTER here, drives the onboard ADS1115 + MLX90640 |
| TIM1 | Local PWM generation, 4 channels |

### Flash layout

Same partition scheme this chip's own bootloader manages - see
`src/F303-slave/boot/README.md` section 2 for the full table. This
firmware only ever runs from the main application slot
(`0x08005000`, 54 KB) that scheme copies a verified update into; it has
no independent existence outside that slot.

---

## 2. Why this chip exists — basic vs. advanced expansion boards

There are 4 expansion board variants total: 2 BASIC (driver only, no
MCU - STEP/DIR/EN routed straight from the main board's own
`EXP_TMC_STEP/DIR/EN` pins) and 2 ADVANCED (this chip, plus the driver).
Both come in a TMC2209 or TMC5160A driver option, independent of the
basic/advanced axis - see `BOM/BOM_EXPANSION_*.TXT` (4 files) for the
complete component lists.

**What actually requires the advanced variant:** the existing expansion
connector carries no dedicated PWM signal (confirmed by checking the
real connector pin list in `docs/PINOUT_CONNECTORS.TXT` before assuming
otherwise - an earlier design pass had assumed one existed and had to
walk that back), so any tool needing PWM generated locally - close to
the actuator, without cable-length/connector-routing jitter working
against sub-millisecond timing - needs this chip's own TIM1. Same for
the 2 local sensor chips: neither has any signal path to the main board
at all except through this chip relaying it.

| Doc tool | Needs advanced variant because |
|---|---|
| #7 Functional Testing Head | ADS1115 (I2C2) |
| #13 PCB Advanced Inspection | MLX90640 (I2C2) |
| #14 Solder Paste Jetting | Local PWM (sub-ms pulse precision) |
| #4 Spot Welder, #15 Ultrasonic Welder | Local PWM *if* the tool head is too far from the main board's own `CONN_T12` for that path to stay precise enough - both can also run entirely off the main board via `CONN_T12`, same as any other basic-variant tool, when that's not a concern |
| #12 Wire Harnessing/Crimping | Basic variant is enough - just needs the driver, no PWM or sensor chips |

---

## 3. Local sensor bus (I2C2) — ADS1115 and MLX90640

This chip is I2C2's **master**, polling both chips on request from the
link bus rather than continuously.

**ADS1115** (16-bit ADC) - straightforward pointer-register protocol,
verified against Texas Instruments' own datasheet: write the register
selector, then either write or read that register's 16 bits.
`REG_ADS_CONFIGURE` forwards the 16-bit config word near-verbatim into
the chip's own config register - this firmware doesn't interpret the
bitfields (gain, mux, data rate), it relays them, same generic-
passthrough philosophy as the main board's own SPI passthrough to the
expansion driver. Conversion results come back as raw ADC counts;
scaling to volts depends on whatever PGA gain the caller last
configured, which is why this stays raw rather than this firmware
guessing at a conversion.

**MLX90640** (32×24 thermal array) - built on Melexis's own official
library (`melexis/MLX90640_API.c`, Apache-2.0), not a hand-rolled
implementation. This sensor's own RAM register map and its interleaved
"chess pattern" sub-page capture scheme (each raw read only returns half
the 768 pixels; the library recombines two sub-page reads internally)
were genuinely not something this project could responsibly reconstruct
from partial datasheet fragments turned up across several searches - see
`slave_i2c_sensors.c`'s own top-of-file note for the full reasoning.
Integrating Melexis's own verified code instead of guessing turned out
simpler than expected: this firmware's own contribution is the 5-function
platform transport layer (`MLX90640_I2CInit/GeneralReset/Read/Write/
FreqSet`, per the library's own `MLX90640_I2C_Driver.h` interface) built
on the same I2C2 primitives ADS1115 uses, plus calling the library's
public sequence in the documented order: `MLX90640_DumpEE` +
`MLX90640_ExtractParameters` once at startup (calibration constants
don't change), then `MLX90640_TriggerMeasurement` + `MLX90640_GetFrameData`
+ `MLX90640_CalculateTo` per capture.

Both raw pixel data and calibrated temperatures are servable - not an
either/or choice. `REG_MLX_RAW_CHUNK` and `REG_MLX_CALIBRATED_CHUNK`
both read from the same underlying capture (`REG_MLX_TRIGGER_CAPTURE`
already has to hold the raw frame in RAM to calibrate it at all, so
exposing that same buffer costs essentially nothing extra), which
matters for verification once real hardware exists: calibrate the raw
frame independently on the host and compare against what this firmware
reports, rather than trusting a single code path with no way to cross-
check it. Calibrated values are `int16_t` centi-degrees C (value ×100),
not `float`, halving the byte count `float32` would cost across 768
pixels × 2 read types - see `MLX_TEMP_SCALE` in `slave_common.h`.

**Noted for revisiting once real hardware is available:** emissivity is
fixed at 0.95 (typical for non-reflective PCB/solder-mask surfaces, not
measured for this specific board) and reflected temperature is
approximated as ambient−8°C (a Melexis reference-example default, absent
a separate way to measure true reflected temperature) - both live as
named constants in `MLX90640_TriggerCapture()`, not buried inline.

---

## 4. I2C1 link-bus protocol (application mode)

Register-based, same "listen mode" HAL pattern as this chip's own
bootloader (`slaveboot_main.c`) reused rather than re-derived - request
the buffer's own max size on every receive, compute actual bytes
received from `XferCount` afterward, since register writes here range
from a bare 1-byte read-pointer-select up to `REG_PWM_CONFIGURE`'s own
4-byte payload.

This is a **separate register space** from the bootloader's own `REG_*`
numbering (`slaveboot_common.h`) - reusing low numbers isn't a
collision, since bootloader and application code are never running at
the same time; whichever one currently has control is the only `REG_*`
table that exists at that moment.

| Register | Direction | Payload | Purpose |
|---|---|---|---|
| `REG_APP_STATUS` (`0x00`) | read | 1 byte | Idle/busy |
| `REG_APP_VERSION` (`0x01`) | read | 10 bytes | HardwareID + firmware version, same field layout as the bootloader's own version-query response |
| `REG_ENTER_BOOTLOADER` (`0x02`) | write | 4 bytes | Magic payload `0xB0 0x07 0x1D 0x5A` (same constant the main board's own application checks for CAN `0x7F0`) resets into this chip's own bootloader - see section 5 below |
| `REG_MLX_TRIGGER_CAPTURE`-`REG_MLX_CALIBRATED_CHUNK` (`0x10`-`0x13`) | both | varies | MLX90640 capture control and chunked frame reads - see section 3 |
| `REG_ADS_CONFIGURE`-`REG_ADS_READ` (`0x20`-`0x22`) | both | varies | ADS1115 control and result read - see section 3 |
| `REG_PWM_CONFIGURE`-`REG_PWM_STOP` (`0x30`-`0x32`) | write | varies | Local PWM channel setup, pulse, and stop - see section 5 of `docs/PINOUT_SLAVE.txt` for which physical pins these 4 channels are |

Full field-by-field byte layout for every register: `slave_common.h`'s
own `REG_*` definitions, each documented inline at its `#define` - no
separate wire-protocol document exists yet for this chip, unlike the
main board's own `docs/CANBUS.TXT`.

Reaching these registers from the master board's own CAN bus (rather
than directly, which this chip has no way to do - no CAN peripheral of
its own) goes through the main board's own CAN-to-I2C bridge - see
`src/F303-master/README.md` section 8's own `firmware_can_slavebridge.c`
entry, and `docs/CANBUS.TXT`'s `0x210`-`0x221` command range.

## 5. Entering the bootloader

`REG_ENTER_BOOTLOADER` mirrors the main board's own CAN `0x7F0` handler
closely: same magic-payload requirement (an exact 4-byte match, not
just the register address alone, so a corrupted/malformed link-bus
transaction can't accidentally reset this chip mid-tool-operation), same
"shut down safely, then reset" ordering. This chip's own version is
simpler than the main board's - stop any active local PWM channel
(section 3's driver state doesn't need explicit shutdown; a reset
handles that on its own) - since it has far fewer actuators to bring to
a safe state than the main board's own 25 tool profiles.

---

## 6. Files (`src/F303-slave/`)

| File(s) | Purpose |
|---|---|
| `slave_main.c` | Entry point: clock/I2C1(slave)/I2C2(master)/TIM1/IWDG init, `main()`'s own loop. |
| `slave_common.h` | Shared types, the `REG_*` register map (section 4), sensor bus addresses, PWM channel count. No matching `.c`. |
| `slave_i2c_link.c` / `.h` | The I2C1 link-bus protocol handler (section 4) - the "listen mode" interrupt callbacks and the register dispatch table. |
| `slave_i2c_sensors.c` / `.h` | ADS1115 driver and the MLX90640 platform-transport layer (section 3), built on shared I2C2 master primitives. |
| `slave_pwm.c` / `.h` | TIM1 local PWM generation (section 5 of `docs/PINOUT_SLAVE.txt`) - channel configuration, timed pulses, and the millisecond countdown (`PWM_Tick`) that auto-stops a non-continuous pulse. |
| `melexis/MLX90640_API.c` / `.h`, `MLX90640_I2C_Driver.h` | Melexis's own official library (Apache-2.0), unmodified. Lives at `src/F303-slave/melexis/`, a sibling folder to the files above rather than nested inside them - kept as its own separate compilation unit rather than folded into this project's own source, since Apache-2.0 requires that code's own copyright notice stay intact and merging third-party code under a different license into this project's own files would obscure that. |

Plus `STM32F303CBTx_SLAVEAPP.ld` (the linker script - see section 1's
flash layout) and this README, both alongside the source files above.

## 7. Build system

`gcc-arm-none-eabi`, same toolchain and shared CMSIS device files as
this chip's own bootloader (see that README's own build system section)
- this chip's own `stm32f303xc.h`/`startup_stm32f303xc.s` are literally
the same files the main board's own application build already uses,
since both chips are the same xB/xC density line.

---

## 8. Related reference files

| File | Covers |
|---|---|
| `docs/PINOUT_SLAVE.txt` | Complete MCU pinout for this chip, block by block, including which tool-facing connector signals come from where |
| `BOM/BOM_EXPANSION_BASIC_TMC2209.TXT` | Basic variant, TMC2209 driver |
| `BOM/BOM_EXPANSION_BASIC_TMC5160A.TXT` | Basic variant, TMC5160A driver (includes the 8 external MOSFETs this driver requires and TMC2209 doesn't) |
| `BOM/BOM_EXPANSION_ADVANCED_TMC2209.TXT` | Advanced variant (this chip populated), TMC2209 driver |
| `BOM/BOM_EXPANSION_ADVANCED_TMC5160A.TXT` | Advanced variant, TMC5160A driver |
| `src/F303-slave/boot/README.md` | This chip's own bootloader |
| `src/F303-master/README.md` | The main board's own firmware, including its half of the I2C-link relationship this chip is the other end of |
