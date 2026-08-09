# URTC Expansion Slave Firmware — Technical Reference

**Project:** URTC (Universal Robot Tool Controller) - Expansion Ecosystem
**Author:** JuanenRac (Electro Hobby 3D) — electrohobby3d@gmail.com
**License:** This document is CC BY-SA 4.0; the source it describes
(`src/F303-slave/`) is GPL-3.0. The 3 Melexis MLX9064x-family
libraries this firmware links against (`melexis_mlx90640/`,
`melexis_mlx90641/`, `melexis_mlx90642/`) are all Apache-2.0 - see each
folder's own `LICENSE_MELEXIS_APACHE2.0` and the repo root README's
"License and Copyright Notices" section for the full breakdown.

This document is the engineering-level reference for the application
firmware (`src/F303-slave/`, entry point `slave_main.c`) running on the
**expansion slave chip** - populated on the 2 ADVANCED expansion board
variants only (Advanced+TMC2209, Advanced+TMC5160A); the 4 BASIC
variants (2 driver-only, 2 sensor-only - see the main board's own
EXPANSION.TXT) all carry no MCU at all. For the main board's own firmware, which
this is a companion to rather than a variant of, see
`src/F303-master/README.md`. For pin-by-pin hardware detail, see
`docs/PINOUT_SLAVE.txt`.

---

## 1. Hardware platform

| | |
|---|---|
| MCU | STM32F303CBT6, LQFP48 |
| Core | ARM Cortex-M4F (hardware FPU, single-precision) - the calibration math for whichever MLX9064x sensor is configured (section 3 below) leans on this directly |
| Flash | 128 KB |
| RAM | 40 KB main SRAM (`0x20000000`) + 8 KB CCM at `0x10000000` (unused by this firmware, same as the main board's own firmware leaves its own CCM unused - nothing here needs the extra 8 KB enough to deal with the split) |
| Clock | No external crystal populated - `RCC_OSCILLATORTYPE_HSI` → `/2` → PLL ×16 → **64 MHz**. Deliberate design choice, not a placeholder: this chip does no CAN (the main board's own reason for needing HSE's tighter accuracy), only I2C, which tolerates HSI comfortably - see `slave_main.c`'s own top-of-file note, flagged for confirmation same as any hardware assumption in this project. |
| I2C1 | LINK bus to the main board's own STM32F303CC - this chip is SLAVE here, real hardware I2C (not bit-banged) |
| I2C2 | LOCAL sensor bus - this chip is MASTER here, drives the onboard ADS1115 + whichever MLX9064x sensor is configured |
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
| #13 PCB Advanced Inspection | An MLX9064x-family thermal sensor (I2C2) - see section 3 below for which of the 3 family members this chip actually supports today |
| #14 Solder Paste Jetting | Local PWM (sub-ms pulse precision) |
| #4 Spot Welder, #15 Ultrasonic Welder | Local PWM *if* the tool head is too far from the main board's own `CONN_T12` for that path to stay precise enough - both can also run entirely off the main board via `CONN_T12`, same as any other basic-variant tool, when that's not a concern |
| #12 Wire Harnessing/Crimping | Basic variant is enough - just needs the driver, no PWM or sensor chips |

---

## 3. Local sensor bus (I2C2) — ADS1115 and the MLX9064x family

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

**MLX9064x thermal sensors** - only ONE of the 3 family members
(MLX90640, MLX90641, MLX90642) is ever actually populated on a real
board; `mlx_sensor_variant` (set by the main board over
`REG_MLX_SENSOR_VARIANT`, see section 4 below - this chip has no
persistent storage of its own to remember it across its own resets)
says which. `MLX_TriggerCapture`/`MLX_GetCaptureStatus`/
`MLX_GetRawChunk`/`MLX_GetCalibratedChunk` (`slave_i2c_sensors.c`) are
the single generic entry points the I2C1 link-bus protocol calls -
they branch on that variant internally, so neither that protocol nor
the main board's own CAN handlers on the other end of the link bus
ever need to know which sensor is actually behind them.

- **MLX90640** (32×24, 768px) - built on Melexis's own official library
  (`melexis_mlx90640/MLX90640_API.c`, Apache-2.0, plain C), not a hand-rolled
  implementation. This sensor's own RAM register map and its
  interleaved "chess pattern" sub-page capture scheme (each raw read
  only returns half the 768 pixels; the library recombines two
  sub-page reads internally) were genuinely not something this
  project could responsibly reconstruct from partial datasheet
  fragments - see `slave_i2c_sensors.c`'s own top-of-file note for the
  full reasoning.

- **MLX90641** (16×12, 192px) - same integration approach, a genuinely
  separate Melexis library (`melexis_mlx90641/MLX90641_API.h`/`.cpp`,
  also Apache-2.0), not a variant of the MLX90640's own one. **Written
  in C++ by Melexis**, unlike its MLX90640 sibling - confirmed against
  the real upstream repository before assuming otherwise, rather than
  hand-porting non-trivial calibration math to plain C (real risk of a
  subtle error with no way to verify it against the original). Only
  modification to the vendored files: an `extern "C"` linkage wrapper
  added to both headers (documented inline in the files themselves),
  compiled with `arm-none-eabi-g++`
  (`-fno-exceptions -fno-rtti -fno-unwind-tables
  -fno-threadsafe-statics`) and linked into the same firmware image as
  every other, plain-C object using `g++` as the final linker driver -
  the one C++ code in this chip's otherwise all-C firmware, same
  reasoning and same build-process change as the main board's own
  equivalent (`src/F303-master/README.md` section 8a).

- **MLX90642** (32×24, 768px, onboard temperature calculation) -
  architecturally different enough from the other two that it needs its
  own driver design, not just another variant of the MLX90640/90641
  pattern above: it calculates temperature onboard the sensor itself
  and reports already-linearized values over I2C, rather than needing
  this chip to run a calibration library against a raw EEPROM dump the
  way MLX90640/90641 both do. Genuinely simpler as a result - no
  `DumpEE`/`ExtractParameters`/`CalculateTo` sequence, just
  `MLX90642_GetFrameData()` per capture. Built on Melexis's own official
  library (`melexis_mlx90642/MLX90642.c`, Apache-2.0, plain C) - its own
  `Config`/`I2CCmd`/`WakeUp` functions are already fully implemented
  inside that vendored file itself (each builds its own exact byte
  buffer using this sensor's own documented opcodes), so this chip's
  own transport only needs to supply the 3 genuinely platform-specific
  pieces (`I2CRead`/`I2CWrite`/`Wait_ms`) rather than the 5-function
  shape the other 2 sensors need. This sensor's own temperature scale
  (degC×50, confirmed against Melexis's own example code) is rescaled
  to this chip's own `MLX_TEMP_SCALE` (degC×100) convention, so a host
  reading `REG_MLX_CALIBRATED_CHUNK` never needs to know which sensor
  actually answered it.

All 3 platform-transport layers (`MLX90640_I2CInit/GeneralReset/Read/
Write/FreqSet`, `MLX90641_I2CInit/GeneralReset/Read/Write/FreqSet`, and
MLX90642's own smaller `I2CRead`/`I2CWrite`/`Wait_ms` set) are built on
the same I2C2 primitives ADS1115 uses, in `slave_i2c_sensors.c`. MLX90640
and MLX90641 both call their own public sequence in the order each
one's own header documents: `DumpEE` + `ExtractParameters` once at
startup (calibration constants don't change), then
`TriggerMeasurement` + `GetFrameData` + `CalculateTo` per capture.
MLX90642 has no equivalent calibration-extraction step at all - see its
own note above for why.

Both raw pixel data and calibrated temperatures are servable - not an
either/or choice - for whichever sensor is actually configured.
`REG_MLX_RAW_CHUNK` and `REG_MLX_CALIBRATED_CHUNK` both read from the
same underlying capture (`REG_MLX_TRIGGER_CAPTURE` already has to hold
the raw frame in RAM to calibrate it at all, so exposing that same
buffer costs essentially nothing extra), which matters for
verification once real hardware exists: calibrate the raw frame
independently on the host and compare against what this firmware
reports, rather than trusting a single code path with no way to cross-
check it. Calibrated values are `int16_t` centi-degrees C (value ×100),
not `float`, halving the byte count `float32` would cost - see
`MLX_TEMP_SCALE` in `slave_common.h`. Chunk count differs by sensor:
48 chunks (768px ÷ 16) for MLX90640, 12 chunks (192px ÷ 16) for
MLX90641 - a chunk index a caller expects to mean "the other sensor's
own chunk count" simply reads back zeroed past the real range, no
separate error signaling needed for it.

**Noted for revisiting once real hardware is available:** emissivity is
fixed at 0.95 (typical for non-reflective PCB/solder-mask surfaces, not
measured for this specific board) and reflected temperature is
approximated as ambient−8°C (a Melexis reference-example default, absent
a separate way to measure true reflected temperature) - both live as
named constants in each sensor's own capture function
(`slave_i2c_sensors.c`), not buried inline. Same defaults for both
MLX90640 and MLX90641.

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
| `REG_MLX_TRIGGER_CAPTURE`-`REG_MLX_CALIBRATED_CHUNK` (`0x10`-`0x13`) | both | varies | Capture control and chunked frame reads for whichever MLX9064x sensor is configured - see section 3 |
| `REG_MLX_SENSOR_VARIANT` (`0x14`) | both | 1 byte | Which of the 3 MLX9064x family members is actually populated (`MLX_VARIANT_90640`/`90641`/`90642`) - set by the main board at boot (and on any later change), since this chip has no persistent storage of its own to remember it across its own resets. See section 3 |
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
| `slave_i2c_sensors.c` / `.h` | ADS1115 driver, both MLX90640 and MLX90641 platform-transport layers, and the generic `MLX_*` dispatch functions that branch on `mlx_sensor_variant` (section 3) - all built on shared I2C2 master primitives. |
| `slave_pwm.c` / `.h` | TIM1 local PWM generation (section 5 of `docs/PINOUT_SLAVE.txt`) - channel configuration, timed pulses, and the millisecond countdown (`PWM_Tick`) that auto-stops a non-continuous pulse. |
| `melexis_mlx90640/MLX90640_API.c` / `.h`, `MLX90640_I2C_Driver.h` | Melexis's own official MLX90640 library (Apache-2.0, plain C), unmodified. Lives at `src/F303-slave/melexis_mlx90640/`, a sibling folder to the files above rather than nested inside them - kept as its own separate compilation unit rather than folded into this project's own source, since Apache-2.0 requires that code's own copyright notice stay intact and merging third-party code under a different license into this project's own files would obscure that. |
| `melexis_mlx90641/MLX90641_API.h` / `.cpp`, `MLX90641_I2C_Driver.h` | Melexis's own official MLX90641 library (Apache-2.0, **C++** - a genuinely separate library from MLX90640's own, not a variant of it), at `src/F303-slave/melexis_mlx90641/`. Only modification: an `extern "C"` linkage wrapper on both headers (documented inline). This is the one C++ code in this chip's otherwise all-C firmware - see section 3's own note on why, and on how the build process handles the C/C++ mix. |
| `melexis_mlx90642/MLX90642.h` / `.c`, `MLX90642_depends.h` | Melexis's own official MLX90642 library (Apache-2.0, plain C), unmodified, at `src/F303-slave/melexis_mlx90642/`. Genuinely simpler transport interface than the other 2 sensors' own - see section 3's own note on why. |

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
