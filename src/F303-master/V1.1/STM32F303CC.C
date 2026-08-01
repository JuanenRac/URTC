// =============================================================================
// URTC - Universal Robot Tool Controller - Firmware
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// NOTE: this license covers the firmware source only. The URTC hardware
// designs (Eagle schematic/board/gerbers, 3D-printable parts) are licensed
// separately under CERN-OHL-S v2, and the project documentation under
// CC BY-SA 4.0 - see the repository README for the full breakdown.
// =============================================================================

#include "stm32f3xx_hal.h"
#include <string.h>
#include <math.h>

// =============================================================================
// TARGET MCU: STM32F303CCT6 (LQFP48 Package - 48 Pins)
// ENVIRONMENT: ASSEMBLY LINE / UNIVERSAL TOOL-HEAD CONTROLLER (URTC v1.1)
// PROJECTS: URTC/HYDRA-UMC (JUANENBOT/PAROL6)
// AUTHOR: JuanenRac (Electro Hobby 3D) - electrohobby3d@gmail.com
// SAFETY HARDWARE ALIGNMENT WITH PINOUT V1.0 AND ECOSYSTEM DOC
// =============================================================================

// Firmware identity - must match BOOTLOADER.C's THIS_HARDWARE_ID exactly
// (same board, same check). Reported over CAN via 0x7F8/0x7F9 - see
// HAL_CAN_RxFifo0MsgPendingCallback - so a host can ask "what's currently
// running" before deciding what to flash, without needing to trigger a
// reset into the bootloader first just to find out.
#define THIS_HARDWARE_ID     0x0303CC01UL // STM32F303CCT6, URTC board revision 1
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 1

// ID configuration matrix (physical tool-head address readout)
#define ID0_PIN         GPIO_PIN_0  // PF0 - Bit 0
#define ID0_PORT        GPIOF
#define ID1_PIN         GPIO_PIN_1  // PF1 - Bit 1
#define ID1_PORT        GPIOF
#define ID2_PIN         GPIO_PIN_4  // PB4 - Bit 2
#define ID2_PORT        GPIOB
#define ID3_PIN         GPIO_PIN_7  // PB7 - Bit 3
#define ID3_PORT        GPIOB
#define ID4_PIN         GPIO_PIN_13 // PC13 - Bit 4 (32-tool addressing)
#define ID4_PORT        GPIOC

// Dedicated OLED interface via hardware I2C2 (PA9/PA10, AF4)
#define OLED_SCL_PIN    GPIO_PIN_9   // PA9  (Pin 19) -> I2C2_SCL (AF4) - confirmed against DS9118 Table 14
#define OLED_SDA_PIN    GPIO_PIN_10  // PA10 (Pin 20) -> I2C2_SDA (AF4) - confirmed against DS9118 Table 14
#define OLED_PORT       GPIOA
#define OLED_I2C_ADDR   0x78        // (0x3C << 1)
// FM24CL64B parameter-persistence F-RAM (64Kbit/8KB), sharing the OLED's
// own hardware I2C2 bus rather than a new one - this chip only has two
// I2C peripherals total (confirmed against ST's own STM32F303xB/xC
// product page: "up to two I2Cs" - there's no I2C3 on this density
// variant at all, a different situation from some larger STM32F3
// parts that do have one), and this design doesn't use the other one
// (I2C1) at all - its dedicated pin-pairs (PA14/15, PB6/7, PB8/9) are
// all already spoken for by other functions on this board. A third,
// bit-banged bus - the same approach already used for CONN_EXPANSION's
// own I2C - isn't an option either: every free GPIO pin on this board
// is already spoken for by that same bus. Sharing the OLED's bus is a
// deliberate choice: this F-RAM is a core board component, so it makes
// more sense paired with the OLED (another core component, and a
// device this firmware already knows how to share a bus with correctly)
// than routed through the expansion connector's own bus, which is
// reserved for whatever an unknown future expansion board turns out to
// need. Address 0x50 (A0/A1/A2
// tied to GND on the board - the most common wiring for a single device)
// doesn't collide with the OLED's own fixed 0x3C.
//
// F-RAM, not EEPROM: the FM24CL64B is pin/protocol-compatible with a
// serial I2C EEPROM of the same capacity (same control-byte/address
// scheme, confirmed against the official datasheet), but the underlying
// memory technology is genuinely different in two ways that matter here -
// writes complete at bus speed with no internal write cycle to wait for
// ("NoDelay" writes, per the datasheet - by the time a new bus
// transaction could be shifted in, the previous write is already done),
// and endurance is measured in the trillions of cycles rather than the
// ~1 million a real EEPROM would have. See FRAM_WriteBytes() below for
// what this actually changes in the code.
#define FRAM_I2C_ADDR 0xA0          // (0x50 << 1)
#define FRAM_WRITE_CHUNK 32         // not a hardware page-boundary limit the way EEPROM has (F-RAM has none) - just a conservative, arbitrary transaction size
#define FRAM_SIZE_BYTES 8192        // 64Kbit

// CAN BUS physical pin assignment (AF9)
#define CAN_RX_PIN      GPIO_PIN_11 // PA11 (Pin 21) -> CAN_RX (AF9)
#define CAN_TX_PIN      GPIO_PIN_12 // PA12 (Pin 22) -> CAN_TX (AF9)
#define CAN_GPIO_PORT   GPIOA

// Coexisting illumination channels: status LED on PA7/SPI1+DMA, ring on
// PB1/bit-banged - kept on two independent pins so CONN_LED1 and CONN_LED2
// don't need a PCB trace joining them in series.
// PA2 (Pin 8): free - not used by the status LED, which lives on PA7/SPI1 instead.
#define LED_RING_PIN     GPIO_PIN_1  // PB1 (Pin 15) -> Bit-Banging Asm NOPs (Camera Ring)
#define LED_RING_PORT    GPIOB

// Expansion connector's SPI bus (CONN_EXPANSION pins 8/9/12/13) - bit-banged,
// not a hardware SPI peripheral. Reasoning (why not SPI2 or SPI3, both of
// which exist on this chip but don't fit this board without moving other
// already-committed pins): SPI2's default pins are PB12/13/14/15, which
// this board already uses for EXP_TMC_STEP/DIR/EN/EXP_PWM. SPI3's default
// pins are PC10/PC11/PC12, which - confirmed against the official
// STM32F303xB/xC datasheet's LQFP48-specific pin table - aren't present on
// this 48-pin package at all, only on the larger LQFP64/LQFP100 variants
// of the same silicon. See PINOUT_CONNECTORS.TXT's CONN_EXPANSION note for
// the full reasoning.
//
// MOSI is PB15 - a natural fit for sharing with a TMC2209 populated
// instead of a TMC5160 on the same connector, since a TMC2209 needs only
// its single UART pin beyond STEP/DIR/EN, and the two chips are never
// populated simultaneously on the same expansion board.
#define EXP_SPI_CS_PIN    GPIO_PIN_2   // PA2
#define EXP_SPI_CS_PORT   GPIOA
#define EXP_SPI_SCK_PIN   GPIO_PIN_2   // PB2
#define EXP_SPI_SCK_PORT  GPIOB
#define EXP_SPI_MISO_PIN  GPIO_PIN_8   // PB8
#define EXP_I2C2_SCL_PORT GPIOB
#define EXP_I2C2_SCL_PIN  GPIO_PIN_10  // PB10 - bit-banged, see MX_ExpansionI2C_Init's own note on why
#define EXP_I2C2_SDA_PORT GPIOB
#define EXP_I2C2_SDA_PIN  GPIO_PIN_11  // PB11 - bit-banged, see MX_ExpansionI2C_Init's own note on why
#define EXP_SPI_MISO_PORT GPIOB
#define EXP_SPI_MOSI_PIN  GPIO_PIN_15  // PB15
#define EXP_SPI_MOSI_PORT GPIOB

// TMC_DIAG0 - combined stall/fault diagnostic line, shared between the
// onboard TMC2209 (U6's own DIAG pin) and whatever driver populates the
// expansion connector, through a common-cathode dual diode (D4,
// MMBD4148CC): each driver's DIAG output feeds its own anode, the shared
// cathode feeds this pin plus an external 10K pull-down (R50) to GND.
// Diode-OR, not a direct tie, specifically because DIAG is a push-pull
// (actively-driven) output on the TMC2209 - confirmed against its own
// datasheet, which never describes this pin as open-drain the way it
// does other pins where that applies - so two such outputs tied
// directly together would fight if one asserts while the other doesn't,
// with real risk of damaging either driver. The diode-OR sidesteps that
// entirely: each output can only ever pull its own diode's cathode
// side high, never fight the other one's low state.
//
// Active-HIGH as a result (either driver's DIAG going high pulls the
// shared node high through its diode) - the OPPOSITE polarity from an
// single open-drain TMC5160 DIAG0 alone, which pulls active-LOW against
// a pull-up. A TMC5160 populating the expansion connector needs its own
// GCONF.diag0_int_pushpull bit set to 1 (push-pull/active-high) for its
// side of this diode-OR to work correctly - left at its power-on
// default of 0 (open-drain/active-low), its DIAG0 output can only pull
// low or float, never actively drive its diode's anode high, so a
// stall on that side would never register here at all. This firmware
// has no way to set that bit itself - the expansion SPI bus is a
// generic passthrough (0x180/0x181), not aware of TMC5160 registers by
// design (see that command's own note) - so getting this right is up
// to whatever configures the expansion driver, whether that's a script
// sending the right SPI frame over 0x180 or a future expansion board's
// own onboard MCU.
// Uses PC15, one of two general-purpose, EXTI-capable pins reserved for
// exactly this kind of fast interrupt-driven input - which is exactly
// what DIAG0 is. Confirmed no EXTI conflict: nothing else in this
// firmware uses bit 15 of SYSCFG_EXTICR (shared across every port for
// that bit number - see the EXP_GPIO1/2 note this replaces in
// PINOUT.TXT).
#define TMC_DIAG0_PIN  GPIO_PIN_15  // PC15 (was EXP_GPIO2)
#define TMC_DIAG0_PORT GPIOC

// DIAGNOSTIC PIN (Visual indicator / Heartbeat)
#define LED_DIAG_PIN    GPIO_PIN_15 // PA15 (Pin 25) -> Diagnostic digital output
#define LED_DIAG_PORT   GPIOA

// Fixed dedicated peripherals (T12 soldering / 3D hotend block)
#define T12_PWM_PIN     GPIO_PIN_1  // PA1 (Pin 7) -> Soldering iron PWM / Extruder heater
#define T12_PWM_PORT    GPIOA
#define T12_ADC_PIN     GPIO_PIN_0  // PA0 (Pin 6) -> ADC Channel 0 (T12 thermocouple / NTC 100k)

// DYNAMIC CENTRAL PIN SWITCHING MATRIX (PORT B)
#define STEP_PIN         GPIO_PIN_3  // PB3 (Pin 26) - Motor mode: Pulse output
#define STEP_PORT        GPIOB
#define TCRT_D0_DIG_PIN  GPIO_PIN_3  // PB3 (Pin 26) - Vacuum pickup sensor mode: LM393 digital input
#define TOUCH_IN_PIN     GPIO_PIN_3  // PB3 (Pin 26) - Probe mode: EXTI3 impact input

#define DIR_PIN          GPIO_PIN_5  // PB5 (Pin 28) - Motor mode: Direction output
#define DIR_PORT         GPIOB
// NOTE: PB5 is not used in AOI Inspection mode. That tool's light is the LED
// ring on CONN_LED2 (see ring_pixels[] and Update_AllLEDs_SPI_DMA).

#define TMC_ENN_PIN      GPIO_PIN_6  // PB6 - Stepper driver enable (Active Low)
#define TMC_ENN_PORT     GPIOB
#define DRILL_BRAKE_PIN GPIO_PIN_9 // PB9 - Drill's own BrakeIN line on CONN_DRILL, independent from the stepper tools' enable
#define DRILL_BRAKE_PORT GPIOB
#define LASER_SAFETY_PIN GPIO_PIN_6  // PB6 (Pin 29) - Laser mode: Physical safety interlock

// SPECIAL REASSIGNED PERIPHERALS 
#define LED_SPI_MOSI_PIN  GPIO_PIN_7  // PA7 (Pin 13) -> SPI1_MOSI (AF5) for the status/ring LED chain (see MX_SPI1_Init) - not drill or laser PWM, despite the pin's original silkscreen/net intent; that PWM moved to PA8/TIM1_CH1 once this pin took over SPI1_MOSI duty
#define DRILL_PWM_PORT GPIOA
#define LASER_PWM_PORT   GPIOA

#define TCRT_A0_ADC_PIN  GPIO_PIN_0  // PB0 (Pin 14) -> Sensor mode: Analog ADC Channel 11 input
#define TCRT_A0_ADC_PORT GPIOB

// Smart drill tool-head auxiliary wiring
#define DRILL_FGIN_PIN   GPIO_PIN_3  // PA3 (Pin 9)  -> Tachometer input (EXTI3)

// Hotend cooling fan (3D printer mode only) - CONN_FAN1, uses 2
// free pins PA5/PA6. Distinct from the layer fan (PA7/PA3): this one cools the
// hotend/heatsink itself, the layer fan cools the printed part.
#define HOTEND_FAN_PWM_PIN   GPIO_PIN_5  // PA5 (Pin 11) -> TIM2_CH1 (AF1)
#define HOTEND_FAN_PWM_PORT  GPIOA
#define HOTEND_FAN_FG_PIN    GPIO_PIN_6  // PA6 (Pin 12) -> Tachometer input (EXTI6)
#define HOTEND_FAN_FG_PORT   GPIOA
#define DRILL_FGIN_PORT  GPIOA
#define DRILL_FRIN_PIN   GPIO_PIN_4  // PA4 (Pin 10) -> BL4260 rotation direction output
#define DRILL_FRIN_PORT  GPIOA

// =============================================================================
// 2. ENUMERATIONS AND CONTROL STRUCTURES
// =============================================================================
typedef enum {
    TOOL_SOLDERING_IRON           = 0,  
    TOOL_PASTE_DISPENSER  = 1,  
    TOOL_LIQUID_DISPENSER    = 2,  
    TOOL_SCREWDRIVER     = 3,  
    TOOL_VACUUM_PICKUP            = 4,  
    TOOL_DRILL            = 5,  
    TOOL_GRIPPER_GIMBAL     = 6,  
    TOOL_GRIPPER_NEMA       = 7,  
    TOOL_AOI_INSPECTION     = 8,  
    TOOL_LASER_ENGRAVER     = 9,  
    TOOL_3D_PRINTER       = 10, 
    TOOL_SCAN_PROBE      = 11, 
    // The 5-bit ID scheme can read 0-31, but only 0-11 map to a real tool
    // head today. 12-31 (no jumpers matching any assigned tool - most
    // commonly unsoldered jumpers, a wiring fault, or simply an ID not yet
    // assigned to a tool) get their own explicit, deliberately inert state
    // here rather than silently mapping to any real tool - falling through
    // to an existing tool profile would make no sense: it isn't that tool,
    // and every other subsystem in the firmware would still treat it as if
    // it were one. This also leaves headroom for
    // up to 20 more tool heads to be added later without another ID-scheme
    // change.
    TOOL_INVALID         = 12
} ToolMode_t;

// Snapshot of parameters worth surviving a power loss, written to the
// FM24CL64B F-RAM - see SavedState_Load()/SavedState_MaybeSave() in
// section 14. __attribute__((packed)) keeps the size predictable (no
// compiler-inserted padding) since this same layout has to match on both
// the write and read side, and the size feeds directly into the checksum
// calculation.
typedef struct __attribute__((packed)) {
    uint32_t magic;              // confirms this F-RAM was actually written by this firmware, not blank/garbage
    uint8_t  struct_version;     // bumped if this layout ever changes, so old/new firmware can tell mismatched saves apart
    uint8_t  active_tool_at_save; // sanity check against the CURRENT active_tool at load time - see the load-time note
    uint16_t solder_setpoint;
    uint16_t printer_nozzle_setpoint;
    uint8_t  drill_speed;
    uint8_t  drill_direction;
    uint8_t  laser_power;
    uint8_t  laser_interlock;
    uint8_t  layer_fan_power;
    uint8_t  hotend_fan_power;
    uint8_t  status_r, status_g, status_b;
    uint8_t  ring_r, ring_g, ring_b, ring_on;
    uint8_t  oled_night_mode;
    uint8_t  had_critical_error; // was system_error_flag set at the moment of the last save, before whatever powered the board off?
    uint8_t  expansion_board_type; // 0=none, 1=basic TMC2209, 2=basic TMC5160A,
                                    // 3=advanced TMC2209+STM32F051T8, 4=advanced
                                    // TMC5160A+STM32F051T8 - see EXPANSION.TXT.
                                    // Set explicitly by the host (0x1A0/0x1A1 -
                                    // see CANBUS.TXT), not auto-detected - there's
                                    // no electrical way to sense which variant (or
                                    // none) is plugged in, so this has to be told
                                    // rather than discovered.
    uint8_t  free_tool_selection;  // Only consulted when the ID-jumper reading is
                                    // 0x1F (11111b, all 5 jumpers installed) - see
                                    // Identify_PhysicalTool()'s own id==31 branch.
                                    // 0 = no tool selected
                                    // (same safe/error state as an unrecognized ID
                                    // reading), 1-12 = one of the 12 currently
                                    // supported tool profiles (stored as id+1, not
                                    // the raw 0-11 ToolMode_t value, so 0 can
                                    // unambiguously mean "nothing chosen yet"
                                    // rather than colliding with tool 0, the
                                    // soldering iron). Set via CAN (0x1A2/0x1A3 -
                                    // see CANBUS.TXT), same reasoning as
                                    // expansion_board_type above.
    uint8_t  device_serial_number; // 0-255, default 0. Purely a host-assigned
                                    // label for telling multiple URTC boards
                                    // apart on the same CAN bus - this firmware
                                    // never reads its own value for any decision,
                                    // it's answered purely for a host/Flasher/
                                    // Tester's own bookkeeping. Set via CAN
                                    // (0x1A4/0x1A5 - see CANBUS.TXT), same
                                    // reasoning as free_tool_selection above. See
                                    // EEPROM.TXT section 6 for the full mechanism
                                    // and why this exists (multiple identically-
                                    // configured boards on one bus were otherwise
                                    // indistinguishable from each other).
    uint8_t  checksum;           // CRC-8 (polynomial 0x07, CRC-8/SMBUS) over every byte above - proportionate for "was this corrupted", not a security boundary the way the OTA update's HMAC is
} SavedState_t;
#define SAVEDSTATE_MAGIC 0x55525443UL // 'URTC' as bytes, arbitrary but distinctive - astronomically unlikely to appear by chance in an uninitialized F-RAM
#define SAVEDSTATE_VERSION 5
// Fixed hardware/firmware identity constant, not a F-RAM field - the same
// for every URTC firmware build, so there's nothing to persist or make
// configurable. 0x03 per EEPROM.TXT section 6's peripheral type
// numbering (0x04-0x05 reserved for expansion-board firmwares, which are
// a different chip - STM32F051T8 - running different firmware entirely,
// not something this codebase reports on their behalf).
#define URTC_PERIPHERAL_TYPE 0x03
#define SAVEDSTATE_FRAM_ADDR 0 // lives at the very start of the F-RAM - nothing else uses this chip yet

// Verified exactly 3 bytes with this toolchain (all-uint8_t members need
// no alignment padding), but __attribute__((packed)) makes that
// explicit rather than relying on it being true "by coincidence" -
// cheap insurance against a future compiler/platform silently adding
// padding and breaking the DMA dump's byte-for-byte protocol match.
typedef struct __attribute__((packed)) {
    uint8_t R; uint8_t G; uint8_t B;
} LED_RGB_t;

// Full standard 5x7 alphanumeric character font (no clipping)
const uint8_t Font5x7[] = {
    0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x4f,0x00,0x00, 0x07,0x00,0x07,0x00,0x00,
    0x14,0x7f,0x14,0x7f,0x14, 0x24,0x2a,0x7f,0x2a,0x12, 0x23,0x13,0x08,0x64,0x62,
    0x36,0x49,0x55,0x22,0x50, 0x00,0x05,0x03,0x00,0x00, 0x00,0x1c,0x22,0x41,0x00,
    0x00,0x41,0x22,0x1c,0x00, 0x14,0x08,0x3e,0x08,0x14, 0x08,0x08,0x3e,0x08,0x08,
    0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00, 0x00,0x60,0x60,0x00,0x00,
    0x3e,0x51,0x49,0x45,0x3e, 0x00,0x42,0x7f,0x40,0x00, 0x42,0x61,0x51,0x49,0x46,
    0x21,0x41,0x45,0x4b,0x31, 0x18,0x14,0x12,0x7f,0x10, 0x27,0x45,0x45,0x45,0x39,
    0x3c,0x4a,0x49,0x49,0x30, 0x01,0x71,0x09,0x05,0x03, 0x36,0x49,0x49,0x49,0x36,
    0x06,0x49,0x49,0x29,0x1e, 0x00,0x36,0x36,0x00,0x00, 0x00,0x56,0x36,0x00,0x00,
    0x08,0x14,0x22,0x41,0x00, 0x14,0x14,0x14,0x14,0x14, 0x00,0x41,0x22,0x14,0x08,
    0x02,0x01,0x51,0x09,0x06, 0x32,0x49,0x79,0x41,0x3e, 0x7e,0x11,0x11,0x11,0x7e,
    0x7f,0x49,0x49,0x49,0x36, 0x3e,0x41,0x41,0x41,0x22, 0x7f,0x41,0x41,0x22,0x1c,
    0x7f,0x49,0x49,0x49,0x41, 0x7f,0x09,0x09,0x09,0x01, 0x3e,0x41,0x49,0x49,0x7a,
    0x7f,0x08,0x08,0x08,0x7f, 0x00,0x41,0x7f,0x41,0x00, 0x20,0x40,0x41,0x3f,0x00,
    0x7f,0x08,0x14,0x22,0x41, 0x7f,0x40,0x40,0x40,0x40, 0x7e,0x02,0x0c,0x02,0x7e,
    0x7f,0x04,0x08,0x10,0x7f, 0x3e,0x41,0x41,0x41,0x3e, 0x7f,0x09,0x09,0x09,0x06,
    0x3e,0x41,0x51,0x21,0x5e, 0x7f,0x09,0x19,0x29,0x46, 0x46,0x49,0x49,0x49,0x31,
    0x01,0x01,0x7f,0x01,0x01, 0x3f,0x40,0x40,0x40,0x3f, 0x1f,0x20,0x40,0x20,0x1f,
    0x3f,0x40,0x38,0x40,0x3f, 0x63,0x14,0x08,0x14,0x63, 0x07,0x08,0x70,0x08,0x07,
    0x61,0x51,0x49,0x45,0x43, 0x5c,0x3a,0x2d,0x20,0x5b,
    // Extends through 95 ('_'), matching the range check below. '[', '\',
    // ']', '^' get blank glyphs, consistent with how any other character
    // outside the font's range already renders; '_' (bottom row only,
    // matching this font's bit0=top/bit6=bottom convention already visible
    // in every letter above) is the one that's actually useful - version
    // strings and similar are a natural place an underscore could show up.
    0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00, 0x40,0x40,0x40,0x40,0x40
};

// =============================================================================
// 3. SYSTEM GLOBAL VARIABLES (VOLATILE MULTI-THREAD)
// =============================================================================
volatile ToolMode_t active_tool;

// Parameter persistence (FM24CL64B F-RAM) - see section 14.
SavedState_t recovered_state;      // populated once at boot by SavedState_Load(); had_valid_recovered_state says whether it's meaningful
uint8_t had_valid_recovered_state = 0;
volatile uint8_t expansion_board_type = 0; // 0=none (safe default until the host
                                            // explicitly sets otherwise) - restored
                                            // from F-RAM at boot if a valid save
                                            // exists, updated by 0x1A0 at runtime
volatile uint8_t free_tool_selection = 0;  // 0=none selected (safe default) - restored
                                            // from F-RAM at boot if a valid save
                                            // exists, updated by 0x1A2 at runtime.
                                            // See the SavedState_t field's own
                                            // comment for the encoding (stored as
                                            // id+1, not the raw ToolMode_t value).
volatile uint8_t device_serial_number = 0; // 0=unassigned (default) - restored from
                                            // F-RAM at boot if a valid save exists,
                                            // updated by 0x1A4 at runtime. Purely a
                                            // host-assigned label - never consulted
                                            // by this firmware for any decision.
uint8_t raw_id_pin_value = 0xFF;           // The actual 5-bit ID-jumper reading
                                            // (0-31) from Identify_PhysicalTool(),
                                            // kept separately from active_tool so
                                            // that same function's id==31 branch can tell
                                            // "the jumpers read 0x1F" apart from
                                            // "active_tool has already been resolved
                                            // to something else since". 0xFF is not
                                            // a valid 5-bit reading - never mistaken
                                            // for a real one.
SavedState_t last_saved_state;     // mirrors what's currently written to the F-RAM, for change detection in SavedState_MaybeSave()
uint32_t last_fram_write_tick = 0;

CAN_HandleTypeDef hcan;
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];

TIM_HandleTypeDef htim3;     
TIM_HandleTypeDef htim1;     // Drill/laser/layer-fan PWM (PA8, TIM1_CH1) - replaces
                              // both the old TIM3_CH2 and TIM17_CH1 usage now that
                              // PA7 carries SPI1_MOSI instead
TIM_HandleTypeDef htim2;     // Dedicated to the hotend fan's PWM (25kHz, PA5/TIM2_CH1)
SPI_HandleTypeDef hspi1;     // Drives both status LED and ring via DMA (PA7/MOSI)
DMA_HandleTypeDef hdma_spi1_tx;
I2C_HandleTypeDef hi2c2;     // Hardware I2C2 for the OLED+F-RAM (PA9/PA10 - AF4 on these pins is I2C2, not I2C1, confirmed against DS9118)
// Expansion connector's own I2C bus (PB10/PB11) is bit-banged, not a
// hardware peripheral - see MX_ExpansionI2C_Init's own note on why, and
// ExpansionI2C_Start/Stop/WriteByte/ReadByte for the implementation. No
// HAL handle needed for a bit-banged bus.
ADC_HandleTypeDef hadc;
IWDG_HandleTypeDef hiwdg;

volatile LED_RGB_t led_state_pixel;      
volatile LED_RGB_t ring_pixels[8];        

volatile uint32_t steps_remaining = 0;
// Which half of the current step pulse the ISR is in - promoted from a
// function-local static so a new move command can reset it (see the 0x120/
// 0x170 handlers): if left at 1 (high) from the tail end of a previous move,
// the very first tick of a fresh move would perform a falling edge instead
// of the intended rising edge, misaligning that first step.
volatile uint8_t step_pulse_high = 0;
volatile uint32_t total_steps_setpoint = 1; 
volatile uint16_t current_temperature = 0;
volatile uint16_t target_temperature = 0;
volatile uint16_t sensor_analog_reading = 0;
volatile uint8_t sensor_digital_reading = 0;
volatile uint8_t drill_speed = 0;       
volatile uint32_t drill_rpm_pulses = 0; 
volatile uint32_t drill_actual_rpm = 0;

// Layer fan (TOOL_3D_PRINTER mode) - reuses PA8 (PWM, TIM1_CH1) and
// PA3 (FG, EXTI3), the same pins as DRILL_PWM/DRILL_FGIN, since both
// tools are never active at the same time (one board = one physical tool).
volatile uint8_t layer_fan_duty = 0;     // 0-255 setpoint received via CAN (0x173)
volatile uint32_t layer_fan_rpm_pulses = 0;          // FG edge accumulator between telemetry cycles
volatile uint32_t layer_fan_actual_rpm = 0;          // Calculated RPM, ready for 0x177 telemetry
volatile uint8_t system_error_flag = 0; 
// Defers movement commands until the boot splash completes - TIM3 (step
// generation) and CAN are both already live during the 5s splash, so a
// 0x120/0x170 move command arriving in that window would otherwise start
// producing real step pulses immediately, moving a physical motor while
// the screen still shows the boot logo. Telemetry and lighting are
// unaffected by this flag.
volatile uint8_t boot_sequence_active = 1;
// CAN bus error indicator - separate from system_error_flag since
// a transient bus fault that AutoBusOff recovers from on its own shouldn't
// necessarily block all future commands the way a declared critical error
// does; this just drives a visible OLED indicator.
volatile uint8_t can_bus_error_flag = 0;

volatile uint8_t laser_power_setpoint = 0;
volatile uint32_t laser_last_kick_tick = 0;
volatile uint32_t drill_last_kick_tick = 0;
volatile uint32_t layer_fan_last_kick_tick = 0; // Layer fan communication watchdog (does not touch PB6)

// Hotend cooling fan (3D printer mode, CONN_FAN1, PA5/PA6) - separate from the
// layer fan above. Same telemetry/watchdog pattern, own set of variables.
volatile uint8_t hotend_fan_duty = 0;
volatile uint32_t hotend_fan_rpm_pulses = 0;
volatile uint32_t hotend_fan_actual_rpm = 0;
volatile uint32_t hotend_fan_last_kick_tick = 0;
// Communication watchdog for both thermal tools (soldering iron, 3D printer
// hotend) - without it, losing CAN comms while a heater is actively on
// would leave the bang-bang loop using the last target_temperature
// forever, with nothing to cut power.
volatile uint32_t solder_iron_last_kick_tick = 0;
volatile uint32_t hotend_heater_last_kick_tick = 0;
volatile uint8_t aoi_mode = 0;
volatile uint16_t aoi_strobe_period = 0;
// Non-blocking state machine for the AOI strobe, instead of a blocking
// HAL_Delay() that would freeze the whole main loop (PID, watchdogs,
// telemetry) for up to 500ms per pulse: aoi_strobe_active marks "LEDs are
// on, waiting to turn off", aoi_strobe_off_tick records when that should happen.
volatile uint8_t aoi_strobe_active = 0;
volatile uint32_t aoi_strobe_off_tick = 0;

volatile uint8_t oled_night_mode = 0; 
volatile uint8_t oled_mode_pending_flag = 0;
volatile uint8_t oled_night_mode_cmd = 0;

volatile uint8_t update_led_ring_flag = 0;
// Ring color, set via 0x100 and persisted - AOI's continuous/strobe modes
// (0x150) reuse whatever was last set here rather than carrying their own
// separate color.
volatile uint8_t ring_color_r = 0;
volatile uint8_t ring_color_g = 0;
volatile uint8_t ring_color_b = 0;
volatile uint8_t trigger_aoi_strobe_flag = 0;
volatile uint32_t probe_impact_counter = 0; // counts impacts detected in TOOL_SCAN_PROBE mode

// Generic endstop/limit switch on PB3 (Soldering iron, Drill, Laser, AOI modes -
// tools that don't already have a dedicated meaning for this pin). Active low.
volatile uint8_t endstop_triggered = 0;

// Top yellow strip state (blinking CAN icon + tool-icon animation)
volatile uint32_t can_led_tick = 0;        // timestamp of the last RX, to turn the icon off after ~200ms
// Automatic status LED color scheme (CONN_LED1) - see Update_StatusLED_AutoColor().
// A host-sent CAN 0x100 color holds for a while (long enough to actually be
// seen) before falling back to the automatic scheme, rather than being
// silently overwritten on the very next loop iteration.
volatile uint8_t led_host_override_active = 0;
volatile uint32_t led_host_override_expire_tick = 0;
#define LED_HOST_OVERRIDE_DURATION_MS 10000
#define LED_FUNCTIONING_WINDOW_MS     1500
uint8_t animation_frame = 0;               // 0/1 - toggles the tool icon's animation frame

// Global function prototypes
void SystemClock_Config(void);
void MX_GPIO_Init_Early(void);
void MX_GPIO_Post_Init(void);
void MX_CAN_Init(void);
void MX_TIM3_Full_Init(void); 
void MX_TIM1_DrillLaserFan_Init(uint32_t period); // Drill/laser/layer-fan PWM on TIM1_CH1 (PA8)
void MX_TIM2_HotendFan_Init(void); // Dedicated 25kHz PWM for the hotend fan (PA5/AF2)
void Telemetry_HotendFan(void);
void Watchdog_Safety_HotendFan(void);
void MX_ADC_Init(uint32_t channel);

// Hardware I2C2 (PA9/PA10, AF4) and OLED driver built on top of it
void MX_I2C2_Init(void);
void MX_ExpansionI2C_Init(void);
void MX_DMA_Init(void);
void MX_SPI1_Init(void);
void OLED_WriteCmd(uint8_t cmd);
void OLED_WriteData(uint8_t *buf, uint16_t len);
void MX_I2C2_Init_Early(void);
void OLED_Init(void);
void OLED_SetCursor(uint8_t page, uint8_t col);
void OLED_ClearPage(uint8_t page);
void OLED_PrintStr(uint8_t page, uint8_t col, const char* str);
uint8_t append_str(char *dest, const char *src);
uint8_t fmt_uint(char *dest, uint32_t value, uint8_t width);
void OLED_DrawHorizontalBar(uint8_t page, uint8_t col_start, uint8_t max_width, uint8_t percent);
void OLED_DrawToolIcon(uint8_t tool);
void OLED_Render_YellowStrip(void);
void Render_SplashScreen(uint8_t arm_frame);
void Render_ToolScreen(void);
void Process_OLED_NightModeChange(void);

void Update_StatusLED_AutoColor(void);
uint8_t Update_StatusLED_SPI_DMA(void);
void Update_RingLEDs_BitBang(void);
uint8_t ExpansionSPI_TransferByte(uint8_t tx_byte);
void ExpansionI2C_Start(void);
void ExpansionI2C_Stop(void);
uint8_t ExpansionI2C_WriteByte(uint8_t byte);
uint8_t ExpansionI2C_ReadByte(uint8_t send_ack);
uint8_t ExpansionI2C_HadTimeout(void);
void MX_ExpansionSPI_Init(void);
uint8_t FRAM_WriteBytes(uint16_t addr, const uint8_t *data, uint16_t len);
uint8_t FRAM_ReadBytes(uint16_t addr, uint8_t *data, uint16_t len);
static uint8_t SavedState_Checksum(const SavedState_t *s);
void SavedState_Load(void);
void SavedState_MaybeSave(void);
void Control_SolderingIron_PID(void);
void Control_3D_Hotend_PID(void);
void Control_SensorTelemetry(void);
void Control_EndstopTelemetry(void);
void Telemetry_Drill(void);
void Telemetry_LayerFan(void);
void Watchdog_Safety_Laser(void);
void Watchdog_Safety_Drill(void);
void Watchdog_Safety_LayerFan(void);
void Watchdog_Safety_SolderIron(void);
void Watchdog_Safety_HotendHeater(void);

// =============================================================================
// 4. LOW-LEVEL OLED GRAPHICS SUBSYSTEM (HARDWARE I2C2)
// =============================================================================
// Init for the OLED's hardware I2C2 bus (PA9/PA10, AF4). Timing register value
// is derived (not guessed) from ST's official AN4235 100kHz-Standard-Mode
// reference figures, scaled from 48MHz down to this chip's actual 8MHz I2C
// kernel clock (both I2C1 and I2C2 are explicitly HSI-sourced - see
// PeriphClkInit.I2c1ClockSelection/I2c2ClockSelection in SystemClock_Config
// - so this same timing value is valid for either peripheral) - getting
// this wrong means the OLED silently doesn't respond with no obvious
// error to debug.
void MX_I2C2_Init(void) {
    __HAL_RCC_I2C2_CLK_ENABLE();

    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = 0x0010232A; // ~100kHz @ 8MHz I2CCLK (this chip's I2C2
    // kernel clock is HSI-sourced directly at 8MHz, independent of the
    // 64MHz system clock - see SystemClock_Config). Derived by scaling the
    // AN4235 100kHz@48MHz reference timing targets down to 8MHz; worth
    // confirming on a scope/logic analyzer before relying on it in the field.
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c2);
}

// Expansion connector's own I2C bus. Kept electrically separate from I2C2/the OLED for the
// reason noted on hi2c2's declaration above. Reuses I2C2's exact timing
// value rather than deriving a new one, since both are configured to the
// same HSI 8MHz kernel clock source here.
// Bit-banged I2C master for CONN_EXPANSION's own bus (PB10=SCL, PB11=SDA).
// Not a hardware I2C peripheral: confirmed against DS9118 Tables 14/15
// that neither pin actually offers an I2C alternate function on this
// chip (the only 3 real hardware-I2C1 pin-pairs - PA14/15, PB6/7, PB8/9 -
// are all already committed to other functions on this design, with no
// free pins to relocate anything to). Bit-banging sidesteps the
// alternate-function requirement entirely - these pins just need to be
// plain open-drain GPIO, which every pin supports regardless of AF
// capability. Same electrical requirements as any I2C bus either way:
// R44/R48 (4.7k pull-ups to +3.3V) are already populated on both lines.
//
// Standard Mode timing (~100kHz, matching this project's hardware I2C2
// bus for the OLED/F-RAM) - roughly 5us per half-bit-period. Not
// cycle-counted precisely (unlike the WS2812B driver, which is
// time-triggered against a protocol-defined bit period); I2C is
// edge-triggered from the slave's perspective, so a plain small delay
// between edges is sufficient and correct, same reasoning as the
// expansion SPI driver above. 320 iterations, not 40: confirmed
// (compiled to assembly and compared instruction-for-instruction, not
// just estimated) that this loop's own per-iteration cost is identical
// to the T12 thermal loop's 640-iteration/~10us delay elsewhere in this
// firmware - 40 iterations here was actually producing ~0.625us, not
// ~5us, pushing this bus toward ~800kHz instead of the intended
// 100kHz. 320 (640 * 5/10) targets the actual documented ~5us.
static void ExpansionI2C_Delay(void) {
    for (volatile uint32_t d = 0; d < 320; d++);
}

// Open-drain "write": SET releases the line (pulled high externally by
// R44/R48), RESET actively pulls it low. Never actually drives high -
// matches real I2C's wired-AND requirement, where two devices pulling
// opposite directions on the same line at once would short the bus if
// either one drove a real high/low push-pull output instead.
static void ExpansionI2C_SetSCL(uint8_t high) {
    HAL_GPIO_WritePin(EXP_I2C2_SCL_PORT, EXP_I2C2_SCL_PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static uint8_t ExpansionI2C_ReadSCL(void) {
    return (HAL_GPIO_ReadPin(EXP_I2C2_SCL_PORT, EXP_I2C2_SCL_PIN) == GPIO_PIN_SET) ? 1 : 0;
}
// Set by ExpansionI2C_SetSCL_WaitHigh() whenever it times out, cleared at
// the start of every transaction - not yet consumed by any caller (this
// whole API isn't wired to a CAN command yet, unlike the expansion SPI's
// own 0x180/0x181 passthrough), but every high-level function below was
// silently ignoring this function's own return value, meaning a stuck-low
// SCL (a shorted bus, or a slave stretching indefinitely) would have gone
// completely unnoticed - the master would sample/drive SDA as if the
// clock edge had genuinely happened, on a bus that was never actually
// ready. Whenever this API is wired to something, check this flag after
// the transaction completes rather than trusting the data unconditionally.
static uint8_t expansion_i2c_scl_timeout = 0;
uint8_t ExpansionI2C_HadTimeout(void) {
    return expansion_i2c_scl_timeout;
}

// Releases SCL high, then waits (bounded) for it to actually read high
// before returning - a real I2C slave is allowed to hold SCL low itself
// to pause the transaction (clock stretching, a standard part of the I2C
// spec, not an error condition). Skipping this wait and assuming SCL
// went high the instant it's released would let this master sample/
// change SDA while a stretching slave still has the clock held down,
// corrupting the transfer. ~2000 loop iterations at this delay's own
// per-iteration granularity is generously above any I2C device's realistic
// stretch duration without risking a true bus-stuck condition hanging
// forever.
static uint8_t ExpansionI2C_SetSCL_WaitHigh(void) {
    ExpansionI2C_SetSCL(1);
    for (uint16_t timeout = 0; timeout < 2000; timeout++) {
        if (ExpansionI2C_ReadSCL()) return 1;
    }
    expansion_i2c_scl_timeout = 1; // SCL never actually went high - a stuck slave or a wiring fault
    return 0;
}
static void ExpansionI2C_SetSDA(uint8_t high) {
    HAL_GPIO_WritePin(EXP_I2C2_SDA_PORT, EXP_I2C2_SDA_PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static uint8_t ExpansionI2C_ReadSDA(void) {
    return (HAL_GPIO_ReadPin(EXP_I2C2_SDA_PORT, EXP_I2C2_SDA_PIN) == GPIO_PIN_SET) ? 1 : 0;
}

// START condition: SDA falls while SCL is high. Caller's responsibility
// to have both lines idling high beforehand (the reset/idle state).
void ExpansionI2C_Start(void) {
    expansion_i2c_scl_timeout = 0; // fresh transaction, fresh timeout state
    ExpansionI2C_SetSDA(1);
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_Delay();
    ExpansionI2C_SetSDA(0);
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL(0);
    ExpansionI2C_Delay();
}

// STOP condition: SDA rises while SCL is high.
void ExpansionI2C_Stop(void) {
    ExpansionI2C_SetSDA(0);
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_Delay();
    ExpansionI2C_SetSDA(1);
    ExpansionI2C_Delay();
}

// Clocks out 8 bits (MSB first) then reads the 9th (ACK/NACK) bit.
// Returns 1 if the slave ACKed (pulled SDA low), 0 for NACK/no response -
// matching how a real hardware I2C peripheral's status flag would read.
uint8_t ExpansionI2C_WriteByte(uint8_t byte) {
    for (int8_t i = 7; i >= 0; i--) {
        ExpansionI2C_SetSDA((byte >> i) & 0x01);
        ExpansionI2C_Delay();
        ExpansionI2C_SetSCL_WaitHigh();
        ExpansionI2C_Delay();
        ExpansionI2C_SetSCL(0);
        ExpansionI2C_Delay();
    }
    // Release SDA so the slave can drive it for the ACK bit.
    ExpansionI2C_SetSDA(1);
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_Delay();
    uint8_t ack = (ExpansionI2C_ReadSDA() == 0) ? 1 : 0;
    ExpansionI2C_SetSCL(0);
    ExpansionI2C_Delay();
    return ack;
}

// Clocks in 8 bits (MSB first), then sends ACK (send_ack=1, more bytes
// to follow) or NACK (send_ack=0, this is the last byte being read).
uint8_t ExpansionI2C_ReadByte(uint8_t send_ack) {
    uint8_t byte = 0;
    ExpansionI2C_SetSDA(1); // release - let the slave drive each data bit
    for (uint8_t i = 0; i < 8; i++) {
        ExpansionI2C_Delay();
        ExpansionI2C_SetSCL_WaitHigh();
        ExpansionI2C_Delay();
        byte = (byte << 1) | ExpansionI2C_ReadSDA();
        ExpansionI2C_SetSCL(0);
    }
    ExpansionI2C_SetSDA(send_ack ? 0 : 1);
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL(0);
    ExpansionI2C_SetSDA(1); // release again for whatever comes next
    ExpansionI2C_Delay();
    return byte;
}

void MX_ExpansionI2C_Init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_OD; // open-drain, matching I2C's wired-AND bus
    gpio.Pull = GPIO_NOPULL; // R44/R48 already provide the real pull-ups externally
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    // Idle state: both lines released high.
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_SetSDA(1);
}

// DMA1_Channel3 is this chip's default (unremapped) SPI1_TX request line -
// confirmed via SYSCFG_CFGR3's SPI1_TX_DMA_RMP field, which defaults to 00
// (no remap) on reset; this firmware never touches that register, so it
// stays at the default mapping. Must be initialized before MX_SPI1_Init
// links it to hspi1.hdmatx.
void MX_DMA_Init(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_spi1_tx.Instance = DMA1_Channel3;
    hdma_spi1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_spi1_tx.Init.Mode = DMA_NORMAL;
    hdma_spi1_tx.Init.Priority = DMA_PRIORITY_LOW;
    HAL_DMA_Init(&hdma_spi1_tx);
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);

    __HAL_LINKDMA(&hspi1, hdmatx, hdma_spi1_tx);
}

// SPI1 configured purely as a WS2812B-via-SPI bit-pattern generator: only
// MOSI (PA7) carries a meaningful signal, so SCK/MISO/NSS are left
// unconfigured (still their GPIO reset defaults) and software NSS management
// is used so the peripheral doesn't wait on a hardware NSS pin that isn't
// wired to anything. 2MHz clock (64MHz APB2 / 32) - see the ws2812_lut
// comment for why this specific rate.
void MX_SPI1_Init(void) {
    __HAL_RCC_SPI1_CLK_ENABLE();
    // PA7's SPI1_MOSI/AF5 config lives in MX_GPIO_Post_Init (unconditional,
    // alongside the rest of this board's universal pin setup) - not repeated here.

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES; // MISO unused but this is the standard/simplest mode
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT; // no physical NSS pin wired - avoid waiting on one
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32; // 64MHz/32 = 2MHz
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB; // matches the ws2812_lut generation
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    HAL_SPI_Init(&hspi1);
}

// Set once by OLED_Init(); every OLED write function checks this first
// and returns immediately if the display never responded, rather than
// paying a real I2C timeout on every single command for the rest of the
// session. A missing/disconnected display should never be able to stack
// repeated 50ms timeouts against the 20ms PID loop and the other
// real-time work sharing this same main loop. The 50ms timeouts below
// stay as a second, independent layer for the case this flag can't catch:
// a display that responded fine at init but fails mid-session (a
// damaged cable, a connector working loose) - the reasoning in each
// comment for why 50ms specifically was chosen is unchanged.
uint8_t oled_present = 1;

// Wraps every I2C2 transmit on this bus (OLED + F-RAM both use it) with
// error detection: if a transmission times out or errors, hi2c2 can be
// left in HAL_I2C_STATE_BUSY/ERROR with nothing else in this firmware to
// detect or recover it, silently breaking every subsequent transaction
// on this bus (both the display AND the F-RAM, since they share it) for
// the rest of runtime. One HAL_I2C_DeInit()+MX_I2C2_Init() re-init
// attempt on failure is cheap insurance against a transient glitch
// (electrical noise, a momentary bus contention) leaving the peripheral
// stuck rather than genuinely disconnected hardware, which would just
// fail again next time and get caught by oled_present's own probe.
static uint8_t I2C2_TransmitWithRecovery(uint16_t addr, uint8_t *data, uint16_t len) {
    if (HAL_I2C_Master_Transmit(&hi2c2, addr, data, len, 50) == HAL_OK) return 1;
    HAL_I2C_DeInit(&hi2c2);
    MX_I2C2_Init();
    return (HAL_I2C_Master_Transmit(&hi2c2, addr, data, len, 50) == HAL_OK) ? 1 : 0;
}

void OLED_WriteCmd(uint8_t cmd) {
    if (!oled_present) return;
    uint8_t frame[2] = {0x00, cmd}; // control byte 0x00 = command stream
    I2C2_TransmitWithRecovery(OLED_I2C_ADDR, frame, 2);
}

void OLED_WriteData(uint8_t *buf, uint16_t len) {
    if (!oled_present) return;
    if (len == 0) return; // a control-byte-only transaction with no data
                           // bytes following desyncs some SSD1306 clone
                           // controllers - nothing to actually write anyway.
    // Control byte and pixel data must go out in a single I2C transaction -
    // per the SSD1306 protocol, every transaction needs its own control
    // byte, so sending them as two separate transactions would make the
    // display read the data's first byte as a malformed control byte.
    if (len > 128) len = 128; // defensive clamp - every real call site today
                              // is verified within this bound, but this
                              // costs nothing and protects against a future
                              // caller passing an oversized len.
    uint8_t frame[129];
    frame[0] = 0x40;
    memcpy(&frame[1], buf, len);
    I2C2_TransmitWithRecovery(OLED_I2C_ADDR, frame, len + 1);
}

void OLED_SetCursor(uint8_t page, uint8_t col) {
    if (!oled_present) return;
    // Combined into one transaction rather than 3 separate OLED_WriteCmd
    // calls - the SSD1306 protocol allows any number of command bytes to
    // follow a single 0x00 control byte, so there's no need to repeat the
    // START/address/control/STOP overhead for each of the 3 commands here.
    uint8_t frame[4] = {0x00, (uint8_t)(0xB0 + page), (uint8_t)(0x00 + (col & 0x0F)), (uint8_t)(0x10 + ((col >> 4) & 0x0F))};
    I2C2_TransmitWithRecovery(OLED_I2C_ADDR, frame, 4);
}

// Split out from OLED_Init() below - just the I2C2 peripheral+pin setup,
// with none of the OLED-specific probing/commands that follow it there.
// Called early in main(), before Identify_PhysicalTool(), specifically so
// that function can read the F-RAM directly when the ID-jumper reading is
// 0x1F (11111b, "free configuration" - see EEPROM.TXT) - the F-RAM shares
// this same I2C2 bus with the OLED (see FRAM_I2C_ADDR's own comment), and
// without this split, I2C2 wouldn't be usable until OLED_Init() runs,
// which happens much later in boot (after MX_GPIO_Post_Init() has already
// configured tool-dependent pins/timers/ADC based on whatever
// active_tool was at that point). OLED_Init() below also calls this
// function rather than keeping its own copy of this setup, same as
// everything else that needs I2C2 up before using it.
void MX_I2C2_Init_Early(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD; // Open-drain: I2C is wired-AND, matches R33/R34's external pull-ups
    GPIO_InitStruct.Pull = GPIO_PULLUP;     // Belt-and-suspenders alongside R33/R34
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(OLED_PORT, &GPIO_InitStruct);

    MX_I2C2_Init();
}

void OLED_Init(void) {
    MX_I2C2_Init_Early();
    
    HAL_Delay(50);
    // A single, cheap probe decides whether this session touches the
    // display at all - one bounded transaction instead of every
    // subsequent command individually risking a timeout against a
    // display that was never going to answer (never populated, a
    // damaged cable, etc. - all indistinguishable from here, and all
    // handled the same way: skip the rest of this function, and every
    // OLED_WriteCmd/WriteData call for the rest of the session).
    oled_present = (HAL_I2C_IsDeviceReady(&hi2c2, OLED_I2C_ADDR, 2, 5) == HAL_OK) ? 1 : 0;
    if (!oled_present) return;
    // Verified compatible with both SSD1306 and SSD1315 (a newer, drop-in
    // replacement controller that many "SSD1306" modules actually ship
    // with today without the silkscreen/listing changing) - Solomon
    // Systech's own SSD1315 datasheet documents an identical command
    // register set to the SSD1306, and this full sequence (as opposed to a
    // truncated one) is what community reports confirm as most reliable
    // across both. One SSD1315-specific quirk exists in the wild: some
    // units emit an audible high-frequency squeak with the standard 0xD5
    // 0x80 oscillator setting, fixed by using 0xD5 0xF0 instead - cosmetic
    // only (doesn't affect what's shown), so left at the SSD1306-standard
    // 0x80 here rather than changing a working parameter on unverified
    // hardware; if a squeak shows up on the actual board, that's the
    // specific value to try.
    OLED_WriteCmd(0xAE); 
    OLED_WriteCmd(0xD5); OLED_WriteCmd(0x80);
    OLED_WriteCmd(0xA8); OLED_WriteCmd(0x3F); 
    OLED_WriteCmd(0xD3); OLED_WriteCmd(0x00);
    OLED_WriteCmd(0x40); 
    OLED_WriteCmd(0x8D); OLED_WriteCmd(0x14); 
    OLED_WriteCmd(0x20); OLED_WriteCmd(0x02); 
    OLED_WriteCmd(0xA1); 
    OLED_WriteCmd(0xC8); 
    OLED_WriteCmd(0xDA); OLED_WriteCmd(0x12);
    OLED_WriteCmd(0x81); OLED_WriteCmd(0xCF); 
    OLED_WriteCmd(0xD9); OLED_WriteCmd(0xF1);
    OLED_WriteCmd(0xDB); OLED_WriteCmd(0x40);
    OLED_WriteCmd(0xA4); OLED_WriteCmd(0xA6); 
    OLED_WriteCmd(0xAF); 
}

// FLASH OPTIMIZATION: replaces sprintf() for all OLED text. sprintf/printf's
// format-string parser plus its float-formatting support pulled in roughly
// 2KB of C library code that this firmware never needed the full generality
// of - every call here only ever builds simple "label + padded number" text.
// These two tiny helpers do the same job at a fraction of the size.

// Copies a null-terminated string, returns how many characters were written
// (not counting the terminator) so the caller can chain calls at the right
// offset without needing strlen().
uint8_t append_str(char *dest, const char *src) {
    uint8_t i = 0;
    while (src[i]) { dest[i] = src[i]; i++; }
    return i;
}

// Writes an unsigned integer in decimal, right-aligned and space-padded to
// at least 'width' characters (writes more if the value needs it - never
// truncates). Does NOT null-terminate; the caller terminates once after the
// last piece, same pattern as append_str.
uint8_t fmt_uint(char *dest, uint32_t value, uint8_t width) {
    char tmp[10]; // max uint32_t is 10 digits
    // Every current call site passes a fixed literal (3/4/5), so this
    // isn't reachable today - but width has no relationship to dest's
    // actual capacity otherwise, and the caller's buffer is sized for
    // one OLED line (see OLED_PrintStr's own 21-character note), so
    // capping here protects any future caller that computes width
    // dynamically instead of using a literal.
    if (width > 21) width = 21;
    uint8_t n = 0;
    do {
        tmp[n++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    uint8_t pad = (n < width) ? (width - n) : 0;
    uint8_t pos = 0;
    while (pad-- > 0) dest[pos++] = ' ';
    while (n > 0) dest[pos++] = tmp[--n];
    return pos;
}

void OLED_PrintStr(uint8_t page, uint8_t col, const char* str) {
    OLED_SetCursor(page, col);
    // Builds the whole string's pixel data locally, then sends it in one
    // I2C transaction rather than one transaction per character - each
    // transaction carries full protocol overhead (START, address, control
    // byte, data, STOP), so batching matters for a 15-20 character line.
    // Capped at 21 characters (126 columns): as many as fit in the
    // 128-column frame OLED_WriteData already caps itself at.
    uint8_t line_buf[126];
    uint8_t len = 0;
    while (*str && len < 21) {
        char c = *str++;
        if (c >= 'a' && c <= 'z') c -= 32; // Font5x7 only has glyphs 32-91 (no lowercase):
                                            // without this, "v1.0" or "__DATE__" (e.g. "Jul") would
                                            // come out with blank gaps where each lowercase letter was.
        if (c >= 32 && c <= 95) {
            uint16_t idx = (c - 32) * 5;
            for (uint8_t i = 0; i < 5; i++) line_buf[len*6 + i] = Font5x7[idx+i];
        } else {
            for (uint8_t i = 0; i < 5; i++) line_buf[len*6 + i] = 0x00;
        }
        line_buf[len*6 + 5] = 0x00; // 1px inter-character spacing
        len++;
    }
    OLED_WriteData(line_buf, len * 6);
}

void OLED_ClearPage(uint8_t page) {
    OLED_SetCursor(page, 0);
    // One 128-byte transaction rather than 8 separate 16-byte ones - same
    // reasoning as OLED_SetCursor's own fix above: each transaction
    // carries its own START/address/control/STOP overhead, and
    // OLED_WriteData's own frame buffer already supports the full 128
    // bytes a page needs in a single call.
    static const uint8_t vacio[128] = {0};
    OLED_WriteData((uint8_t *)vacio, 128);
}

void OLED_DrawHorizontalBar(uint8_t page, uint8_t col_start, uint8_t max_width, uint8_t percent) {
    if (percent > 100) percent = 100;
    if (max_width > 128) max_width = 128; // matches OLED_WriteData's own frame buffer cap
    // Every current call site uses col_start=5, max_width=85 (well within
    // the 128-column display), so this isn't reachable today - but
    // nothing else here checks the combination, and a future caller with
    // a different col_start could push filled pixels past column 127,
    // wrapping onto the start of the same page instead of simply running
    // off the edge.
    if (col_start < 128 && (uint16_t)col_start + max_width > 128) {
        max_width = 128 - col_start;
    }
    uint8_t filled_pixels = (max_width * percent) / 100;

    // Builds the whole bar locally and sends it in one I2C transaction,
    // rather than one transaction per column - each carries full protocol
    // overhead, and this loop runs on the same thread as the 20ms PID loop.
    uint8_t bar_buffer[128];
    for(uint8_t i = 0; i < max_width; i++) {
        if(i == 0 || i == (max_width - 1)) {
            bar_buffer[i] = 0x7E;
        } else if(i <= filled_pixels) {
            bar_buffer[i] = 0x5E;
        } else {
            bar_buffer[i] = 0x42;
        }
    }

    OLED_SetCursor(page, col_start);
    OLED_WriteData(bar_buffer, max_width);
}

// 16x16 monochrome icon table, one per each of the 12 tools.
// SSD1306 format: 2 pages (top/bottom) x 16 columns, bit0 = the page's
// top row (same convention as Font5x7). Generated from simple silhouettes
// recognizable at this very small size.
static const uint8_t ToolIcons[12][4][2][16] = {
    // 0 TOOL_SOLDERING_IRON
    {
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0xF0,0xF8,0x7C,0x3A,0x14,0x08}, {0x00,0x80,0x60,0x18,0x04,0x0E,0x1F,0x0F,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0xF4,0xFA,0x7D,0x38,0x14,0x08}, {0x00,0x80,0x60,0x18,0x04,0x0E,0x1F,0x0F,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0xF4,0xFA,0x7D,0x38,0x11,0x06}, {0x00,0x80,0x60,0x18,0x04,0x0E,0x1F,0x0F,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0xF0,0xF8,0x7C,0x3A,0x11,0x06}, {0x00,0x80,0x60,0x18,0x04,0x0E,0x1F,0x0F,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00}}
    },
    // 1 TOOL_PASTE_DISPENSER
    {
       {{0x00,0x00,0x00,0x00,0xF8,0x09,0x09,0x0F,0x0F,0x09,0x09,0xF8,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x03,0x02,0xCE,0xFE,0xFE,0xCE,0x02,0x03,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0xF8,0x0A,0x0A,0x0E,0x0F,0x09,0x09,0xF8,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x03,0xE2,0xEE,0xFE,0xFE,0xCE,0x02,0x03,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0xF8,0x0A,0x0A,0x0E,0x0E,0x0A,0x0A,0xF8,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x03,0xE2,0xEE,0xFE,0xFE,0xEE,0xE2,0x03,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0xF8,0x09,0x09,0x0F,0x0E,0x0A,0x0A,0xF8,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x03,0x02,0xCE,0xFE,0xFE,0xEE,0xE2,0x03,0x00,0x00,0x00,0x00}}
    },
    // 2 TOOL_LIQUID_DISPENSER
    {
       {{0x00,0x00,0x00,0x00,0xF8,0x09,0x09,0x0F,0x0F,0x09,0x09,0xF8,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x03,0x02,0x4E,0xFE,0xFE,0x4E,0x02,0x03,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0xF8,0x0A,0x0A,0x0E,0x0F,0x09,0x09,0xF8,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x03,0x02,0x8E,0xDE,0xFE,0x4E,0x02,0x03,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0xF8,0x0A,0x0A,0x0E,0x0E,0x0A,0x0A,0xF8,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x03,0x02,0x8E,0xDE,0x9E,0x0E,0x02,0x03,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0xF8,0x09,0x09,0x0F,0x0E,0x0A,0x0A,0xF8,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x03,0x02,0x4E,0xFE,0x9E,0x0E,0x02,0x03,0x00,0x00,0x00,0x00}}
    },
    // 3 TOOL_SCREWDRIVER
    {
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0x7C,0x22,0x41,0x41,0x41,0x22,0x1C}, {0x40,0xA0,0x58,0x3C,0x0E,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0x7C,0x22,0x41,0x41,0x41,0x22,0x1C}, {0xA0,0x60,0x58,0x3C,0x0E,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0x7C,0x22,0x41,0xC1,0x41,0x22,0x1C}, {0xA0,0x60,0x68,0x5C,0x0E,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0x7C,0x22,0x41,0xC1,0x41,0x22,0x1C}, {0x40,0xA0,0x68,0x5C,0x0E,0x07,0x03,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}
    },
    // 4 TOOL_VACUUM_PICKUP
    {
       {{0x00,0x00,0x00,0xC0,0xC0,0xC0,0xC0,0xFF,0xFF,0xC0,0xC0,0xC0,0xC0,0x00,0x00,0x00}, {0x00,0x00,0x00,0x01,0x0F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x0F,0x01,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x80,0x80,0x80,0xFF,0xFF,0x80,0x80,0x80,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x03,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x03,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x07,0x1F,0x1F,0x1F,0x1F,0x07,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00}}
    },
    // 5 TOOL_DRILL
    {
       {{0x00,0x00,0xFE,0xC2,0xC2,0xC2,0xC2,0xC2,0xC2,0x42,0x42,0x7E,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x04,0xC8,0x10,0x20,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0xFE,0xC2,0xC2,0xC2,0xC2,0xC2,0xC2,0x42,0x42,0x7E,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x10,0xC8,0x04,0x02,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0xFE,0xC2,0xC2,0xC2,0xC2,0xC2,0xC2,0x42,0x42,0x7E,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x04,0xC8,0x10,0x20,0x00,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0xFE,0xC2,0xC2,0xC2,0xC2,0xC2,0xC2,0x42,0x42,0x7E,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x10,0xC8,0x04,0x02,0x00,0x00,0x00,0x00,0x00}}
    },
    // 6 TOOL_GRIPPER_GIMBAL - a solid ring with a fixed center
    // pivot and a marker rotating around the OUTSIDE edge of the ring -
    // placed there rather than on the ring itself, where it would be
    // redundant with pixels the ring already sets and wouldn't actually
    // change anything between angles (verified by rendering all 4 frames
    // before committing).
    {
       {{0x00,0xC0,0xF0,0x18,0x0C,0x04,0xC6,0xC6,0xC6,0xC6,0x04,0x0C,0x18,0xF0,0xC0,0x00}, {0x00,0x03,0x0F,0x18,0x30,0x20,0x63,0x63,0x63,0x63,0x20,0x30,0x18,0x0F,0x03,0x01}},
       {{0x00,0xC0,0xF0,0x18,0x0C,0x04,0xC6,0xC6,0xC6,0xC6,0x04,0x0C,0x18,0xF0,0xC0,0x00}, {0x00,0x03,0x0F,0x18,0x30,0x20,0x63,0x63,0xE3,0x63,0x20,0x30,0x18,0x0F,0x03,0x00}},
       {{0x00,0xC0,0xF0,0x18,0x0C,0x04,0xC6,0xC6,0xC6,0xC6,0x04,0x0C,0x18,0xF0,0xC0,0x00}, {0x01,0x03,0x0F,0x18,0x30,0x20,0x63,0x63,0x63,0x63,0x20,0x30,0x18,0x0F,0x03,0x00}},
       {{0x00,0xC0,0xF0,0x18,0x0C,0x04,0xC6,0xC7,0xC6,0xC6,0x04,0x0C,0x18,0xF0,0xC0,0x00}, {0x00,0x03,0x0F,0x18,0x30,0x20,0x63,0x63,0x63,0x63,0x20,0x30,0x18,0x0F,0x03,0x00}}
    },
    // 7 TOOL_GRIPPER_NEMA
    {
       {{0x00,0xC0,0xC0,0xC0,0xC0,0x80,0xBF,0xA1,0xA1,0xBF,0x80,0xC0,0xC0,0xC0,0xC0,0x00}, {0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00}},
       {{0x00,0x00,0x00,0xC0,0xC0,0xC0,0xFF,0xA1,0xA1,0xBF,0x80,0xC0,0xC0,0xC0,0xC0,0x00}, {0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x78,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00}},
       {{0x00,0x00,0x00,0xC0,0xC0,0xC0,0xFF,0xA1,0xA1,0xFF,0xC0,0xC0,0xC0,0x00,0x00,0x00}, {0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x78,0x78,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00}},
       {{0x00,0xC0,0xC0,0xC0,0xC0,0x80,0xBF,0xA1,0xA1,0xFF,0xC0,0xC0,0xC0,0x00,0x00,0x00}, {0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x78,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00}}
    },
    // 8 TOOL_AOI_INSPECTION
    {
       {{0x00,0x80,0x40,0x24,0x28,0x90,0x50,0xB0,0xB0,0x50,0x90,0x28,0x24,0x40,0x80,0x00}, {0x00,0x01,0x02,0x04,0x04,0x09,0x0A,0x0D,0x0D,0x0A,0x09,0x04,0x04,0x02,0x01,0x00}},
       {{0x80,0x80,0xC2,0x24,0x28,0x90,0xD0,0xF0,0xB0,0x50,0x90,0x28,0x24,0x40,0x80,0x00}, {0x00,0x01,0x02,0x04,0x04,0x09,0x0B,0x0F,0x0D,0x0A,0x09,0x04,0x04,0x02,0x01,0x00}},
       {{0x80,0x80,0xC2,0x24,0x28,0x90,0xD0,0xF0,0xF0,0xD0,0x90,0x28,0x24,0xC2,0x80,0x80}, {0x00,0x01,0x02,0x04,0x04,0x09,0x0B,0x0F,0x0F,0x0B,0x09,0x04,0x04,0x02,0x01,0x00}},
       {{0x00,0x80,0x40,0x24,0x28,0x90,0x50,0xB0,0xF0,0xD0,0x90,0x28,0x24,0xC2,0x80,0x80}, {0x00,0x01,0x02,0x04,0x04,0x09,0x0A,0x0D,0x0F,0x0B,0x09,0x04,0x04,0x02,0x01,0x00}}
    },
    // 9 TOOL_LASER_ENGRAVER
    {
       {{0x00,0x00,0x00,0x00,0x80,0x60,0x10,0x0C,0x82,0x0C,0x10,0x60,0x80,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x03,0x84,0x58,0xE0,0x58,0x84,0x03,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x80,0x60,0xD0,0xEC,0xE2,0x0C,0x10,0x60,0x80,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x80,0x83,0x45,0x5B,0xE0,0x58,0x84,0x03,0x00,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x80,0x60,0xD0,0xEC,0xE2,0xEC,0xD0,0x60,0x80,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x80,0x83,0x45,0x5B,0xE3,0x5B,0x45,0x83,0x80,0x00,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x80,0x60,0x10,0x0C,0x82,0xEC,0xD0,0x60,0x80,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x03,0x84,0x58,0xE3,0x5B,0x45,0x83,0x80,0x00,0x00,0x00}}
    },
    // 10 TOOL_3D_PRINTER
    {
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x7F,0x7F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x02,0x8A,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x8A,0x02,0x00,0x00}},
       {{0x00,0x00,0x80,0x80,0x80,0x80,0x80,0x80,0x00,0x1F,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x02,0x8A,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x8A,0x02,0x00,0x00}},
       {{0x00,0x00,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x9F,0xFF,0xFF,0x9F,0x80,0x00,0x00}, {0x00,0x00,0x02,0x8A,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x8A,0x02,0x00,0x00}},
       {{0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x7F,0xFF,0x9F,0xFF,0xFF,0x9F,0x80,0x00,0x00}, {0x00,0x00,0x02,0x8A,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0x8A,0x02,0x00,0x00}}
    },
    // 11 TOOL_SCAN_PROBE
    {
       {{0x80,0x80,0x80,0xC0,0x20,0x10,0x88,0xCF,0xCF,0x88,0x10,0x20,0xC0,0x80,0x80,0x80}, {0x01,0x01,0x01,0x03,0x04,0x08,0x11,0xF3,0xF3,0x11,0x08,0x04,0x03,0x01,0x01,0x01}},
       {{0x80,0x80,0xC0,0xB0,0x08,0x08,0x84,0xCF,0xCF,0x88,0x10,0x20,0xC0,0x80,0x80,0x80}, {0x01,0x01,0x03,0x0D,0x10,0x10,0x21,0xF3,0xF3,0x11,0x08,0x04,0x03,0x01,0x01,0x01}},
       {{0x80,0x80,0xC0,0xB0,0x08,0x08,0x84,0xCF,0xCF,0x84,0x08,0x08,0xB0,0xC0,0x80,0x80}, {0x01,0x01,0x03,0x0D,0x10,0x10,0x21,0xF3,0xF3,0x21,0x10,0x10,0x0D,0x03,0x01,0x01}},
       {{0x80,0x80,0x80,0xC0,0x20,0x10,0x88,0xCF,0xCF,0x84,0x08,0x08,0xB0,0xC0,0x80,0x80}, {0x01,0x01,0x01,0x03,0x04,0x08,0x11,0xF3,0xF3,0x21,0x10,0x10,0x0D,0x03,0x01,0x01}}
    },
};

// 8x8 CAN activity icon (a single page), blinks in the yellow strip
// every time a new frame arrives on the bus.
static const uint8_t CANIcon[8] = {0x00,0x04,0x14,0xD5,0xD5,0x14,0x04,0x00};

// Large "ERROR" wordmark for the invalid-tool-ID state (TOOL_INVALID),
// built from simple rectangular block letters (E, R, O), 128 columns wide
// x 5 pages (40px) tall, generated programmatically and verified by
// rendering before being embedded here.
static const uint8_t CheckIdJumpersText[2][128] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF8,0x06,0x06,0x06,0x06,0x00,0xFE,0x80,0x80,0x80,0xFE,0x00,0xFE,0x86,0x86,0x86,0x06,0x00,0xF8,0x06,0x06,0x06,0x06,0x00,0xFE,0x80,0x60,0x18,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x06,0xFE,0x06,0x06,0x00,0xFE,0x06,0x06,0x06,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0xFE,0x06,0x00,0xFE,0x00,0x00,0x00,0xFE,0x00,0xFE,0x18,0xE0,0x18,0xFE,0x00,0xFE,0x86,0x86,0x86,0x78,0x00,0xFE,0x86,0x86,0x86,0x06,0x00,0xFE,0x86,0x86,0x86,0x78,0x00,0x78,0x86,0x86,0x86,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x60,0x60,0x60,0x60,0x00,0x7F,0x01,0x01,0x01,0x7F,0x00,0x7F,0x61,0x61,0x61,0x60,0x00,0x1F,0x60,0x60,0x60,0x60,0x00,0x7F,0x01,0x06,0x18,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x60,0x60,0x7F,0x60,0x60,0x00,0x7F,0x60,0x60,0x60,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x60,0x60,0x1F,0x00,0x00,0x1F,0x60,0x60,0x60,0x1F,0x00,0x7F,0x00,0x01,0x00,0x7F,0x00,0x7F,0x01,0x01,0x01,0x00,0x00,0x7F,0x61,0x61,0x61,0x60,0x00,0x7F,0x01,0x07,0x19,0x60,0x00,0x60,0x61,0x61,0x61,0x1E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static const uint8_t SplashYellowText[2][128] = {
    {0x00,0x00,0xFE,0xFE,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0xFE,0xFE,0x00,0x00,0x00,0xFE,0xFE,0xFE,0x86,0x86,0x86,0x86,0x86,0x86,0x86,0x86,0x86,0x78,0x78,0x78,0x00,0x00,0x00,0x06,0x06,0x06,0x06,0x06,0x06,0xFE,0xFE,0xFE,0x06,0x06,0x06,0x06,0x06,0x06,0x00,0x00,0x00,0xF8,0xF8,0xF8,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x00,0x00,0x00,0xE0,0x00,0x00,0x00,0x00,0xE0,0xE0,0x00,0x00,0x10,0xF8,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0xF8,0xF8,0x00,0x00,0x00,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x00,0xF0,0x08,0x08,0x08,0x08,0x08,0x10,0x00,0xF8,0x08,0x08,0x08,0x08,0x00},
    {0x00,0x00,0x1F,0x1F,0x1F,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x7F,0x7F,0x7F,0x01,0x01,0x01,0x07,0x07,0x07,0x19,0x19,0x19,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0x7F,0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x03,0x04,0x08,0x08,0x04,0x03,0x03,0x00,0x00,0x08,0x0F,0x0F,0x08,0x00,0x00,0x00,0x00,0x0C,0x0C,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x0F,0x0F,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x08,0x08,0x08,0x08,0x08,0x04,0x00,0x0F,0x08,0x08,0x08,0x0F,0x00},
};

static const uint8_t ErrorText[5][128] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0xFC,0xFC,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x00,0x00,0x00,0xFC,0xFC,0xFC,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x80,0x80,0x80,0x00,0x00,0x00,0xFC,0xFC,0xFC,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x80,0x80,0x80,0x00,0x00,0x00,0x80,0x80,0x80,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x80,0x80,0x80,0x00,0x00,0x00,0xFC,0xFC,0xFC,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x7C,0x80,0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x3E,0x3E,0x3E,0xFE,0xFE,0xFE,0x3E,0x3E,0x3E,0x01,0x01,0x01,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x3E,0x3E,0x3E,0xFE,0xFE,0xFE,0x3E,0x3E,0x3E,0x01,0x01,0x01,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x3E,0x3E,0x3E,0xFE,0xFE,0xFE,0x3E,0x3E,0x3E,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x07,0x07,0x07,0xF8,0xF8,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x07,0x07,0x07,0xF8,0xF8,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x07,0x07,0x07,0xF8,0xF8,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

// WS2812B-via-SPI byte expansion table. Each WS2812B color byte becomes 3
// SPI-transmitted bytes (24 SPI bits): a WS '1' bit -> SPI pattern 110
// (~66% high), a WS '0' bit -> SPI pattern 100 (~33% high), MSB-first,
// matching the standard WS2812B-over-SPI technique. At 2MHz SPI (64MHz/32),
// each SPI bit is 500ns, so each WS bit is 1.5us total - comfortably within
// the WS2812B's tolerance. Generated and verified programmatically (0x00
// and 0xFF spot-checked bit-by-bit) rather than hand-computed, since a single
// wrong bit here would show up as literally every LED color being slightly
// wrong in a way that's hard to trace back to this table.
static const uint8_t ws2812_lut[256][3] = {
    {0x92,0x49,0x24}, {0x92,0x49,0x26}, {0x92,0x49,0x34}, {0x92,0x49,0x36}, {0x92,0x49,0xA4}, {0x92,0x49,0xA6}, {0x92,0x49,0xB4}, {0x92,0x49,0xB6},
    {0x92,0x4D,0x24}, {0x92,0x4D,0x26}, {0x92,0x4D,0x34}, {0x92,0x4D,0x36}, {0x92,0x4D,0xA4}, {0x92,0x4D,0xA6}, {0x92,0x4D,0xB4}, {0x92,0x4D,0xB6},
    {0x92,0x69,0x24}, {0x92,0x69,0x26}, {0x92,0x69,0x34}, {0x92,0x69,0x36}, {0x92,0x69,0xA4}, {0x92,0x69,0xA6}, {0x92,0x69,0xB4}, {0x92,0x69,0xB6},
    {0x92,0x6D,0x24}, {0x92,0x6D,0x26}, {0x92,0x6D,0x34}, {0x92,0x6D,0x36}, {0x92,0x6D,0xA4}, {0x92,0x6D,0xA6}, {0x92,0x6D,0xB4}, {0x92,0x6D,0xB6},
    {0x93,0x49,0x24}, {0x93,0x49,0x26}, {0x93,0x49,0x34}, {0x93,0x49,0x36}, {0x93,0x49,0xA4}, {0x93,0x49,0xA6}, {0x93,0x49,0xB4}, {0x93,0x49,0xB6},
    {0x93,0x4D,0x24}, {0x93,0x4D,0x26}, {0x93,0x4D,0x34}, {0x93,0x4D,0x36}, {0x93,0x4D,0xA4}, {0x93,0x4D,0xA6}, {0x93,0x4D,0xB4}, {0x93,0x4D,0xB6},
    {0x93,0x69,0x24}, {0x93,0x69,0x26}, {0x93,0x69,0x34}, {0x93,0x69,0x36}, {0x93,0x69,0xA4}, {0x93,0x69,0xA6}, {0x93,0x69,0xB4}, {0x93,0x69,0xB6},
    {0x93,0x6D,0x24}, {0x93,0x6D,0x26}, {0x93,0x6D,0x34}, {0x93,0x6D,0x36}, {0x93,0x6D,0xA4}, {0x93,0x6D,0xA6}, {0x93,0x6D,0xB4}, {0x93,0x6D,0xB6},
    {0x9A,0x49,0x24}, {0x9A,0x49,0x26}, {0x9A,0x49,0x34}, {0x9A,0x49,0x36}, {0x9A,0x49,0xA4}, {0x9A,0x49,0xA6}, {0x9A,0x49,0xB4}, {0x9A,0x49,0xB6},
    {0x9A,0x4D,0x24}, {0x9A,0x4D,0x26}, {0x9A,0x4D,0x34}, {0x9A,0x4D,0x36}, {0x9A,0x4D,0xA4}, {0x9A,0x4D,0xA6}, {0x9A,0x4D,0xB4}, {0x9A,0x4D,0xB6},
    {0x9A,0x69,0x24}, {0x9A,0x69,0x26}, {0x9A,0x69,0x34}, {0x9A,0x69,0x36}, {0x9A,0x69,0xA4}, {0x9A,0x69,0xA6}, {0x9A,0x69,0xB4}, {0x9A,0x69,0xB6},
    {0x9A,0x6D,0x24}, {0x9A,0x6D,0x26}, {0x9A,0x6D,0x34}, {0x9A,0x6D,0x36}, {0x9A,0x6D,0xA4}, {0x9A,0x6D,0xA6}, {0x9A,0x6D,0xB4}, {0x9A,0x6D,0xB6},
    {0x9B,0x49,0x24}, {0x9B,0x49,0x26}, {0x9B,0x49,0x34}, {0x9B,0x49,0x36}, {0x9B,0x49,0xA4}, {0x9B,0x49,0xA6}, {0x9B,0x49,0xB4}, {0x9B,0x49,0xB6},
    {0x9B,0x4D,0x24}, {0x9B,0x4D,0x26}, {0x9B,0x4D,0x34}, {0x9B,0x4D,0x36}, {0x9B,0x4D,0xA4}, {0x9B,0x4D,0xA6}, {0x9B,0x4D,0xB4}, {0x9B,0x4D,0xB6},
    {0x9B,0x69,0x24}, {0x9B,0x69,0x26}, {0x9B,0x69,0x34}, {0x9B,0x69,0x36}, {0x9B,0x69,0xA4}, {0x9B,0x69,0xA6}, {0x9B,0x69,0xB4}, {0x9B,0x69,0xB6},
    {0x9B,0x6D,0x24}, {0x9B,0x6D,0x26}, {0x9B,0x6D,0x34}, {0x9B,0x6D,0x36}, {0x9B,0x6D,0xA4}, {0x9B,0x6D,0xA6}, {0x9B,0x6D,0xB4}, {0x9B,0x6D,0xB6},
    {0xD2,0x49,0x24}, {0xD2,0x49,0x26}, {0xD2,0x49,0x34}, {0xD2,0x49,0x36}, {0xD2,0x49,0xA4}, {0xD2,0x49,0xA6}, {0xD2,0x49,0xB4}, {0xD2,0x49,0xB6},
    {0xD2,0x4D,0x24}, {0xD2,0x4D,0x26}, {0xD2,0x4D,0x34}, {0xD2,0x4D,0x36}, {0xD2,0x4D,0xA4}, {0xD2,0x4D,0xA6}, {0xD2,0x4D,0xB4}, {0xD2,0x4D,0xB6},
    {0xD2,0x69,0x24}, {0xD2,0x69,0x26}, {0xD2,0x69,0x34}, {0xD2,0x69,0x36}, {0xD2,0x69,0xA4}, {0xD2,0x69,0xA6}, {0xD2,0x69,0xB4}, {0xD2,0x69,0xB6},
    {0xD2,0x6D,0x24}, {0xD2,0x6D,0x26}, {0xD2,0x6D,0x34}, {0xD2,0x6D,0x36}, {0xD2,0x6D,0xA4}, {0xD2,0x6D,0xA6}, {0xD2,0x6D,0xB4}, {0xD2,0x6D,0xB6},
    {0xD3,0x49,0x24}, {0xD3,0x49,0x26}, {0xD3,0x49,0x34}, {0xD3,0x49,0x36}, {0xD3,0x49,0xA4}, {0xD3,0x49,0xA6}, {0xD3,0x49,0xB4}, {0xD3,0x49,0xB6},
    {0xD3,0x4D,0x24}, {0xD3,0x4D,0x26}, {0xD3,0x4D,0x34}, {0xD3,0x4D,0x36}, {0xD3,0x4D,0xA4}, {0xD3,0x4D,0xA6}, {0xD3,0x4D,0xB4}, {0xD3,0x4D,0xB6},
    {0xD3,0x69,0x24}, {0xD3,0x69,0x26}, {0xD3,0x69,0x34}, {0xD3,0x69,0x36}, {0xD3,0x69,0xA4}, {0xD3,0x69,0xA6}, {0xD3,0x69,0xB4}, {0xD3,0x69,0xB6},
    {0xD3,0x6D,0x24}, {0xD3,0x6D,0x26}, {0xD3,0x6D,0x34}, {0xD3,0x6D,0x36}, {0xD3,0x6D,0xA4}, {0xD3,0x6D,0xA6}, {0xD3,0x6D,0xB4}, {0xD3,0x6D,0xB6},
    {0xDA,0x49,0x24}, {0xDA,0x49,0x26}, {0xDA,0x49,0x34}, {0xDA,0x49,0x36}, {0xDA,0x49,0xA4}, {0xDA,0x49,0xA6}, {0xDA,0x49,0xB4}, {0xDA,0x49,0xB6},
    {0xDA,0x4D,0x24}, {0xDA,0x4D,0x26}, {0xDA,0x4D,0x34}, {0xDA,0x4D,0x36}, {0xDA,0x4D,0xA4}, {0xDA,0x4D,0xA6}, {0xDA,0x4D,0xB4}, {0xDA,0x4D,0xB6},
    {0xDA,0x69,0x24}, {0xDA,0x69,0x26}, {0xDA,0x69,0x34}, {0xDA,0x69,0x36}, {0xDA,0x69,0xA4}, {0xDA,0x69,0xA6}, {0xDA,0x69,0xB4}, {0xDA,0x69,0xB6},
    {0xDA,0x6D,0x24}, {0xDA,0x6D,0x26}, {0xDA,0x6D,0x34}, {0xDA,0x6D,0x36}, {0xDA,0x6D,0xA4}, {0xDA,0x6D,0xA6}, {0xDA,0x6D,0xB4}, {0xDA,0x6D,0xB6},
    {0xDB,0x49,0x24}, {0xDB,0x49,0x26}, {0xDB,0x49,0x34}, {0xDB,0x49,0x36}, {0xDB,0x49,0xA4}, {0xDB,0x49,0xA6}, {0xDB,0x49,0xB4}, {0xDB,0x49,0xB6},
    {0xDB,0x4D,0x24}, {0xDB,0x4D,0x26}, {0xDB,0x4D,0x34}, {0xDB,0x4D,0x36}, {0xDB,0x4D,0xA4}, {0xDB,0x4D,0xA6}, {0xDB,0x4D,0xB4}, {0xDB,0x4D,0xB6},
    {0xDB,0x69,0x24}, {0xDB,0x69,0x26}, {0xDB,0x69,0x34}, {0xDB,0x69,0x36}, {0xDB,0x69,0xA4}, {0xDB,0x69,0xA6}, {0xDB,0x69,0xB4}, {0xDB,0x69,0xB6},
    {0xDB,0x6D,0x24}, {0xDB,0x6D,0x26}, {0xDB,0x6D,0x34}, {0xDB,0x6D,0x36}, {0xDB,0x6D,0xA4}, {0xDB,0x6D,0xA6}, {0xDB,0x6D,0xB4}, {0xDB,0x6D,0xB6}
};

// Draws the ANIMATED 16x16 icon (4 frames, cycled via 'animation_frame')
// of the active tool in the top-right corner of the white area
// (pages 5-6, column 108), without overwriting the telemetry text that
// always lives starting at column 5.
void OLED_DrawToolIcon(uint8_t tool) {
    if (tool > TOOL_SCAN_PROBE) tool = TOOL_SOLDERING_IRON; // invalid ID safeguard
    uint8_t buf[16];

    memcpy(buf, ToolIcons[tool][animation_frame][0], 16);
    OLED_SetCursor(5, 108);
    OLED_WriteData(buf, 16);

    memcpy(buf, ToolIcons[tool][animation_frame][1], 16);
    OLED_SetCursor(6, 108);
    OLED_WriteData(buf, 16);
}

// Top yellow strip (pages 0-1, physical rows 0-15 of the two-tone panel).
// Page 0: CAN activity icon (blinks ~200ms after each RX) + English
// status message. Page 1: live "hero" variable depending on the active
// tool (RPM, temperature, power...). Called on every refresh, even
// during a critical error, so the operator keeps seeing live data.
void OLED_Render_YellowStrip(void) {
    uint8_t buf8[8];
    if (HAL_GetTick() - can_led_tick < 200) {
        memcpy(buf8, CANIcon, 8);
    } else {
        memset(buf8, 0, 8);
    }
    OLED_SetCursor(0, 0);
    OLED_WriteData(buf8, 8);

    const char* status_text;
    switch (active_tool) {
        case TOOL_SOLDERING_IRON:
            status_text = (target_temperature > 0 && current_temperature < target_temperature) ? "HEATING...   " : "READY        ";
            break;
        case TOOL_PASTE_DISPENSER:
        case TOOL_LIQUID_DISPENSER:
            status_text = (steps_remaining > 0) ? "DISPENSING   " : "READY        ";
            break;
        case TOOL_SCREWDRIVER:
            status_text = (steps_remaining > 0) ? "DRIVING      " : "READY        ";
            break;
        case TOOL_VACUUM_PICKUP:
            status_text = sensor_digital_reading ? "PART PICKED  " : "SEARCHING    ";
            break;
        case TOOL_DRILL:
            status_text = (drill_speed > 0) ? "DRILLING     " : "IDLE         ";
            break;
        case TOOL_GRIPPER_GIMBAL:
        case TOOL_GRIPPER_NEMA:
            status_text = (steps_remaining > 0) ? "GRIPPING     " : "READY        ";
            break;
        case TOOL_AOI_INSPECTION:
            status_text = (aoi_mode == 0x02) ? "SCANNING     " : (aoi_mode == 0x01) ? "LIGHT ON     " : "STANDBY      ";
            break;
        case TOOL_LASER_ENGRAVER:
            status_text = (laser_power_setpoint > 0) ? "ENGRAVING    " : "STANDBY      ";
            break;
        case TOOL_3D_PRINTER:
            status_text = (target_temperature > 0 && current_temperature < target_temperature) ? "HEATING...   " :
                         (steps_remaining > 0) ? "PRINTING     " : "READY        ";
            break;
        case TOOL_SCAN_PROBE:
            status_text = "ARMED        ";
            break;
        default:
            status_text = "STANDBY      ";
            break;
    }
    OLED_PrintStr(0, 10, status_text);

    char hero[24];
    uint8_t hp;
    switch (active_tool) {
        case TOOL_SOLDERING_IRON:
            hp = append_str(hero, "TEMP: ");
            hp += fmt_uint(hero+hp, current_temperature, 3);
            hp += append_str(hero+hp, " C        ");
            hero[hp] = 0;
            break;
        case TOOL_DRILL:
            hp = append_str(hero, "RPM: ");
            hp += fmt_uint(hero+hp, drill_actual_rpm, 5);
            hp += append_str(hero+hp, "          ");
            hero[hp] = 0;
            break;
        case TOOL_LASER_ENGRAVER:
            hp = append_str(hero, "LASER PWR: ");
            hp += fmt_uint(hero+hp, (laser_power_setpoint * 100) / 255, 3);
            hp += append_str(hero+hp, " %     ");
            hero[hp] = 0;
            break;
        case TOOL_3D_PRINTER:
            hp = append_str(hero, "HOTEND: ");
            hp += fmt_uint(hero+hp, current_temperature, 3);
            hp += append_str(hero+hp, " C      ");
            hero[hp] = 0;
            break;
        case TOOL_VACUUM_PICKUP:
            hp = append_str(hero, "VACUUM: ");
            hp += fmt_uint(hero+hp, sensor_analog_reading, 4);
            hp += append_str(hero+hp, "       ");
            hero[hp] = 0;
            break;
        case TOOL_AOI_INSPECTION:
            hp = append_str(hero, "STROBE: ");
            hp += fmt_uint(hero+hp, aoi_strobe_period, 4);
            hp += append_str(hero+hp, "       ");
            hero[hp] = 0;
            break;
        case TOOL_SCAN_PROBE:
            hp = append_str(hero, "HITS: ");
            // Display-only clamp: probe_impact_counter is a uint32_t with no
            // cap anywhere. At 10 digits, "HITS: " (6) + 10 + the 9 trailing
            // spaces below = 25 bytes into this 24-byte buffer - writing the
            // null terminator out of bounds. The real counter is untouched.
            hp += fmt_uint(hero+hp, (probe_impact_counter > 99999) ? 99999 : probe_impact_counter, 5);
            hp += append_str(hero+hp, "         ");
            hero[hp] = 0;
            break;
        default:
            hp = append_str(hero, "STEPS: ");
            hp += fmt_uint(hero+hp, (steps_remaining > 99999) ? 99999 : steps_remaining, 5);
            hp += append_str(hero+hp, "        ");
            hero[hp] = 0;
            break;
    }
    OLED_PrintStr(1, 0, hero);
}

// JuanenBOT boot logo + emblem + "U R T C", 128x40 (pages 0-4).
// Simple 4-frame robot-arm silhouette (base + articulated forearm + small
// gripper), animated during the splash screen (page 5, between the logo
// above and the version/date text below). Purely geometric/original -
// not based on any specific existing robot design.
// 4-frame animated JuanenBOT face (antenna wobble, blinking eyes, talking
// mouth) alongside the "JuanenBOT" wordmark, both living entirely in the
// white area of the OLED (below the yellow strip) - simple geometric shapes
// for the face, and the same "JuanenBOT" typography already used elsewhere
// in this file, just re-cropped and repositioned. 5 pages x 128 columns per
// frame, matching this screen's white region exactly (pages 2-6).
static const uint8_t SplashFace[4][5][128] = {
    { // frame 0
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x7F,0x7F,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0xFC,0xFC,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0xFC,0xFC,0xFC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0x30,0x30,0xF0,0xF0,0xE0,0x00,0x00,0xE0,0xF0,0xF0,0x30,0x30,0xF0,0xF0,0xE0,0x00,0x30,0x30,0xF0,0xF0,0xF0,0x30,0x30,0x30,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0,0x00,0x00,0xFF,0xFF,0xFF,0x00,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x9E,0xDF,0xDF,0x63,0x63,0x23,0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0x02,0x03,0xFF,0xFF,0xFE,0x00,0x00,0xFE,0xFF,0xFF,0x33,0x33,0xBF,0xBF,0xBE,0x00,0xFF,0xFF,0xFF,0x02,0x03,0xFF,0xFF,0xFE,0x00,0x00,0xFF,0xFF,0xFF,0x18,0x18,0xFF,0xFF,0xE7,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xE0,0xE0,0xE0,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE0,0xE0,0xE0,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x07,0x0F,0x0F,0x0C,0x04,0x0F,0x0F,0x0F,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x04,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x00,0x00},
    },
    { // frame 1
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x7F,0x7F,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0xFC,0xFC,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0xFC,0xFC,0xFC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0x30,0x30,0xF0,0xF0,0xE0,0x00,0x00,0xE0,0xF0,0xF0,0x30,0x30,0xF0,0xF0,0xE0,0x00,0x30,0x30,0xF0,0xF0,0xF0,0x30,0x30,0x30,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0,0x00,0x00,0xFF,0xFF,0xFF,0x00,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x9E,0xDF,0xDF,0x63,0x63,0x23,0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0x02,0x03,0xFF,0xFF,0xFE,0x00,0x00,0xFE,0xFF,0xFF,0x33,0x33,0xBF,0xBF,0xBE,0x00,0xFF,0xFF,0xFF,0x02,0x03,0xFF,0xFF,0xFE,0x00,0x00,0xFF,0xFF,0xFF,0x18,0x18,0xFF,0xFF,0xE7,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xE0,0xE0,0xE0,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE0,0xE0,0xE0,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x07,0x0F,0x0F,0x0C,0x04,0x0F,0x0F,0x0F,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x04,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x00,0x00},
    },
    { // frame 2
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x7F,0x7F,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0xFC,0xFC,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0xFC,0xFC,0xFC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0x30,0x30,0xF0,0xF0,0xE0,0x00,0x00,0xE0,0xF0,0xF0,0x30,0x30,0xF0,0xF0,0xE0,0x00,0x30,0x30,0xF0,0xF0,0xF0,0x30,0x30,0x30,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0,0x00,0x00,0xFF,0xFF,0xFF,0x00,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x9E,0xDF,0xDF,0x63,0x63,0x23,0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0x02,0x03,0xFF,0xFF,0xFE,0x00,0x00,0xFE,0xFF,0xFF,0x33,0x33,0xBF,0xBF,0xBE,0x00,0xFF,0xFF,0xFF,0x02,0x03,0xFF,0xFF,0xFE,0x00,0x00,0xFF,0xFF,0xFF,0x18,0x18,0xFF,0xFF,0xE7,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xE0,0xE0,0xE0,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE0,0xE0,0xE0,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x07,0x0F,0x0F,0x0C,0x04,0x0F,0x0F,0x0F,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x04,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x00,0x00},
    },
    { // frame 3
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x7F,0x7F,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0xFC,0xFC,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0xFC,0xFC,0xFC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF0,0xF0,0xF0,0x30,0x30,0xF0,0xF0,0xE0,0x00,0x00,0xE0,0xF0,0xF0,0x30,0x30,0xF0,0xF0,0xE0,0x00,0x30,0x30,0xF0,0xF0,0xF0,0x30,0x30,0x30,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0,0x00,0x00,0xFF,0xFF,0xFF,0x00,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x9E,0xDF,0xDF,0x63,0x63,0x23,0xFF,0xFF,0xFE,0xFF,0xFF,0xFF,0x02,0x03,0xFF,0xFF,0xFE,0x00,0x00,0xFE,0xFF,0xFF,0x33,0x33,0xBF,0xBF,0xBE,0x00,0xFF,0xFF,0xFF,0x02,0x03,0xFF,0xFF,0xFE,0x00,0x00,0xFF,0xFF,0xFF,0x18,0x18,0xFF,0xFF,0xE7,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00},
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xE0,0xE0,0xE0,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE3,0xE0,0xE0,0xE0,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x07,0x0F,0x0F,0x0C,0x04,0x0F,0x0F,0x0F,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x04,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x0F,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x00,0x07,0x0F,0x0F,0x0C,0x0C,0x0F,0x0F,0x07,0x00,0x00,0x00,0x0F,0x0F,0x0F,0x00,0x00,0x00,0x00},
    },
};;


void Render_SplashScreen(uint8_t face_frame) {
    uint8_t buf[128];
    for(uint8_t p=0; p<8; p++) OLED_ClearPage(p);

    // Yellow strip (pages 0-1, 16px): URTC big and centered, firmware
    // version smaller alongside it - both baked into one bitmap so they sit
    // together cleanly rather than as separate small OLED_PrintStr calls.
    for (uint8_t p = 0; p < 2; p++) {
        memcpy(buf, SplashYellowText[p], 128);
        OLED_SetCursor(p, 0);
        OLED_WriteData(buf, 128);
    }

    // White area (pages 2-6, 40px): animated JuanenBOT face on the left,
    // the "JuanenBOT" wordmark on the right - both live entirely within the
    // white region now. Page 7 is left blank as a small bottom margin.
    if (face_frame > 3) face_frame = 0;
    for (uint8_t p = 0; p < 5; p++) {
        memcpy(buf, SplashFace[face_frame][p], 128);
        OLED_SetCursor(p + 2, 0);
        OLED_WriteData(buf, 128);
    }
}

void Process_OLED_NightModeChange(void) {
    oled_mode_pending_flag = 0;
    oled_night_mode = oled_night_mode_cmd;
    if (oled_night_mode == 0x01) {
        // Explicitly wakes the display (0xAF) before setting contrast -
        // contrast alone has no effect if the display is currently asleep
        // (put there via 0x0F, which sends only 0xAE). Harmless when
        // already on; the SSD1306 tolerates a redundant 0xAF.
        OLED_WriteCmd(0xAF);
        OLED_WriteCmd(0x81); OLED_WriteCmd(0x0F); 
    }
    else if (oled_night_mode == 0x0F) {
        OLED_WriteCmd(0xAE); 
    }
    else {
        OLED_WriteCmd(0xAE);
        OLED_WriteCmd(0x81); OLED_WriteCmd(0xCF); 
        OLED_WriteCmd(0xAF); 
    }
}

void Render_ToolScreen(void) {
    char txt[24];

    // Dedicated path for "no recognized tool head" (IDs 12-15) - checked
    // before the generic error message and before the normal yellow strip,
    // since there's no live CAN/tool data to show for this state, and it
    // deserves its own clear identity rather than looking like a generic
    // fault on a tool that IS actually connected.
    if (active_tool == TOOL_INVALID) {
        // Yellow strip (pages 0-1): just the big "CHECK ID JUMPERS" text,
        // filling the strip - dropped the "URTC - NO TOOL" line per feedback.
        for (uint8_t p = 0; p < 2; p++) {
            uint8_t ybuf[128];
            memcpy(ybuf, CheckIdJumpersText[p], 128);
            OLED_SetCursor(p, 0);
            OLED_WriteData(ybuf, 128);
        }
        for (uint8_t page = 2; page < 8; page++) OLED_ClearPage(page);
        // White area: nothing but the big blinking ERROR wordmark. Blink:
        // alternate on/off using the same animation_frame the tool icons
        // use (0-3) - checking evenness keeps a clean on/off blink (2 frames
        // on, 2 frames off, ~900ms per half-cycle) rather than a 3-on/1-off skew.
        for (uint8_t page = 2; page <= 6; page++) {
            uint8_t buf[128];
            if (animation_frame % 2 == 0) {
                memcpy(buf, ErrorText[page - 2], 128);
            } else {
                memset(buf, 0, 128);
            }
            OLED_SetCursor(page, 0);
            OLED_WriteData(buf, 128);
        }
        OLED_PrintStr(7, 4, "CHECK ID JUMPERS");
        return;
    }

    OLED_Render_YellowStrip(); // yellow strip (CAN + hero) always refreshes

    if (system_error_flag) {
        OLED_PrintStr(2, 10, "[ CRITICAL ERROR ]");
        OLED_PrintStr(3, 10, " SYSTEM BLOCKED ");
        return;
    }
    
    switch(active_tool) {
        case TOOL_SOLDERING_IRON:           OLED_PrintStr(2, 16, "T12 SOLDER IRON"); break;
        case TOOL_PASTE_DISPENSER:  OLED_PrintStr(2, 16, "PASTE DISPENSER"); break;
        case TOOL_LIQUID_DISPENSER:    OLED_PrintStr(2, 16, "LIQUID DISPENSR"); break;
        case TOOL_SCREWDRIVER:     OLED_PrintStr(2, 16, "SCREWDRIVER    "); break;
        case TOOL_VACUUM_PICKUP:            OLED_PrintStr(2, 16, "VACUUM PICKUP  "); break;
        case TOOL_DRILL:            OLED_PrintStr(2, 16, "DRILL BL4260   "); break;
        case TOOL_GRIPPER_GIMBAL:     OLED_PrintStr(2, 16, "GRIPPER GIMBAL "); break;
        case TOOL_GRIPPER_NEMA:       OLED_PrintStr(2, 16, "GRIPPER NEMA 14"); break;
        case TOOL_AOI_INSPECTION:     OLED_PrintStr(2, 16, "AOI INSPECTION "); break;
        case TOOL_LASER_ENGRAVER:     OLED_PrintStr(2, 16, "LASER DIODE 10W"); break;
        case TOOL_3D_PRINTER:       OLED_PrintStr(2, 16, "3D PRINTER  V4 "); break;
        case TOOL_SCAN_PROBE:      OLED_PrintStr(2, 16, "SCAN PROBE     "); break;
        default: break; // TOOL_INVALID never reaches here - caught by the early return above
    }
    
    OLED_PrintStr(3, 0, "--------------------");
    
    uint8_t tp;
    switch(active_tool) {
        case TOOL_SOLDERING_IRON: {
            // Same defensive display-only clamp as the 3D printer's
            // identical NOZZLE: line below - target_temperature is a
            // uint16_t set directly from CAN bytes (0x130), and this
            // doesn't touch the real value (Control_SolderingIron_PID's
            // own 450C safety ceiling already corrects that within 20ms).
            uint16_t display_target_temp = (target_temperature > 999) ? 999 : target_temperature;
            tp = append_str(txt, "TEMP: ");
            tp += fmt_uint(txt+tp, current_temperature, 3);
            tp += append_str(txt+tp, "/");
            tp += fmt_uint(txt+tp, display_target_temp, 3);
            tp += append_str(txt+tp, " C");
            txt[tp] = 0;
            OLED_PrintStr(5, 5, txt);
            OLED_PrintStr(6, 5, endstop_triggered ? "ENDSTOP: HIT  " : "ENDSTOP: OPEN ");
            OLED_DrawHorizontalBar(7, 5, 85, (current_temperature * 100) / 450);
            OLED_DrawToolIcon(TOOL_SOLDERING_IRON);
            break;
        }
        case TOOL_PASTE_DISPENSER:
        case TOOL_LIQUID_DISPENSER:
        case TOOL_SCREWDRIVER:
        case TOOL_GRIPPER_GIMBAL:
        case TOOL_GRIPPER_NEMA: {
            __disable_irq();
            uint32_t steps_rem_snapshot = steps_remaining;
            uint32_t steps_total_snapshot = total_steps_setpoint;
            __enable_irq();
            if (steps_rem_snapshot > 0) {
                // Display-only clamp: steps_remaining comes straight from an
                // unvalidated 32-bit CAN payload with nothing capping it. A
                // very large value (up to 10 digits) would overflow this
                // line's ~17-character budget before the icon column at
                // pixel 108, corrupting it. Only the displayed copy is
                // clamped - the real steps_remaining below (used by the step
                // ISR) is untouched, so a genuinely long move still runs correctly.
                uint32_t steps_display = (steps_rem_snapshot > 99999) ? 99999 : steps_rem_snapshot;
                tp = append_str(txt, "STEPS REM: ");
                tp += fmt_uint(txt+tp, steps_display, 5);
                txt[tp] = 0;
                OLED_PrintStr(5, 5, txt);
                uint8_t pct = (steps_total_snapshot > 0)
                    ? 100 - (uint8_t)(((uint64_t)steps_rem_snapshot * 100) / steps_total_snapshot)
                    : 0; // defensive: steps_total_snapshot and steps_rem_snapshot are only ever
                         // set together by the same command handler, so this shouldn't be
                         // reachable, but a zero-cost guard removes any doubt rather than relying on that.
                OLED_DrawHorizontalBar(7, 5, 85, pct);
            } else {
                OLED_PrintStr(5, 5, "STATUS: READY");
                OLED_DrawHorizontalBar(7, 5, 85, 100);
            }
            OLED_DrawToolIcon(active_tool); // each of the 5 tools' own icon
            break;
        }
        case TOOL_VACUUM_PICKUP:
            tp = append_str(txt, "VACUUM ADC: ");
            tp += fmt_uint(txt+tp, sensor_analog_reading, 3);
            txt[tp] = 0;
            OLED_PrintStr(5, 5, txt);
            tp = append_str(txt, "OBJ: ");
            tp += append_str(txt+tp, (sensor_digital_reading) ? "OK" : "FILL");
            txt[tp] = 0;
            OLED_PrintStr(7, 5, txt);
            OLED_DrawToolIcon(TOOL_VACUUM_PICKUP);
            break;
        case TOOL_DRILL:
            tp = append_str(txt, "REAL RPM: ");
            tp += fmt_uint(txt+tp, drill_actual_rpm, 5);
            txt[tp] = 0;
            OLED_PrintStr(5, 5, txt);
            OLED_PrintStr(6, 5, endstop_triggered ? "ENDSTOP: HIT  " : "ENDSTOP: OPEN ");
            tp = append_str(txt, "SETPOINT: ");
            tp += fmt_uint(txt+tp, drill_speed, 3);
            tp += append_str(txt+tp, "%");
            txt[tp] = 0;
            OLED_PrintStr(7, 5, txt);
            OLED_DrawToolIcon(TOOL_DRILL);
            break;
        case TOOL_LASER_ENGRAVER:
            tp = append_str(txt, "POWER:    ");
            tp += fmt_uint(txt+tp, laser_power_setpoint, 3);
            tp += append_str(txt+tp, "%");
            txt[tp] = 0;
            OLED_PrintStr(5, 5, txt);
            OLED_PrintStr(6, 5, endstop_triggered ? "ENDSTOP: HIT  " : "ENDSTOP: OPEN ");
            OLED_DrawHorizontalBar(7, 5, 85, (laser_power_setpoint * 100) / 255);
            OLED_DrawToolIcon(TOOL_LASER_ENGRAVER);
            break;
        case TOOL_3D_PRINTER: {
            // Defensive clamp for display purposes only - doesn't touch
            // the real target_temperature (Control_SolderingIron_PID's own
            // 450C safety ceiling already corrects that within 20ms of a
            // bad value arriving). This just protects THIS fixed-width
            // line from the brief window before that correction lands,
            // since target_temperature is a uint16_t set directly from
            // CAN bytes and could otherwise be far more than 3 digits.
            uint16_t display_target_temp = (target_temperature > 999) ? 999 : target_temperature;
            tp = append_str(txt, "NOZZLE:");
            tp += fmt_uint(txt+tp, current_temperature, 3);
            tp += append_str(txt+tp, "/");
            tp += fmt_uint(txt+tp, display_target_temp, 3);
            tp += append_str(txt+tp, "C");
            txt[tp] = 0;
            OLED_PrintStr(5, 5, txt);
            tp = append_str(txt, "LFAN:");
            tp += fmt_uint(txt+tp, (layer_fan_duty * 100) / 255, 3);
            tp += append_str(txt+tp, "%");
            tp += fmt_uint(txt+tp, layer_fan_actual_rpm, 4);
            tp += append_str(txt+tp, "RPM");
            txt[tp] = 0;
            OLED_PrintStr(6, 5, txt);
            tp = append_str(txt, "HFAN:");
            tp += fmt_uint(txt+tp, (hotend_fan_duty * 100) / 255, 3);
            tp += append_str(txt+tp, "%");
            tp += fmt_uint(txt+tp, hotend_fan_actual_rpm, 4);
            tp += append_str(txt+tp, "RPM");
            txt[tp] = 0;
            OLED_PrintStr(7, 5, txt);
            OLED_DrawToolIcon(TOOL_3D_PRINTER);
            break;
        }
        case TOOL_AOI_INSPECTION: {
            const char* mode_text = (aoi_mode == 0x01) ? "LIGHT: FIXED  " :
                                   (aoi_mode == 0x02) ? "LIGHT: STROBE " :
                                                         "LIGHT: OFF    ";
            OLED_PrintStr(5, 5, mode_text);
            OLED_PrintStr(6, 5, endstop_triggered ? "ENDSTOP: HIT  " : "ENDSTOP: OPEN ");
            tp = append_str(txt, "PERIOD: ");
            tp += fmt_uint(txt+tp, aoi_strobe_period, 4);
            txt[tp] = 0;
            OLED_PrintStr(7, 5, txt);
            OLED_DrawToolIcon(TOOL_AOI_INSPECTION);
            break;
        }
        case TOOL_SCAN_PROBE:
            OLED_PrintStr(5, 5, "STATUS: ARMED ");
            tp = append_str(txt, "IMPACTS:  ");
            // Display-only clamp, same reasoning as the other
            // probe_impact_counter display above: a uint32_t with no cap
            // anywhere could otherwise write more digits than this
            // 24-byte buffer has room for. The real counter is untouched.
            tp += fmt_uint(txt+tp, (probe_impact_counter > 99999) ? 99999 : probe_impact_counter, 5);
            txt[tp] = 0;
            OLED_PrintStr(7, 5, txt);
            OLED_DrawToolIcon(TOOL_SCAN_PROBE);
            break;
        default:
            OLED_PrintStr(5, 10, "BUS CAN ONLINE");
            break;
    }
}

// =============================================================================
// 5. WS2812B LED EMULATION DRIVER (STATUS LED DMA / RING LED BIT-BANG)
// =============================================================================
// 5. WS2812B LED DRIVER (SPI1 + DMA, PA7/MOSI) - drives BOTH the status LED
//    and the ring in a single transfer, replacing the previous bit-banged
//    approach entirely now that PA7 is free to carry SPI1 instead of PWM.
// =============================================================================
uint8_t led_spi_buffer[200];

// CONN_LED1 (status, 1 pixel) via SPI1+DMA on PA7 - kept independent from the
// ring below rather than combined into one chain, specifically so CONN_LED1
// and CONN_LED2 don't need a new PCB trace joining them in series. This
// pixel's data volume is tiny (9 SPI bytes + reset), so there's little to
// gain from moving it into a shared transfer anyway.
// Automatic status LED coloring (CONN_LED1) - runs every main loop
// iteration, just before the color actually gets sent out. Three-way
// priority: a hardware fault always wins over everything else; a recent
// host-sent CAN 0x100 color takes the next priority and is left alone;
// otherwise the board colors itself blue (CAN traffic within the last
// 1.5s - "actively functioning/communicating") or green (nothing that
// recent - "idle, waiting for commands").
void Update_StatusLED_AutoColor(void) {
    uint8_t prev_r = led_state_pixel.R, prev_g = led_state_pixel.G, prev_b = led_state_pixel.B;

    if (system_error_flag) {
        led_state_pixel.R = 255; led_state_pixel.G = 0; led_state_pixel.B = 0;
        led_host_override_active = 0; // a fault clearing shouldn't snap back
                                       // to a stale host color from before it
    } else if (led_host_override_active &&
               (int32_t)(HAL_GetTick() - led_host_override_expire_tick) < 0) {
        // still within the held window - leave the host's color alone
    } else {
        led_host_override_active = 0; // expired, or was never active
        if (HAL_GetTick() - can_led_tick < LED_FUNCTIONING_WINDOW_MS) {
            led_state_pixel.R = 0; led_state_pixel.G = 0; led_state_pixel.B = 255;
        } else {
            led_state_pixel.R = 0; led_state_pixel.G = 255; led_state_pixel.B = 0;
        }
    }

    // Only ask for an actual SPI transmission if the color genuinely
    // changed - this runs every main loop iteration (it has to, since the
    // blue/green split depends on elapsed time), but re-sending the same
    // three bytes every iteration would be pointless traffic on the SPI/DMA
    // link and could fight with the busy-check in Update_StatusLED_SPI_DMA.
    if (led_state_pixel.R != prev_r || led_state_pixel.G != prev_g || led_state_pixel.B != prev_b) {
        update_led_ring_flag = 1;
    }
}

uint8_t Update_StatusLED_SPI_DMA(void) {
    if (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) return 0;

    uint32_t idx = 0;
    memcpy(&led_spi_buffer[idx], ws2812_lut[led_state_pixel.G], 3); idx += 3;
    memcpy(&led_spi_buffer[idx], ws2812_lut[led_state_pixel.R], 3); idx += 3;
    memcpy(&led_spi_buffer[idx], ws2812_lut[led_state_pixel.B], 3); idx += 3;
    memset(&led_spi_buffer[idx], 0x00, 100); // reset/latch, same reasoning as before
    idx += 100;

    HAL_SPI_Transmit_DMA(&hspi1, led_spi_buffer, idx);
    return 1;
}

// CONN_LED2 (ring, 8 pixels) - back to bit-banging on PB1, independent of
// CONN_LED1/PA7. Same technique used throughout this project before the
// SPI/DMA redesign: NOPs tuned for this core's 64MHz clock (15.625ns/cycle),
// LOW-phase NOPs and the trailing latch delay both included from the start
// this time (rather than as later patches), and the color captured once,
// atomically, before the pixel loop.
void Update_RingLEDs_BitBang(void) {
    __disable_irq();
    for (uint8_t p = 0; p < 8; p++) {
        uint8_t g = ring_pixels[p].G, r = ring_pixels[p].R, b = ring_pixels[p].B;
        for (int8_t i = 7; i >= 0; i--) {
            if (g & (1 << i)) {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            } else {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            }
        }
        for (int8_t i = 7; i >= 0; i--) {
            if (r & (1 << i)) {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            } else {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            }
        }
        for (int8_t i = 7; i >= 0; i--) {
            if (b & (1 << i)) {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            } else {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            }
        }
    }
    // Reset/latch delay, re-enabled before it since the electrical hold
    // doesn't need cycle-precise timing the way bit transmission does -
    // matches the reasoning already used elsewhere in this file.
    __enable_irq();
    for (volatile uint32_t w = 0; w < 4000; w++);
}

// Bit-banged SPI transfer for CONN_EXPANSION's EXP_SPI_* pins - see the pin
// definitions above and PINOUT_CONNECTORS.TXT's CONN_EXPANSION note for why
// this is software-timed rather than a hardware SPI peripheral.
//
// Implements SPI Mode 3 (CPOL=1, CPHA=1) specifically - confirmed against
// the TMC5160/TMC5160A datasheet's own SPI Interface section (section 4.2:
// "the node latching the data from SDI on the rising edge of SCK and
// driving data to SDO following the falling edge... Hint: Usually this SPI
// timing is referred to as SPI MODE 3"), not assumed. Getting this wrong
// wouldn't cause a hard fault or an obviously broken symptom - it would
// just silently fail to communicate, or read back garbage that looks
// plausible enough to be confusing. Clock idles HIGH between transfers,
// matching Mode 3 - the caller is responsible for asserting EXP_SPI_CS_PIN
// low before the first byte of a transaction and releasing it high after
// the last one; this function only handles the 8-bit shift itself.
//
// Timing is intentionally not cycle-counted the way the WS2812B driver
// above is - unlike WS2812B's protocol-defined bit timing, SPI is
// edge-triggered rather than time-triggered: the slave samples relative to
// the clock edges this function generates, not against a wall-clock
// deadline, so a plain small delay between edges (not a precise one) is
// sufficient and correct.
static void ExpansionSPI_Delay(void) {
    for (volatile uint32_t d = 0; d < 20; d++);
}

uint8_t ExpansionSPI_TransferByte(uint8_t tx_byte) {
    uint8_t rx_byte = 0;
    for (int8_t i = 7; i >= 0; i--) {
        // Setup: drive the next output bit while SCK is still high (idle)
        // from the previous iteration (or CS assertion, for the first bit) -
        // matches Mode 3's "data changes on the falling edge" by having the
        // new value already stable before that edge happens below.
        if (tx_byte & (1 << i)) {
            HAL_GPIO_WritePin(EXP_SPI_MOSI_PORT, EXP_SPI_MOSI_PIN, GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(EXP_SPI_MOSI_PORT, EXP_SPI_MOSI_PIN, GPIO_PIN_RESET);
        }
        HAL_GPIO_WritePin(EXP_SPI_SCK_PORT, EXP_SPI_SCK_PIN, GPIO_PIN_RESET); // falling edge
        ExpansionSPI_Delay();
        HAL_GPIO_WritePin(EXP_SPI_SCK_PORT, EXP_SPI_SCK_PIN, GPIO_PIN_SET); // rising edge - sample here
        rx_byte <<= 1;
        if (HAL_GPIO_ReadPin(EXP_SPI_MISO_PORT, EXP_SPI_MISO_PIN) == GPIO_PIN_SET) {
            rx_byte |= 0x01;
        }
        ExpansionSPI_Delay();
    }
    return rx_byte;
}

void MX_ExpansionSPI_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // CS idles high (deselected) - set before configuring as output so
    // there's never a glitch low, even momentarily, during setup.
    HAL_GPIO_WritePin(EXP_SPI_CS_PORT, EXP_SPI_CS_PIN, GPIO_PIN_SET);
    // SCK idles high, matching Mode 3.
    HAL_GPIO_WritePin(EXP_SPI_SCK_PORT, EXP_SPI_SCK_PIN, GPIO_PIN_SET);

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    GPIO_InitStruct.Pin = EXP_SPI_CS_PIN;
    HAL_GPIO_Init(EXP_SPI_CS_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = EXP_SPI_SCK_PIN;
    HAL_GPIO_Init(EXP_SPI_SCK_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = EXP_SPI_MOSI_PIN;
    HAL_GPIO_Init(EXP_SPI_MOSI_PORT, &GPIO_InitStruct);

    // MISO is the only input of the four - pulled up so an expansion board
    // that isn't actually populated yet (or a chip in reset/unpowered)
    // reads back a stable, known 0xFF pattern rather than a floating,
    // noise-sensitive line.
    GPIO_InitStruct.Pin = EXP_SPI_MISO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(EXP_SPI_MISO_PORT, &GPIO_InitStruct);

    // TMC_DIAG0 - plain input, no internal pull. The board now has a real
    // external pull-down (R50, 10K) as part of the diode-OR combining this
    // with the onboard TMC2209's own DIAG - enabling the MCU's internal
    // pull-up here as well would fight that external pull-down and leave
    // the idle level ambiguous instead of a clean, unambiguous low. See
    // this pin's own #define comment for the full topology.
    GPIO_InitStruct.Pin = TMC_DIAG0_PIN;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(TMC_DIAG0_PORT, &GPIO_InitStruct);
}


// =============================================================================
// 6. SAFE PID THERMAL CONTROL LOOPS (PHYSICAL LIMIT INCLUDED)
// =============================================================================
void Control_SolderingIron_PID(void) {
    if (active_tool != TOOL_SOLDERING_IRON) return;

    // The T12 cartridge routes its heater current and its internal
    // thermocouple through the same two conductors. Force the heater off
    // and give the line a brief settling window before sampling - reading
    // while it's still on (e.g. left on from the previous 20ms cycle's
    // decision) would sample the 24V switching transient instead of the
    // thermocouple's real microvolt-level signal. The bang-bang decision
    // below turns it back on immediately if still needed; the tip's own
    // thermal mass is large relative to this brief window.
    HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
    for (volatile uint32_t settle = 0; settle < 640; settle++); // ~10us @ 64MHz

    // AD8605 thermocouple ADC reading
    HAL_ADC_Start(&hadc);
    // 2ms timeout, not 10ms: a healthy conversion completes in
    // microseconds at any reasonable clock config, so this only ever
    // actually waits the full duration when something's already wrong -
    // bounding it tighter limits how long a stuck ADC can stall the rest
    // of this 20ms-cyclic block (watchdogs included) before either
    // succeeding or falling through to the fault handling below.
    if (HAL_ADC_PollForConversion(&hadc, 2) == HAL_OK) {
        uint32_t adc_val = HAL_ADC_GetValue(&hadc);
        // Same fault-detection philosophy as the hotend NTC below: a failed
        // amplifier or an open/shorted thermocouple typically pins the ADC
        // reading near one rail (~0 or ~4095), not somewhere in the middle
        // of the working range. Catch that instead of computing and trusting
        // a bogus temperature from it.
        if (adc_val > 15 && adc_val < 4090) { // see the hotend NTC section below for why 4090, not 4080
            current_temperature = (uint16_t)(((adc_val * 450) + 2047) / 4095); // Mapped to 450C max, rounded to nearest degree
        } else {
            system_error_flag = 1;
        }
    } else {
        // A conversion that can't complete even within this 2ms budget is
        // exactly as untrustworthy as one that comes back out of range -
        // raising the fault here (rather than leaving current_temperature
        // frozen at its last value) keeps the heater from running off a
        // stale reading with no indication anything's wrong.
        system_error_flag = 1;
    }
    HAL_ADC_Stop(&hadc);

    // Strict independent safety ceiling to mitigate catastrophic thermal-runaway risk.
    // 445, not 450: the ADC range guard above caps current_temperature's own
    // max representable value at 449 (adc_val must stay below 4090 to be
    // trusted at all - see that guard's own comment). The bang-bang shutoff
    // below only fires once current_temperature exceeds target_temperature+2,
    // so a target sitting right at 450 would leave that condition
    // (449 > 452) permanently unsatisfiable - the heater reaching its own
    // measurable ceiling with no way to command itself off. 445 leaves the
    // shutoff condition (up to 447) comfortably below what can actually be
    // measured.
    if (target_temperature > 445) {
        target_temperature = 0;
        system_error_flag = 1;
    }

    if (system_error_flag) {
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
        return;
    }

    // Stuck-heater detection: a MOSFET or SSR that's fused/shorted from
    // thermal stress keeps conducting regardless of what this GPIO
    // commands - a real, known power-semiconductor failure mode. If the
    // output's been continuously off for 3s yet temperature keeps rising
    // instead of leveling off or falling, that's consistent with exactly
    // that, well before the absolute 450C ceiling below would catch it.
    // This adds earlier warning on top of that ceiling, not instead of it -
    // if the power stage is genuinely fused, cutting this signal can't
    // actually help either way; a real fix needs an independent hardware
    // cutoff downstream of this GPIO, which this alone can't provide.
    static uint8_t heater_on = 0;
    static uint32_t t12_pwm_off_since = 0;
    static uint16_t t12_temp_at_off = 0;
    if (heater_on) {
        t12_pwm_off_since = 0;
    } else {
        if (t12_pwm_off_since == 0) {
            t12_pwm_off_since = HAL_GetTick();
            t12_temp_at_off = current_temperature;
        } else if (HAL_GetTick() - t12_pwm_off_since > 3000 &&
                   current_temperature > t12_temp_at_off + 5) {
            system_error_flag = 1;
            HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
            return;
        }
    }

    // Bang-Bang thermal loop, gated by the absolute safety ceiling. Small
    // hysteresis band (+/-2C) added: with zero hysteresis, once the reading
    // settles exactly at the setpoint this would toggle the heater on/off
    // every 20ms tick indefinitely - pure switching stress and noise for no
    // control benefit.
    if ((int32_t)current_temperature < (int32_t)target_temperature - 2) {
        heater_on = 1;
    } else if ((int32_t)current_temperature > (int32_t)target_temperature + 2) {
        heater_on = 0;
    }
    HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, heater_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// FLASH OPTIMIZATION: replaces the float/logf() Beta-equation calculation.
// Cortex-M0 has no hardware FPU, so every float operation needs a software
// library routine - the log() call alone plus the float add/sub/mul/div it
// needs pulled in several KB of C library code for a calculation that only
// ever needed to run 33 times' worth of precision. This table was generated
// from the exact same formula (R_PULLUP=4.7k, Beta=3950, R0=100k, T0=25C),
// sampled every 10C from 0 to 320C (deliberately uniform in TEMPERATURE, not
// ADC counts, since the curve is steep enough that uniform-ADC spacing gave
// interpolation errors over 100C at the extremes - this spacing keeps the
// worst-case linear-interpolation error to 0.45C across the whole range).
// {raw_adc, temperature_C}, ascending by raw_adc.
static const int16_t NTC_TABLE[33][2] = {
    {116,320},{130,310},{146,300},{164,290},{185,280},{210,270},{239,260},
    {273,250},{313,240},{360,230},{416,220},{482,210},{560,200},{653,190},
    {763,180},{893,170},{1045,160},{1221,150},{1423,140},{1650,130},
    {1901,120},{2169,110},{2447,100},{2724,90},{2989,80},{3232,70},
    {3444,60},{3621,50},{3762,40},{3869,30},{3947,20},{4002,10},{4039,0}
};

static int16_t NTC_Lookup(uint16_t raw_adc) {
    if (raw_adc <= (uint16_t)NTC_TABLE[0][0]) return NTC_TABLE[0][1];
    if (raw_adc >= (uint16_t)NTC_TABLE[32][0]) return NTC_TABLE[32][1];
    for (uint8_t i = 0; i < 32; i++) {
        int16_t adc0 = NTC_TABLE[i][0], adc1 = NTC_TABLE[i+1][0];
        if ((int16_t)raw_adc >= adc0 && (int16_t)raw_adc <= adc1) {
            int16_t t0 = NTC_TABLE[i][1], t1 = NTC_TABLE[i+1][1];
            // C truncates division toward zero, not toward negative
            // infinity. Because this divider is inverse (hotter = lower
            // ADC), the table is descending - (t1-t0) is negative within
            // every segment, so this numerator is negative whenever
            // raw_adc > adc0. Sign-aware rounding (below) avoids the
            // sawtooth bias plain truncation would otherwise produce at
            // every table breakpoint.
            int32_t numerator = (int32_t)((int16_t)raw_adc - adc0) * (t1 - t0);
            int32_t denom = (adc1 - adc0);
            int32_t rounding = (numerator >= 0) ? (denom / 2) : -(denom / 2);
            return t0 + (numerator + rounding) / denom;
        }
    }
    return NTC_TABLE[32][1]; // unreachable given the guards above
}

void Control_3D_Hotend_PID(void) {
    if (active_tool != TOOL_3D_PRINTER) return;
    
    HAL_ADC_Start(&hadc);
    // 2ms timeout - see the soldering iron's identical 2ms budget above
    if (HAL_ADC_PollForConversion(&hadc, 2) == HAL_OK) {
        uint32_t raw_adc = HAL_ADC_GetValue(&hadc);
        // Two things this reading depends on getting right:
        // 1) Topology: R40 (4.7k, confirmed in the netlist) pulls PB0 up to
        //    +3.3V, and the 100k NTC pulls the same node down to GND (per
        //    CONN_SEN: Pin1=GND to the NTC, Pin2=PB0 to the NTC). That's an
        //    inverse divider: hotter -> lower NTC resistance -> lower ADC
        //    reading - the opposite of a direct relationship, which matters
        //    since getting it backwards would read a heating hotend as
        //    cooling down to the PID, a genuine thermal-runaway risk.
        // 2) An NTC isn't linear with temperature; this uses the standard
        //    Beta equation instead of a straight-line guess.
        if (raw_adc > 15 && raw_adc < 4090) { // guard against open/shorted sensor
            // 4090 (~449.6C at this chip's 450C/4095-count mapping) leaves
            // the iron's full legitimate 450C operating range untouched
            // while still catching genuine saturation - an open/shorted
            // sensor pins the ADC essentially at the true rail, not just
            // near the top of the legitimate range.
            current_temperature = NTC_Lookup(raw_adc);
        } else {
            // A disconnected/shorted sensor is a hardware failure the
            // system needs to react to: without this, current_temperature
            // would stay frozen at its last reading, and the PID loop below
            // has no way to distinguish "still heating toward target" from
            // "sensor is gone and I'm flying blind" - the heater would stay
            // on indefinitely if that frozen reading was below target.
            system_error_flag = 1;
        }
    } else {
        // Same reasoning as the soldering iron above: a timed-out poll
        // must raise the fault too, not leave current_temperature frozen
        // with the heater still running on stale data.
        system_error_flag = 1;
    }
    HAL_ADC_Stop(&hadc);

    // Independent safety ceiling for the extruder block
    if (target_temperature > 300) {
        target_temperature = 0;
        system_error_flag = 1;
    }

    if (system_error_flag) {
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
        return;
    }

    static uint8_t hotend_heater_on = 0;
    // Stuck-heater detection - see the identical check in
    // Control_SolderingIron_PID for the full reasoning; same failure mode,
    // same GPIO (T12_PWM_PIN is shared between these two heater tools).
    static uint32_t hotend_pwm_off_since = 0;
    static uint16_t hotend_temp_at_off = 0;
    if (hotend_heater_on) {
        hotend_pwm_off_since = 0;
    } else {
        if (hotend_pwm_off_since == 0) {
            hotend_pwm_off_since = HAL_GetTick();
            hotend_temp_at_off = current_temperature;
        } else if (HAL_GetTick() - hotend_pwm_off_since > 3000 &&
                   current_temperature > hotend_temp_at_off + 5) {
            system_error_flag = 1;
            HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
            return;
        }
    }
    if ((int32_t)current_temperature < (int32_t)target_temperature - 2) {
        hotend_heater_on = 1;
    } else if ((int32_t)current_temperature > (int32_t)target_temperature + 2) {
        hotend_heater_on = 0;
    }
    HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, hotend_heater_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// =============================================================================
// 7. SECONDARY DYNAMIC TELEMETRY AND SENSING SUBSYSTEM
// =============================================================================
void Control_SensorTelemetry(void) {
    if (active_tool != TOOL_VACUUM_PICKUP) return;
    
    // TCRT5000 analog phototransistor reading on Channel 11 (PB0). 2ms
    // timeout, matching the same shared ADC's own soldering-iron read
    // elsewhere in this firmware - a healthy conversion completes in
    // microseconds, so this only ever actually waits the full duration
    // when something's already wrong.
    HAL_ADC_Start(&hadc);
    if(HAL_ADC_PollForConversion(&hadc, 2) == HAL_OK) {
        sensor_analog_reading = HAL_ADC_GetValue(&hadc);
    }
    HAL_ADC_Stop(&hadc);
    
    // LM393 digital input on PB3
    sensor_digital_reading = HAL_GPIO_ReadPin(GPIOB, TCRT_D0_DIG_PIN);
}

// Generic endstop/limit switch, active low. Shares PB3 with the tools
// above, but only makes sense for the 4 that don't already use that pin for
// something else - reconfigured as a plain input for exactly these 4 in main().
void Control_EndstopTelemetry(void) {
    if (active_tool != TOOL_SOLDERING_IRON && active_tool != TOOL_DRILL &&
        active_tool != TOOL_LASER_ENGRAVER && active_tool != TOOL_AOI_INSPECTION) return;

    // Debounced the same way the scan probe's own EXTI handler already is
    // (see HAL_GPIO_EXTI_Callback above) - a mechanical limit switch
    // genuinely bounces, and this is a periodically-polled telemetry
    // value rather than an interrupt, so the equivalent protection here
    // is requiring 2 consecutive matching reads before committing to a
    // new state, rather than a time-based suppression window. This
    // function runs often enough that the extra read adds negligible
    // lag to a telemetry-only value - nothing here gates an actual
    // safety cutoff on this debounce.
    static uint8_t last_raw_reading = 0xFF;  // sentinel - never a valid 0/1 reading, guarantees the first call never matches
    uint8_t raw_reading = (HAL_GPIO_ReadPin(GPIOB, TOUCH_IN_PIN) == GPIO_PIN_RESET) ? 1 : 0;
    if (raw_reading == last_raw_reading) {
        endstop_triggered = raw_reading;
    }
    last_raw_reading = raw_reading;
}

void Telemetry_Drill(void) {
    if(active_tool != TOOL_DRILL) return;
    // RPM conversion assumes 1 FG pulse per revolution and this function's
    // 150ms telemetry window (60000/150=400) - verify against the BL4260's
    // actual FG spec if it turns out to give more than 1 pulse/rev, same
    // as the fan assumptions.
    __disable_irq();
    uint32_t drill_pulses_captured = drill_rpm_pulses;
    drill_rpm_pulses = 0;
    __enable_irq();
    drill_actual_rpm = drill_pulses_captured * 400; 
    // Sanity clamp: a single stray noise-triggered pulse count can produce a
    // physically impossible RPM figure. Beyond protecting the CAN telemetry
    // from reporting nonsense, this also keeps the 5-digit case from ever
    // reaching the OLED, where it would overflow into the tool icon's column.
    if (drill_actual_rpm > 9999) drill_actual_rpm = 9999;
}

void Telemetry_LayerFan(void) {
    if (active_tool != TOOL_3D_PRINTER) return;
    // Assumes standard 4-wire fan convention (2 FG pulses per revolution).
    // 150ms window -> RPM = pulses * (60000/150) / 2 = pulses * 200.
    // If your specific fan gives 1 pulse/revolution, change *200 to *400.
    __disable_irq();
    uint32_t layer_pulses_captured = layer_fan_rpm_pulses;
    layer_fan_rpm_pulses = 0;
    __enable_irq();
    layer_fan_actual_rpm = layer_pulses_captured * 200;
    if (layer_fan_actual_rpm > 9999) layer_fan_actual_rpm = 9999; // see drill_actual_rpm above
}

void Watchdog_Safety_Laser(void) {
    if (active_tool != TOOL_LASER_ENGRAVER) return;
    
    // If power is active but the master doesn't refresh the watchdog within 250ms, full shutdown
    if (laser_power_setpoint > 0 && (HAL_GetTick() - laser_last_kick_tick > 250)) {
        laser_power_setpoint = 0;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
        // Per CANBUS.TXT's documented polarity (PB6 LOW = safe/locked, PB6
        // HIGH = armed), a safety timeout must drive this LOW to lock the
        // interlock, not HIGH - see the POLARITY WARNING on the RX handler
        // above, which applies here too and still needs physical verification.
        HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET); // Lock interlock (safe)
        system_error_flag = 1;
    }
}

// Drill communication watchdog - the laser, layer fan, and hotend fan all
// have one, but a lost-comms drill would have
// spun at whatever speed was last commanded indefinitely). Same 250ms
// threshold and shutdown pattern as the laser above, but also actively
// brakes (PB6 LOW) rather than just cutting PWM - coasting a cutting bit
// down under its own momentum isn't good enough for a safety timeout.
void Watchdog_Safety_Drill(void) {
    if (active_tool != TOOL_DRILL) return;

    if (drill_speed > 0 && (HAL_GetTick() - drill_last_kick_tick > 250)) {
        drill_speed = 0;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // Brake (LOW, per this tool's polarity)
        system_error_flag = 1;
    }
}

// Communication watchdog for a thermal tool - a lost umbilical while a
// heater is actively on would otherwise leave the bang-bang loop happily
// driving toward the last known target_temperature forever. Setting
// target_temperature=0 alongside system_error_flag reuses the existing,
// already-verified shutdown path each control function takes for a
// declared error, rather than duplicating pin-level logic here.
void Watchdog_Safety_SolderIron(void) {
    if (active_tool != TOOL_SOLDERING_IRON) return;

    if (target_temperature > 0 && (HAL_GetTick() - solder_iron_last_kick_tick > 250)) {
        target_temperature = 0;
        system_error_flag = 1;
    }
}

void Watchdog_Safety_HotendHeater(void) {
    if (active_tool != TOOL_3D_PRINTER) return;

    if (target_temperature > 0 && (HAL_GetTick() - hotend_heater_last_kick_tick > 250)) {
        target_temperature = 0;
        system_error_flag = 1;
    }
}

// Layer fan communication watchdog. Replaces the original idea
// of "emergency stop on PB6": that pin is already the extruder's TMC_ENN in this
// same tool mode, and forcing it here would have cut the driver mid-way
// through a print every time the fan dropped to 0%. This is the real,
// conflict-free emergency stop: if the master stops refreshing 0x173
// while the fan is at >0% for 1s, the PWM is shut off
// autonomously (same philosophy as the laser, just without touching PB6).
// Protocol note: as with the laser, the master must resend 0x173
// periodically even if the setpoint doesn't change, to keep this watchdog alive.
void Watchdog_Safety_LayerFan(void) {
    if (active_tool != TOOL_3D_PRINTER) return;

    if (layer_fan_duty > 0 && (HAL_GetTick() - layer_fan_last_kick_tick > 1000)) {
        layer_fan_duty = 0;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
        system_error_flag = 1;
    }
}

void Telemetry_HotendFan(void) {
    if (active_tool != TOOL_3D_PRINTER) return;
    // Same 2-pulses/revolution assumption as the layer fan - adjust if this
    // fan's actual FG spec differs.
    __disable_irq();
    uint32_t hotend_pulses_captured = hotend_fan_rpm_pulses;
    hotend_fan_rpm_pulses = 0;
    __enable_irq();
    hotend_fan_actual_rpm = hotend_pulses_captured * 200;
    if (hotend_fan_actual_rpm > 9999) hotend_fan_actual_rpm = 9999; // see drill_actual_rpm above
}

// Deliberately NOT a comm-loss watchdog like the layer fan's. This fan cools
// the heat break/heatsink, not the printed part - if the master ever goes
// silent, leaving it RUNNING is the safer failure mode (protects against heat
// creep), not shutting it off. So instead this is a STALL detector: if duty
// is commanded >0 but the tachometer reads exactly 0 RPM 3s after starting
// (enough time to spin up), the fan has physically failed, and that's worth
// flagging regardless of what the master is doing.
void Watchdog_Safety_HotendFan(void) {
    if (active_tool != TOOL_3D_PRINTER) return;

    static uint32_t stall_start_tick = 0;

    // Minimum duty before arming: below this, many small DC fans can't
    // physically overcome static friction to start spinning at all - that's
    // not a fault, it's an intentionally very low commanded speed. Arming
    // the stall detector for any nonzero duty would false-trip on exactly
    // that case and abort an otherwise-fine print over a fan that was never
    // asked to actually turn.
    if (hotend_fan_duty > 10) {
        if (hotend_fan_actual_rpm > 0) {
            // Fan is genuinely turning - keep the stall timer reset
            stall_start_tick = HAL_GetTick();
        } else if (HAL_GetTick() - stall_start_tick > 3000) {
            // RPM has read exactly 0 for 3 continuous seconds while duty > 0
            system_error_flag = 1;
        }
    } else {
        stall_start_tick = HAL_GetTick();
    }
}

// =============================================================================
// 8. AUTOMATIC MUTANT IDENTIFICATION (TOOL ECOSYSTEM HARDWARE READOUT)
// =============================================================================
void Identify_PhysicalTool(void) {
    // Read multiple times with a short settling delay between each,
    // requiring 3 consecutive identical readings before accepting one.
    // This runs exactly once, here at boot, never again during operation
    // (active_tool is a fixed, per-board hardware property, not something
    // re-read mid-session) - so this isn't guarding against vibration
    // during a move the way a continuously-read input would need to. What
    // it does guard against: a single noisy or unsettled reading right at
    // power-up latching the wrong tool for the entire session, since nothing
    // else ever gets a chance to correct it before the next power cycle.
    uint8_t id = 0;
    uint8_t stable_count = 0;
    uint8_t last_id = 0xFF; // sentinel - never a valid 5-bit reading, guarantees the first pass never matches
    for (uint8_t attempt = 0; attempt < 10 && stable_count < 3; attempt++) {
        id = 0;
        if (HAL_GPIO_ReadPin(ID0_PORT, ID0_PIN) == GPIO_PIN_RESET) id |= 0x01;
        if (HAL_GPIO_ReadPin(ID1_PORT, ID1_PIN) == GPIO_PIN_RESET) id |= 0x02;
        if (HAL_GPIO_ReadPin(ID2_PORT, ID2_PIN) == GPIO_PIN_RESET) id |= 0x04;
        if (HAL_GPIO_ReadPin(ID3_PORT, ID3_PIN) == GPIO_PIN_RESET) id |= 0x08;
        if (HAL_GPIO_ReadPin(ID4_PORT, ID4_PIN) == GPIO_PIN_RESET) id |= 0x10;
        if (id == last_id) {
            stable_count++;
        } else {
            stable_count = 1;
            last_id = id;
        }
        if (stable_count < 3) {
            HAL_Delay(2);
        }
    }
    // Falls through with whatever the last reading was even if 10 attempts
    // never produced 3 matching in a row (a genuinely floating/unconnected
    // jumper bank could oscillate indefinitely) - the id<=11 validity check
    // right below still applies exactly as before, so an unstable reading
    // most likely just lands on TOOL_INVALID rather than a wrong-but-valid
    // tool, which is the safe direction for this to fail in.
    
    raw_id_pin_value = id; // kept for the id==31 branch just below, which
                            // runs later (once the F-RAM is actually
                            // readable) and needs to know whether this
                            // boot's reading was specifically 0x1F/11111b,
                            // not just whatever active_tool ends up as here.

    if (id <= 11) {
        active_tool = (ToolMode_t)id;
    } else if (id == 31) {
        // 0x1F/11111b - every jumper installed - is the "free
        // configuration" address (see EEPROM.TXT/CANBUS.TXT): rather than
        // a fixed tool, the actual selection lives in the F-RAM's
        // free_tool_selection register, set ahead of time via CAN
        // (0x1A2, see CANBUS.TXT) - normally through the Tester, since
        // the Flasher is the one that can actually write it. Read
        // directly here (not through SavedState_Load(), which hasn't run
        // yet and reads the same F-RAM this same way, but for the wider
        // set of restorable settings) - only this one register matters
        // for this decision, and MX_I2C2_Init_Early() just above made
        // that read possible this early in boot.
        SavedState_t fram_check;
        uint8_t selection = 0; // 0 = no tool selected, same fail-safe default as an invalid reading
        if (FRAM_ReadBytes(SAVEDSTATE_FRAM_ADDR, (uint8_t *)&fram_check, sizeof(fram_check))
            && fram_check.magic == SAVEDSTATE_MAGIC
            && fram_check.struct_version == SAVEDSTATE_VERSION
            && SavedState_Checksum(&fram_check) == fram_check.checksum) {
            selection = fram_check.free_tool_selection;
        }
        if (selection >= 1 && selection <= 12) {
            active_tool = (ToolMode_t)(selection - 1);
        } else {
            active_tool = TOOL_INVALID;
            system_error_flag = 1;
            HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET);   // steppers disabled
            HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // drill brake engaged
            HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET); // laser interlock locked
            HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET); // heater off
        }
    } else {
        active_tool = TOOL_INVALID;
        system_error_flag = 1;
        // Explicit, redundant safety: force every actuator output to its
        // inert state right now, rather than trusting that no other code
        // path will ever touch them for a tool ID nothing else recognizes.
        HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET);   // steppers disabled
        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // drill brake engaged
        HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET); // laser interlock locked
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET); // heater off
    }
}

// =============================================================================
// 9. CAN BUS RX CALLBACK (CRITICAL COMMAND PROCESSING)
// =============================================================================
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_m) {
    if (HAL_CAN_GetRxMessage(hcan_m, CAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK) {
        
        can_led_tick = HAL_GetTick();

        // CAN bootloader entry (0x7F0) - checked first, before even the
        // system_error_flag gate below, since being able to trigger an
        // update is exactly what you'd want if a bug were causing
        // persistent false errors and physical JTAG access isn't
        // convenient. Requires a specific magic payload, not just the ID
        // alone, so a corrupted/malformed frame can't accidentally trigger
        // a reset into update mode.
        if (rxHeader.StdId == 0x7F0 && rxHeader.DLC == 4 &&
            rxData[0] == 0xB0 && rxData[1] == 0x07 && rxData[2] == 0x1D && rxData[3] == 0x5A) {
            // Explicit, inline shutdown of every actuator - not deferred to
            // system_error_flag's usual next-loop-iteration handling, since
            // there won't BE a next iteration once NVIC_SystemReset() runs.
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
            HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET);   // stepper tools: HIGH disables
            HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // drill: LOW brakes, own pin now
            HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET); // heater off
            // TMC_ENN_PIN and LASER_SAFETY_PIN are the same physical pin
            // (PB6) - writing both unconditionally would have this second
            // write undo the SET above, leaving a stepper driver
            // re-enabled instead of disabled if that's what's actually
            // attached (the two functions need opposite polarities for
            // "safe", so there's no single value safe for both). The SET
            // above already covers every stepper tool correctly; only the
            // laser case needs to override it, matching
            // MX_GPIO_Post_Init's own handling of this same shared pin.
            if (active_tool == TOOL_LASER_ENGRAVER) {
                HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET); // laser interlock: LOW locked
            }
            // A plain cycle-counted wait, not HAL_Delay(): HAL_Delay()
            // depends on SysTick advancing the millisecond tick, but
            // SysTick's priority is numerically lower than this CAN ISR's
            // (1), so it can't preempt an already-running ISR - HAL_Delay()
            // here would spin forever waiting for a tick that can't arrive.
            for (volatile uint32_t settle = 0; settle < 320000; settle++); // ~5ms @ 64MHz
            NVIC_SystemReset();
            // never reached
        }

        // Version query (0x7F8) - read-only, no magic payload needed since
        // nothing here can cause harm. Responds via 0x7F9: byte0=0 marks
        // this as the application answering directly (as opposed to the
        // bootloader answering from stored metadata, byte0=1 - see
        // BOOTLOADER.C), then HardwareID (4 bytes) + version major
        // (2 bytes) + version minor (1 byte), all big-endian.
        if (rxHeader.StdId == 0x7F8) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[8];
                txD[0] = 0x00; // answering as the running application
                txD[1] = (uint8_t)(THIS_HARDWARE_ID >> 24);
                txD[2] = (uint8_t)(THIS_HARDWARE_ID >> 16);
                txD[3] = (uint8_t)(THIS_HARDWARE_ID >> 8);
                txD[4] = (uint8_t)(THIS_HARDWARE_ID);
                txD[5] = (uint8_t)(FIRMWARE_VERSION_MAJOR >> 8);
                txD[6] = (uint8_t)(FIRMWARE_VERSION_MAJOR);
                txD[7] = (uint8_t)(FIRMWARE_VERSION_MINOR);
                txH.StdId = 0x7F9;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 8;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Active tool + quick state query (0x110) - read-only, no side
        // effects, same reasoning as 0x7F8 above: a host has no other way
        // to learn which of the 12 tool profiles this board is jumpered
        // for without this, short of already knowing which tool-specific
        // command/telemetry IDs get a response (which itself takes time
        // and doesn't distinguish "no response yet" from "wrong tool").
        // Answered even during a declared error, unlike most commands
        // below - this is exactly the kind of read-only diagnostic that's
        // most useful precisely when something's gone wrong.
        if (rxHeader.StdId == 0x110) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[4];
                txD[0] = (uint8_t)active_tool; // 0-11 = a real tool head, 12+ = no tool assigned to this jumper setting
                txD[1] = system_error_flag ? 0x01 : 0x00;
                txD[2] = can_bus_error_flag ? 0x01 : 0x00;
                txD[3] = boot_sequence_active ? 0x01 : 0x00;
                txH.StdId = 0x111;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 4;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Recovered persisted-state query (0x190/0x191) - read-only, answers
        // with whatever was loaded from the FM24CL64B at boot (see
        // SavedState_Load()). Deliberately does NOT re-apply anything
        // hazardous itself - this just reports it. A compact 8-byte
        // summary rather than the full ~26-byte saved struct, since a
        // single CAN frame can't carry that much - covers what a host
        // most likely needs to decide what to do next; the full record
        // stays in recovered_state in RAM if a future need justifies a
        // multi-frame dump. Positioned here, before the error gate below,
        // to actually match its own documented behavior (CANBUS.TXT) -
        // read-only diagnostics stay available exactly when something's
        // already gone wrong, same reasoning as 0x110 just above.
        if (rxHeader.StdId == 0x190) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[8] = {0};
                txD[0] = had_valid_recovered_state;
                txD[1] = recovered_state.active_tool_at_save;
                txD[2] = recovered_state.had_critical_error;
                uint16_t temp_setpoint = recovered_state.solder_setpoint
                    ? recovered_state.solder_setpoint : recovered_state.printer_nozzle_setpoint;
                txD[3] = (uint8_t)(temp_setpoint >> 8);
                txD[4] = (uint8_t)(temp_setpoint & 0xFF);
                txD[5] = recovered_state.drill_speed
                    ? recovered_state.drill_speed : recovered_state.laser_power;
                txD[6] = recovered_state.drill_direction || recovered_state.laser_interlock;
                txD[7] = recovered_state.layer_fan_power;
                txH.StdId = 0x191;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 8;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Set/query expansion board type (0x1A0/0x1A1) - which of the 5
        // possible configurations (see EXPANSION.TXT) is physically
        // installed on CONN_EXPANSION. There's no electrical way to
        // sense this - the board has to be told, once, and this value
        // then persists across power cycles the same way any other saved
        // setting does (via the normal SavedState_MaybeSave()
        // change-detection on the next main loop pass, not forced
        // immediately here). Positioned here, before the error gate
        // below, for the same reason as 0x190 just above - this is
        // configuration/diagnostic, not an actuation command, so there's
        // no reason to gate it behind a fault clearing.
        if (rxHeader.StdId == 0x1A0 && rxHeader.DLC >= 1 && rxData[0] <= 4) {
            expansion_board_type = rxData[0];
        }
        if (rxHeader.StdId == 0x1A0 || rxHeader.StdId == 0x1A1) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[1] = {expansion_board_type};
                txH.StdId = 0x1A1;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 1;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Set/query free tool configuration (0x1A2/0x1A3) - the register
        // Identify_PhysicalTool() consults when the ID-jumper reading is
        // 0x1F/11111b (see that function's own comment, and EEPROM.TXT).
        // 0=no tool selected, 1-12=one of the 12 currently supported tool
        // profiles (stored as id+1 - see the SavedState_t field's own
        // comment for why). Same positioning/persistence reasoning as
        // 0x1A0 just above: configuration, not actuation, so it isn't
        // gated behind the error block below, and persists via the
        // normal SavedState_MaybeSave() path rather than a forced
        // immediate write. Response carries the raw ID-jumper reading
        // (0-31) alongside the current selection, not just the
        // selection alone - lets a reader (the Tester) tell "jumpers
        // read something other than 0x1F" apart from "jumpers read
        // 0x1F but nothing's configured yet", which a bare selection
        // value on its own couldn't distinguish.
        if (rxHeader.StdId == 0x1A2 && rxHeader.DLC >= 1 && rxData[0] <= 12) {
            free_tool_selection = rxData[0];
        }
        if (rxHeader.StdId == 0x1A2 || rxHeader.StdId == 0x1A3) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[2] = {raw_id_pin_value, free_tool_selection};
                txH.StdId = 0x1A3;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 2;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Set/query peripheral type + device serial number (0x1A4/0x1A5) -
        // see EEPROM.TXT section 6. Two very different kinds of value in
        // one response, same reasoning as 0x1A3 pairing the ID-jumper
        // reading with free_tool_selection above: URTC_PERIPHERAL_TYPE is
        // a fixed firmware-identity constant (0x03, never written - 0x1A4
        // only ever updates the serial byte below it, this firmware has
        // no code path that could change its own type at runtime), while
        // device_serial_number is a real F-RAM-backed, host-assigned
        // label purely for telling multiple otherwise-identical URTC
        // boards apart on the same CAN bus.
        if (rxHeader.StdId == 0x1A4 && rxHeader.DLC >= 1) {
            device_serial_number = rxData[0];
        }
        if (rxHeader.StdId == 0x1A4 || rxHeader.StdId == 0x1A5) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[2] = {URTC_PERIPHERAL_TYPE, device_serial_number};
                txH.StdId = 0x1A5;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 2;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Blocks every incoming command except 0x100 (lighting) while a
        // critical error is declared - movement/power commands must not be
        // processed mid-fault, which would defeat the point of raising the
        // flag. 0x100 stays open because it touches nothing
        // actuation-relevant (just LED color/OLED mode), letting the master
        // command a distinct warning pattern on the LEDs to signal the
        // fault externally, rather than the board going dark on the
        // outside the moment it declares itself unsafe.
        if (system_error_flag && rxHeader.StdId != 0x100) {
            return;
        }

        // Expansion connector SPI passthrough (0x180/0x181) - generic byte
        // transport for whatever ends up on CONN_EXPANSION's SPI bus (a
        // TMC5160, or any other SPI-configurable chip), rather than this
        // firmware needing to know that chip's specific register protocol.
        // byte[0] = N (1-7, how many of the remaining bytes to clock),
        // byte[1..N] = the bytes to send. Toggles EXP_SPI_CS_PIN low for
        // the whole transfer and back high after, per the TMC5160
        // datasheet's own requirement that CS stay low for the complete
        // transaction. Answers with the same N bytes, replaced with
        // whatever came back on MISO during that same transfer (SPI is
        // inherently full-duplex - every byte sent is also a byte
        // received, even if the caller only cares about one direction for
        // a given transaction).
        if (rxHeader.StdId == 0x180 && rxHeader.DLC >= 2) {
            uint8_t n = rxData[0];
            if (n > 7) n = 7;
            if (n > (rxHeader.DLC - 1)) n = rxHeader.DLC - 1;
            uint8_t response[8];
            response[0] = n;
            HAL_GPIO_WritePin(EXP_SPI_CS_PORT, EXP_SPI_CS_PIN, GPIO_PIN_RESET);
            for (uint8_t i = 0; i < n; i++) {
                response[i + 1] = ExpansionSPI_TransferByte(rxData[i + 1]);
            }
            HAL_GPIO_WritePin(EXP_SPI_CS_PORT, EXP_SPI_CS_PIN, GPIO_PIN_SET);
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                txH.StdId = 0x181;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = n + 1;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, response, &mb);
            }
        }

        // TMC_DIAG0 level query (0x182/0x183) - simple, read-only,
        // polled rather than interrupt-driven for now. A real hardware
        // EXTI on this pin (matching the scan probe's own pattern on
        // PB3) is a natural future addition once real-world latency
        // requirements are clear - not built speculatively ahead of
        // that. Reads back the raw pin level, no inversion - this line
        // is active-HIGH (see TMC_DIAG0_PIN's own comment for why:
        // diode-OR combining the onboard TMC2209's own DIAG with
        // whatever's on the expansion connector), so txD[0]=1 means a
        // stall/error condition on either driver, txD[0]=0 means both
        // clear.
        if (rxHeader.StdId == 0x182) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[1];
                txD[0] = (HAL_GPIO_ReadPin(TMC_DIAG0_PORT, TMC_DIAG0_PIN) == GPIO_PIN_SET) ? 1 : 0;
                txH.StdId = 0x183;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 1;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Erase recovered persisted state (0x192) - a magic 4-byte payload is
        // required, same reasoning as 0x7F0's own magic trigger: a
        // corrupted or malformed frame landing on this ID by chance
        // shouldn't be able to wipe a real saved record. Unlike 0x110/
        // 0x190 above, this is gated by the normal error-block (it's
        // above, so a declared critical error already returned before
        // reaching here) - erasing isn't itself physically hazardous the
        // way a motor/heater/laser command would be, but there's no
        // strong reason to except it either, so it stays consistent with
        // "most things wait until the fault clears".
        if (rxHeader.StdId == 0x192 && rxHeader.DLC == 4 &&
            rxData[0] == 0xE3 && rxData[1] == 0xA5 && rxData[2] == 0xE0 && rxData[3] == 0xFF) {
            uint8_t blank[sizeof(SavedState_t)];
            memset(blank, 0xFF, sizeof(blank)); // an arbitrary but definite pattern (no special hardware meaning for F-RAM the way it has for Flash/EEPROM - chosen purely so the magic/checksum check below is guaranteed to fail on the next boot)
            FRAM_WriteBytes(SAVEDSTATE_FRAM_ADDR, blank, sizeof(blank));
            had_valid_recovered_state = 0;
            memset(&recovered_state, 0, sizeof(recovered_state));
            memset(&last_saved_state, 0xFF, sizeof(last_saved_state)); // forces the next SavedState_MaybeSave() comparison to see a real difference and write a fresh record rather than assuming nothing changed
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[8] = {0}; // had_valid_recovered_state=0, everything else zeroed - confirms the erase to whatever's listening for 0x191
                txH.StdId = 0x191;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 8;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Global synchronous command 0x100: Lighting and OLED screen.
        // DLC 8 carries both the status LED color (bytes 0-2) and the ring
        // LED color (bytes 4-6, see below) in one frame.
        if (rxHeader.StdId == 0x100 && rxHeader.DLC == 8) {
            led_state_pixel.R = rxData[0];
            led_state_pixel.G = rxData[1];
            led_state_pixel.B = rxData[2];
            // Host explicitly asked for this color - hold it for a while
            // instead of letting the automatic scheme (see
            // Update_StatusLED_AutoColor) silently overwrite it on the very
            // next loop iteration. Every fresh 0x100 command extends this
            // window, so a host that updates the color periodically can
            // keep it held indefinitely; a host that sends it once gets it
            // displayed for a solid 10s before this reverts to automatic.
            led_host_override_active = 1;
            led_host_override_expire_tick = HAL_GetTick() + LED_HOST_OVERRIDE_DURATION_MS;
            oled_night_mode_cmd = rxData[3];
            oled_mode_pending_flag = 1;

            ring_color_r = rxData[4];
            ring_color_g = rxData[5];
            ring_color_b = rxData[6];
            uint8_t ring_on = rxData[7]; // 0x00 = off, 0x01 = on

            // AOI drives the ring itself via 0x150 (off/strobe/continuous) -
            // this command still keeps the stored color current for it (so
            // a color change takes effect immediately in continuous mode),
            // but doesn't directly toggle the ring on/off while AOI owns it.
            if (active_tool != TOOL_AOI_INSPECTION) {
                for (int i = 0; i < 8; i++) {
                    ring_pixels[i].R = ring_on ? ring_color_r : 0;
                    ring_pixels[i].G = ring_on ? ring_color_g : 0;
                    ring_pixels[i].B = ring_on ? ring_color_b : 0;
                }
            } else if (aoi_mode == 0x02) {
                // Atomic capture: reading these three volatiles individually,
                // 8 times each inside the loop below, left a window where an
                // interrupt updating them mid-loop could give different
                // pixels in the same ring a momentary mismatched color.
                // Cosmetic at worst, but free to close.
                __disable_irq();
                uint8_t cr = ring_color_r, cg = ring_color_g, cb = ring_color_b;
                __enable_irq();
                for (int i = 0; i < 8; i++) {
                    ring_pixels[i].R = cr;
                    ring_pixels[i].G = cg;
                    ring_pixels[i].B = cb;
                }
            }
            update_led_ring_flag = 1;
            return;
        }

        // Blocks every incoming command except 0x100 (lighting) while a
        // critical error is active - nothing here is safe to let through
        // otherwise. A master that didn't notice the fault (or was itself
        // the cause of it) could keep driving motors or firing the laser
        // while the OLED reads SYSTEM BLOCKED. Lighting/night-mode above
        // still works since neither is hazardous either way.
        if (system_error_flag) {
            return;
        }

        switch(active_tool) {
            case TOOL_SOLDERING_IRON:
                if (rxHeader.StdId == 0x130 && rxHeader.DLC >= 2) {
                    // Big-endian reconstruction (MSB | LSB)
                    target_temperature = (rxData[0] << 8) | rxData[1];
                    solder_iron_last_kick_tick = HAL_GetTick();
                }
                break;

            case TOOL_PASTE_DISPENSER:
            case TOOL_LIQUID_DISPENSER:
            case TOOL_SCREWDRIVER:
            case TOOL_GRIPPER_GIMBAL:
            case TOOL_GRIPPER_NEMA:
                if (rxHeader.StdId == 0x120 && rxHeader.DLC >= 5 && !boot_sequence_active) {
                    __disable_irq();
                    // Clears any steps left over from the previous command
                    // before a direction change takes effect - not a
                    // physical pause before reversing (the DIR pin and the
                    // new step count both change together, in this same
                    // critical section, with no STEP pulse generated
                    // between them, so the actual motion is an instant
                    // reversal, not stop-then-reverse). What this prevents
                    // is stale steps queued under the old direction
                    // running under the new one instead - without this,
                    // a command that both reverses direction and asks for
                    // fewer steps than were still queued would execute
                    // some of that leftover count in the wrong direction.
                    GPIO_PinState new_dir = (rxData[0] == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET;
                    if (HAL_GPIO_ReadPin(DIR_PORT, DIR_PIN) != new_dir) {
                        steps_remaining = 0;
                    }
                    HAL_GPIO_WritePin(DIR_PORT, DIR_PIN, new_dir);
                    
                    // 32-bit motor steps, big-endian (MSB to LSB)
                    uint32_t steps = ((uint32_t)rxData[1] << 24) | 
                                     ((uint32_t)rxData[2] << 16) | 
                                     ((uint32_t)rxData[3] << 8)  | 
                                     rxData[4];
                    if (steps > 0) {
                        total_steps_setpoint = steps;
                        steps_remaining = steps;
                        // Fresh move: force the step ISR to start with a
                        // rising edge. Left at 1 from the tail end of a
                        // previous move, the first tick of this new move
                        // would perform a falling edge instead, misaligning
                        // the very first step.
                        step_pulse_high = 0;
                        HAL_GPIO_WritePin(STEP_PORT, STEP_PIN, GPIO_PIN_RESET); // force the physical pin low too, not just the flag above
                        HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_RESET); // Enable TMC2209
                    } else {
                        // steps==0 is a stop request - the direction-change
                        // branch above already clears steps_remaining when
                        // direction flips, but a stop sent with the same
                        // direction as an already-in-progress move needs
                        // this too, or that move would otherwise run to
                        // completion unaffected by a command that asked
                        // for zero steps.
                        steps_remaining = 0;
                    }
                    __enable_irq();
                }
                break;

            case TOOL_DRILL:
                if (rxHeader.StdId == 0x140 && rxHeader.DLC >= 2 && !boot_sequence_active) {
                    drill_speed = rxData[0];
                    drill_last_kick_tick = HAL_GetTick();
                    HAL_GPIO_WritePin(DRILL_FRIN_PORT, DRILL_FRIN_PIN, (rxData[1] == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                    
                    // drill_speed is 0-255 (full byte) per CANBUS.TXT,
                    // not a 0-100 percentage. Scaled the same way as the laser to cover the full range.
                    uint32_t duty = (drill_speed * 3199) / 255; // scaled for TIM1's 20kHz period
                    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty);
                    
                    if (drill_speed > 0) {
                        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_SET); // Release brake
                    } else {
                        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // Brake
                    }
                }
                break;

            case TOOL_AOI_INSPECTION:
                if (rxHeader.StdId == 0x150 && rxHeader.DLC >= 3) {
                    aoi_mode = rxData[0];
                    // Strobe period, big-endian
                    aoi_strobe_period = (rxData[1] << 8) | rxData[2];
                    if (aoi_mode == 0x01) {
                        // Strobe: handled in the main loop's pulse trigger
                        trigger_aoi_strobe_flag = 1;
                    } else if (aoi_mode == 0x02) {
                        // Continuous: ring solid on at the stored color.
                        // Same atomic-capture reasoning as the 0x150 handler.
                        __disable_irq();
                        uint8_t cr2 = ring_color_r, cg2 = ring_color_g, cb2 = ring_color_b;
                        __enable_irq();
                        for (int i = 0; i < 8; i++) {
                            ring_pixels[i].R = cr2;
                            ring_pixels[i].G = cg2;
                            ring_pixels[i].B = cb2;
                        }
                        update_led_ring_flag = 1;
                    } else {
                        // Off
                        for (int i = 0; i < 8; i++) {
                            ring_pixels[i].R = 0;
                            ring_pixels[i].G = 0;
                            ring_pixels[i].B = 0;
                        }
                        update_led_ring_flag = 1;
                    }
                }
                break;

            case TOOL_LASER_ENGRAVER:
                if (rxHeader.StdId == 0x160 && rxHeader.DLC >= 2 && !boot_sequence_active) {
                    laser_power_setpoint = rxData[0];
                    laser_last_kick_tick = HAL_GetTick();

                    // Duty forced to 0 unless the interlock is explicitly
                    // armed (rxData[1]==0x01) - a software floor beneath the
                    // physical interlock pin below, not a replacement for it.
                    // Without this, the PWM itself was generated purely from
                    // the power setpoint, with zero dependency on the
                    // interlock state - a host sending a nonzero power byte
                    // while leaving the interlock at 0x00 (locked) would
                    // still get real PWM out of TIM1, relying entirely on
                    // the external interlock circuit to physically cut the
                    // beam. This way, an interlock left unarmed - or a
                    // stray/malformed frame with rxData[1] anything other
                    // than exactly 0x01 - can't produce laser PWM even if
                    // that external circuit is slow, miswired, or absent.
                    uint32_t laser_duty = (rxData[1] == 0x01)
                        ? (laser_power_setpoint * 3199) / 255 // scaled for TIM1's 20kHz period
                        : 0;
                    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, laser_duty);
                    
                    // rxData[1] is the master's explicit interlock/safety
                    // command, independent of the power setpoint above -
                    // follows CANBUS.TXT literally: 0x01=armed=PB6 HIGH,
                    // 0x00=safe/locked=PB6 LOW.
                    // POLARITY WARNING: this pin is physically shared with
                    // TMC_ENN (active-low ENABLE on the stepper driver, in
                    // other tool modes). If the laser's actual interlock relay
                    // was wired assuming that same active-low-enable sense,
                    // PB6 LOW here would mean "enabled" - the OPPOSITE of what
                    // CANBUS.TXT documents (PB6 LOW = safe/locked). Verify
                    // against the real interlock circuit before relying on
                    // this with the laser powered - getting this backwards
                    // means the laser could fire when you expect it locked.
                    HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN,
                        (rxData[1] == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                }
                break;

            case TOOL_3D_PRINTER:
                if (rxHeader.StdId == 0x170 && rxHeader.DLC >= 6) {
                    // Hotend temperature, big-endian (MSB | LSB)
                    // Not gated on boot_sequence_active - starting the heater
                    // pre-warming during the splash is fine (no physical
                    // motion involved); only the extruder steps below need
                    // to wait for the splash to finish.
                    target_temperature = (rxData[0] << 8) | rxData[1];
                    hotend_heater_last_kick_tick = HAL_GetTick();
                    if (!boot_sequence_active) {
                    __disable_irq();
                    // Clear residual queued steps before a direction change - same reasoning as the 0x120 handler above
                    GPIO_PinState new_dir = (rxData[2] == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET;
                    if (HAL_GPIO_ReadPin(DIR_PORT, DIR_PIN) != new_dir) {
                        steps_remaining = 0;
                    }
                    HAL_GPIO_WritePin(DIR_PORT, DIR_PIN, new_dir);
                    
                    // Extruder steps, big-endian (Bytes 3, 4, and 5)
                    uint32_t extruder_steps = ((uint32_t)rxData[3] << 16) | 
                                         ((uint32_t)rxData[4] << 8)  | 
                                         rxData[5];
                    if (extruder_steps > 0) {
                        total_steps_setpoint = extruder_steps;
                        steps_remaining = extruder_steps;
                        step_pulse_high = 0; // see the 0x120 handler above
                        HAL_GPIO_WritePin(STEP_PORT, STEP_PIN, GPIO_PIN_RESET); // physical pin low too, not just the flag
                        HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_RESET);
                    } else {
                        steps_remaining = 0; // stop request - see the 0x120 handler above
                    }
                    __enable_irq();
                    }
                }
                // Layer fan (0x173): duty 0-255 towards TIM1_CH1/PA8 at 25kHz,
                // sharing the same channel as drill/laser (reconfigured to
                // this tool's exact rate in main()) - independent from TIM3.
                else if (rxHeader.StdId == 0x173 && rxHeader.DLC >= 1) {
                    layer_fan_duty = rxData[0];
                    uint32_t fan_duty = (layer_fan_duty * 2559) / 255;
                    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, fan_duty);
                    layer_fan_last_kick_tick = HAL_GetTick();
                }
                // Hotend fan (0x178): duty 0-255 towards TIM2_CH1/PA5 at
                // 25kHz. Separate from the layer fan - cools the heatsink/heat
                // break, not the printed part.
                else if (rxHeader.StdId == 0x178 && rxHeader.DLC >= 1) {
                    hotend_fan_duty = rxData[0];
                    uint32_t hfan_duty = (hotend_fan_duty * 2559) / 255;
                    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, hfan_duty);
                    hotend_fan_last_kick_tick = HAL_GetTick();
                }
                break;
                
            default:
                break;
        }
    }
}

// =============================================================================
// 10. ASSOCIATED INTERRUPTS AND VECTOR HANDLERS (ISR)
// =============================================================================
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == TOUCH_IN_PIN) {
        if (active_tool == TOOL_SCAN_PROBE) {
            // Debounces the probe: a mechanical contact genuinely bounces
            // (unlike the tachometer inputs elsewhere, which are about
            // electrical noise) - 30-40 bounces within a millisecond would
            // each fire this whole handler, flooding the bus with duplicate
            // critical-priority messages. This doesn't delay the genuine
            // first impact (it always passes immediately, since nothing
            // recent precedes it) - it only suppresses the bounce-induced
            // duplicates that follow within 2ms.
            static uint32_t last_probe_tick = 0;
            uint32_t now_probe = HAL_GetTick();
            if (now_probe - last_probe_tick < 2) {
                return;
            }
            last_probe_tick = now_probe;
            // Collision frame with critical-priority ID (0x095), injected immediately
            CAN_TxHeaderTypeDef txHead;
            uint8_t txDataLocal[1] = {0x01};
            uint32_t mailbox;
            
            txHead.StdId = 0x095;
            txHead.RTR = CAN_RTR_DATA;
            txHead.IDE = CAN_ID_STD;
            txHead.DLC = 1;
            txHead.TransmitGlobalTime = DISABLE;

            // This is the one message documented as never allowed to fail - if
            // all 3 hardware mailboxes are occupied, clear them out rather than
            // risk AddTxMessage silently dropping the impact report.
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) {
                HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX0);
                HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX1);
                HAL_CAN_AbortTxRequest(&hcan, CAN_TX_MAILBOX2);
                // HAL_CAN_AbortTxRequest only sets the hardware's abort-request
                // bit and returns immediately - it doesn't wait for the
                // hardware to actually finish releasing the mailbox, so a
                // short poll here (not a fixed delay) confirms it's really
                // free before trusting the send below to succeed. Bounded
                // iteration count, not a real-time wait, so this can't hang.
                for (uint16_t wait = 0; wait < 1000 && HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0; wait++);
            }

            HAL_CAN_AddTxMessage(&hcan, &txHead, txDataLocal, &mailbox);
            probe_impact_counter++; // to show it live on the OLED
        }
        else if (active_tool == TOOL_DRILL) {
            // BL4260 motor tachometer pulse count. Debounced the same way as
            // the hotend fan (2ms minimum spacing) - a good general defense
            // for any tachometer input even without a specific known noise
            // source, and it's free.
            static uint32_t last_drill_pulse_tick = 0;
            uint32_t now_drill = HAL_GetTick();
            if (now_drill - last_drill_pulse_tick >= 2) {
                drill_rpm_pulses++;
                last_drill_pulse_tick = now_drill;
            }
        }
        else if (active_tool == TOOL_3D_PRINTER) {
            // Layer fan FG pulse count (same PA3 pin, same EXTI3). Same 2ms
            // debounce as above.
            static uint32_t last_layerfan_pulse_tick = 0;
            uint32_t now_layerfan = HAL_GetTick();
            if (now_layerfan - last_layerfan_pulse_tick >= 2) {
                layer_fan_rpm_pulses++;
                last_layerfan_pulse_tick = now_layerfan;
            }
        }
    }
    else if (GPIO_Pin == HOTEND_FAN_FG_PIN) {
        // Hotend fan FG pulse count (PA6, EXTI6 - its own line, no sharing/routing
        // needed unlike EXTI3). Debounced: PA5 (this fan's own PWM output) sits
        // on the adjacent pin and switches at 25kHz - capacitive crosstalk
        // between closely-routed traces on this package could inject noise
        // edges into PA6 that look like real tachometer pulses, which would
        // mask a genuine stall right when the stall detector needs to catch
        // it. 2ms is a clean separation: even an unrealistically fast small
        // fan (15,000+ RPM at 2 pulses/rev) doesn't produce genuine pulses
        // faster than that, while 25kHz noise (40us period) is two orders of
        // magnitude quicker and gets rejected.
        static uint32_t last_hotend_fan_pulse_tick = 0;
        if (active_tool == TOOL_3D_PRINTER) {
            uint32_t now = HAL_GetTick();
            if (now - last_hotend_fan_pulse_tick >= 2) {
                hotend_fan_rpm_pulses++;
                last_hotend_fan_pulse_tick = now;
            }
        }
    }
}

// Overrides the startup file's default weak HardFault_Handler (an empty
// infinite loop). If a memory fault or illegal instruction ever lands here,
// none of the PWM timers or software watchdogs get a chance to react on
// their own - whatever was last commanded (heater, laser, motor) would
// otherwise stay latched on indefinitely. First action: force every pin on
// GPIOA and GPIOB low, physically cutting every actuator this board drives,
// before anything else - including before any debug/reset attempt.
void HardFault_Handler(void) {
    // BRR only affects pins currently in GPIO output mode - a pin in
    // Alternate Function mode is driven by that peripheral's own output
    // logic instead, completely bypassing ODR/BSRR/BRR. PA8 (TIM1_CH1 -
    // the laser/drill/layer-fan PWM) is exactly such a pin, so writing to
    // BRR alone wouldn't touch it - a HardFault while the 10W laser was
    // firing would leave it firing indefinitely. Forcing MODER to
    // all-analog (0b11 per pin) electrically disconnects every pin from
    // both the GPIO driver and any AF peripheral - the only state that's
    // safe regardless of whatever mode each pin was in at the moment of
    // the fault.
    // EXCEPTION: PA13/PA14 (SWDIO/SWCLK) are deliberately spared and forced
    // to AF mode (0b10) instead of analog - blanking Port A entirely would
    // cut the debugger connection at exactly the moment someone would most
    // want to read the fault status registers and call stack to find out
    // why this handler was even entered.
    GPIOA->MODER = 0xEBFFFFFF; // all Port A analog except PA13/PA14 -> AF
    GPIOB->MODER = 0xFFFFFFFF;
    while (1) { }
}

// Required for HAL_SPI_Transmit_DMA's completion callback to ever fire -
// without this, HAL_SPI_GetState never transitions back to READY after the
// first transfer, and Update_AllLEDs_SPI_DMA's busy-check would silently
// skip every subsequent call forever.
void DMA1_Channel3_IRQHandler(void) {
    HAL_DMA_IRQHandler(&hdma_spi1_tx);
}

// Fires on any CAN error (including bus-off) and sets a flag the OLED can
// show - AutoBusOff recovery already handles the electrical side, this
// just makes sure a fault is visible to whoever's standing at the machine
// instead of it going silent with no explanation.
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan_e) {
    can_bus_error_flag = 1;
}

// Advances the millisecond tick HAL_GetTick() reads. Without this handler
// calling HAL_IncTick(), HAL_GetTick() would always return 0 and every
// HAL_Delay() call in this file would spin forever, since its exit
// condition compares HAL_GetTick() against a start value that's also 0.
void SysTick_Handler(void) {
    HAL_IncTick();
}

void CAN_RX0_IRQHandler(void) {
    HAL_CAN_IRQHandler(&hcan);
}

// Drives the stepper motors: every 1ms it toggles STEP once, so a full
// pulse (high+low edge, what nearly every TMC/generic driver counts as
// "one step") takes 2 ticks = 2ms -> max ~500 steps/second. Enough for
// dispensers/screwdriver/grippers; if the 3D extruder needs a higher
// print speed, this is the place to speed up (shorter TIM3 period).
void TIM3_IRQHandler(void) {
    HAL_TIM_IRQHandler(&htim3);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance != TIM3) return;

    // Blocking new CAN commands during system_error_flag alone does
    // nothing to stop a motion already in progress. A long extrusion or
    // dispense move started before a thermal fault or fan stall tripped
    // the flag would keep running to completion - potentially tens of
    // seconds of a motor driven blind while the OLED already reads SYSTEM
    // BLOCKED. An emergency stop needs to halt physical motion the instant
    // the fault is detected, not just refuse new instructions.
    if (system_error_flag) {
        steps_remaining = 0;
        HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET); // stepper tools disabled
        // Cutting stepper current does nothing for the laser/drill PWM
        // channel or the two fan PWMs - force every PWM output to 0 too.
        // Guarded: htim1/htim2 are only initialized for certain tools (see
        // main()) - their .Instance is NULL otherwise.
        if (htim1.Instance != NULL) {
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0); // drill/laser/layer-fan PWM
        }
        // The hotend fan is a cooling fan, not an actuator that can cause
        // harm by continuing to run - left running (htim2 intentionally not
        // zeroed) so it can keep dissipating residual heat.
        if (active_tool == TOOL_LASER_ENGRAVER) {
            HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET); // interlock locked
        }
        // PWM=0 alone doesn't stop a spinning drill bit - it coasts freely.
        // DRILL_ENN's polarity is LOW=brake, HIGH=spin. Its own pin now, so
        // this can't interact with the stepper ENN write above regardless
        // of ordering.
        if (active_tool == TOOL_DRILL) {
            HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET);
        }
        return;
    }

    // Explicitly tracks which half of the pulse we're in, rather than a
    // plain TogglePin: stepper drivers (TMC2209 included) only count a
    // step on the rising edge, so steps_remaining decrements once per real
    // step (matching the OLED display and the CAN protocol) - still 2
    // ticks (2ms) per real step, ~500 steps/sec ceiling.

    if (steps_remaining > 0) {
        if (!step_pulse_high) {
            HAL_GPIO_WritePin(GPIOB, STEP_PIN, GPIO_PIN_SET); // rising edge = the actual step
            step_pulse_high = 1;
        } else {
            HAL_GPIO_WritePin(GPIOB, STEP_PIN, GPIO_PIN_RESET);
            step_pulse_high = 0;
            // CONCURRENCY FIX (separate from the one below): steps_remaining--
            // is a non-atomic read-modify-write. The higher-priority CAN
            // interrupt can preempt right in the middle of it with a fresh
            // move command (a new nonzero steps_remaining), and this ISR
            // resuming afterward would overwrite that fresh value with its
            // own stale decremented one - silently erasing the new command.
            __disable_irq();
            steps_remaining--;
            __enable_irq();
            if (steps_remaining == 0) {
                // A higher-priority CAN interrupt can
                // preempt this ISR right here, process a new move command
                // (fresh nonzero steps_remaining, driver re-enabled), then
                // return control back to this exact point. Without a fresh
                // re-check, the code below would still see the earlier
                // (now stale) "just reached zero" result and disable the
                // driver it was just re-enabled for - the new move would
                // count down doing nothing, since no current reaches the
                // motor. Re-reading right before acting catches that.
                if (steps_remaining != 0) {
                    return;
                }
                // Cutting power unconditionally here would release
                // holding torque even for tools that need to keep gripping or
                // resisting backpressure after the move finishes - the NEMA
                // and gimbal grippers would relax their jaws and could drop
                // whatever they just grasped, and the 3D printer's extruder
                // would let molten filament pressure push the gear backward
                // between moves. Only cut power for the tools where holding
                // torque genuinely isn't needed once idle.
                if (active_tool != TOOL_GRIPPER_NEMA && active_tool != TOOL_GRIPPER_GIMBAL &&
                    active_tool != TOOL_3D_PRINTER) {
                    HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET); // Cut power once finished
                }
            }
        }
    }
}

// EXTI3 - the touch scan probe and the drill/3D-printer tachometer share
// this line (see PB3's shared usage elsewhere), routed to Port B in
// MX_GPIO_Post_Init.
void EXTI3_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
}

// Hotend fan FG tachometer (PA6, EXTI6) - falls in this chip's combined
// 5-9 vector.
void EXTI9_5_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
}

// =============================================================================
// 11. BASE CLOCK SUBSYSTEM (48 MHz INTERNAL, HARDWARE-SYNCED)
// =============================================================================
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    // HSI (8MHz) through the PLL: this chip divides HSI by 2 before the
    // multiplier automatically, so 4MHz x16 = 64MHz - the practical ceiling
    // without an external crystal (72MHz needs HSE, which this board doesn't
    // populate).
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2; // CAN/I2C1 bus - max 36MHz, gives 32MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1; // Timers/ADC bus - 64MHz
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C1 | RCC_PERIPHCLK_I2C2;
    PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
    PeriphClkInit.I2c2ClockSelection = RCC_I2C2CLKSOURCE_HSI;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
}

// =============================================================================
// 12. FULL PERIPHERAL INITIALIZATION BLOCK
// =============================================================================
void MX_GPIO_Init_Early(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    // SYSCFG's clock must be enabled for writes to SYSCFG->EXTICR (which
    // selects which GPIO port feeds each EXTI line) to actually take
    // effect - without it, routing EXTI3 to Port B (touch probe, endstop
    // tools' PB3 reads) would be silently ignored by hardware.
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    // Heartbeat / local diagnostic LED configuration (PA15 - Pin 25)
    HAL_GPIO_WritePin(LED_DIAG_PORT, LED_DIAG_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LED_DIAG_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_DIAG_PORT, &GPIO_InitStruct);

    // Logic inputs for reading the ID addressing matrix (PF0, PF1, PB4, PB7, PC13)
    GPIO_InitStruct.Pin = ID0_PIN | ID1_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ID2_PIN | ID3_PIN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PC13 (ID4) and PC15 (TMC_DIAG0, configured later) are both
    // backup-domain-adjacent pins. This design never enables the LSE
    // oscillator (no RTC crystal populated), so both should read/write
    // correctly as plain GPIO without this - but enabling PWR's clock and
    // backup-domain write access is cheap insurance either way, and
    // removes any doubt for pins with this much STM32-family-specific
    // nuance around their behavior.
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitStruct.Pin = ID4_PIN;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void MX_GPIO_Post_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Base dynamic configuration for critical Port B lines
    HAL_GPIO_WritePin(GPIOB, DIR_PIN, GPIO_PIN_RESET);
    // Tool-aware boot default for PB6 (active_tool is already known before
    // this function runs): HIGH = stepper disabled, the safe default for the
    // 6 motor tools - but per CANBUS.TXT's polarity, HIGH means interlock
    // ARMED for the laser, and HIGH means Spin (brake released) for the
    // drill, both wrong safe defaults, so those two get their own explicit case.
    if (active_tool == TOOL_LASER_ENGRAVER) {
        HAL_GPIO_WritePin(GPIOB, TMC_ENN_PIN, GPIO_PIN_RESET); // Laser: LOW is safe/locked
    } else if (active_tool == TOOL_DRILL) {
        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // Drill: LOW is braked, on its own dedicated pin
    } else if (active_tool == TOOL_INVALID) {
        // Covers every possible physical identity this state could
        // correspond to - LOW is safe for the shared stepper/laser pin,
        // and the drill's own, separate pin also needs to be forced to its
        // safe (braked) state independently.
        HAL_GPIO_WritePin(GPIOB, TMC_ENN_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(GPIOB, TMC_ENN_PIN, GPIO_PIN_SET); // Motors: HIGH is disabled
    }

    GPIO_InitStruct.Pin = DIR_PIN | TMC_ENN_PIN | DRILL_BRAKE_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PB3/STEP_PIN is shared with the vacuum pickup's LM393 comparator
    // output and the generic probe/endstop input, so it's only ever
    // configured as an output here for the tools that actually drive a
    // stepper off it (R39's 330R series protection and the LM393's
    // open-collector output make this a low-risk net either way, but
    // getting the mode right still matters). Every other tool leaves PB3
    // untouched for the reconfiguration block further down (search "PB3
    // reconfiguration") to set once, correctly, as its actual final mode.
    if (active_tool == TOOL_PASTE_DISPENSER || active_tool == TOOL_LIQUID_DISPENSER ||
        active_tool == TOOL_SCREWDRIVER || active_tool == TOOL_GRIPPER_GIMBAL ||
        active_tool == TOOL_GRIPPER_NEMA || active_tool == TOOL_3D_PRINTER) {
        HAL_GPIO_WritePin(GPIOB, STEP_PIN, GPIO_PIN_RESET);
        GPIO_InitStruct.Pin = STEP_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    }

    // Pin init for camera ring control (PB1) - independent from the status
    // LED's PA7/SPI1, specifically so CONN_LED1/CONN_LED2 don't need a PCB
    // trace joining them in series.
    HAL_GPIO_WritePin(LED_RING_PORT, LED_RING_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LED_RING_PIN;
    HAL_GPIO_Init(LED_RING_PORT, &GPIO_InitStruct);

    // PA2 (bit-banged) is free. Left unconfigured (floating input, the reset default).

    // Soldering iron / hotend power actuator pin (PA1)
    HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = T12_PWM_PIN;
    HAL_GPIO_Init(T12_PWM_PORT, &GPIO_InitStruct);

    // PA7 carries SPI1_MOSI (AF5) for the LED chain (see MX_SPI1_Init).
    // Confirmed against this exact chip's datasheet
    // (DS9866 Table 13): PA7's alternate function list includes SPI1_MOSI.
    GPIO_InitStruct.Pin = LED_SPI_MOSI_PIN; // PA7 - SPI1_MOSI for the LED chain
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PA8 carries TIM1_CH1 (AF6) for the drill/laser/layer-fan PWM,
    // reconfigured per active tool. Confirmed against this
    // exact chip's datasheet (DS9866 Table 13): PA8's alternate function list
    // is "MCO, TIM1_CH1, USART1_CK, EVENTOUT".
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    // Restore to the baseline (plain digital output) so the following
    // HAL_GPIO_Init calls in this block, which reuse this same struct
    // without reassigning every field, don't accidentally inherit this
    // pin's alternate-function settings. All 4 fields that PA8's config
    // above touched, not just Mode/Alternate - Speed specifically was
    // being left at HIGH (this block's own original baseline, set at the
    // top of this function, is LOW) until this was caught, silently
    // giving every following digital output in this block faster edge
    // rates than intended.
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0;

    // Secondary drill pins (PA3 as EXTI, PA4 as rotation direction)
    HAL_GPIO_WritePin(DRILL_FRIN_PORT, DRILL_FRIN_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = DRILL_FRIN_PIN;
    HAL_GPIO_Init(DRILL_FRIN_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DRILL_FGIN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DRILL_FGIN_PORT, &GPIO_InitStruct);

    // EXTI vector interrupt enable
    HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void MX_CAN_Init(void) {
    CAN_FilterTypeDef sFilterConfig;
    __HAL_RCC_CAN1_CLK_ENABLE();

    // CAN_RX/CAN_TX (PA11/PA12) alternate-function configuration. Without
    // this, the pins stay at their reset-default state and the bus signals
    // never actually reach the physical pins, regardless of how the CAN
    // peripheral itself is configured below.
    GPIO_InitTypeDef GPIO_CAN = {0};
    GPIO_CAN.Pin = CAN_RX_PIN | CAN_TX_PIN;
    GPIO_CAN.Mode = GPIO_MODE_AF_PP;
    GPIO_CAN.Pull = GPIO_PULLUP;
    GPIO_CAN.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_CAN.Alternate = GPIO_AF9_CAN;
    HAL_GPIO_Init(CAN_GPIO_PORT, &GPIO_CAN);

    hcan.Instance = CAN;
    // Bit timing for 500 Kbps: CAN sits on APB1 (32MHz on this chip - see
    // SystemClock_Config). Prescaler=4, 16 total time quanta (1 SYNC_SEG +
    // 13 BS1 + 2 BS2): 32,000,000 / 4 / 16 = 500,000 exactly, at the
    // standard 87.5% sample point. SJW=2TQ (up from 1TQ) for better
    // tolerance of clock drift - this tool head sits near a 260C hotend, a
    // T12 iron, and TMC driver heatsinks, any of which can shift a
    // crystal/resonator's frequency over time. SJW is physically bounded by
    // BS2, so getting a real 2TQ jump width meant widening BS2 to 2TQ too
    // (was 1TQ) rather than just changing this one field - baud rate and
    // sample point land in the exact same place either way, just with finer
    // time-quantum resolution now (16 total instead of 8).
    hcan.Init.Prescaler = 4;
    hcan.Init.Mode = CAN_MODE_NORMAL;
    hcan.Init.SyncJumpWidth = CAN_SJW_2TQ;
    hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
    hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan.Init.TimeTriggeredMode = DISABLE;
    hcan.Init.AutoBusOff = ENABLE;
    hcan.Init.AutoWakeUp = DISABLE;
    hcan.Init.AutoRetransmission = ENABLE;
    hcan.Init.ReceiveFifoLocked = DISABLE;
    hcan.Init.TransmitFifoPriority = ENABLE;
    HAL_CAN_Init(&hcan);

    // Open filters to capture every mapped setpoint ID range
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);

    HAL_CAN_Start(&hcan);
    HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
    // Error/bus-off notifications: AutoBusOff recovery is already enabled,
    // but without this, a bus fault (electrically plausible next to a BLDC
    // drill) would silence the transceiver with zero visibility that
    // anything had gone wrong. See HAL_CAN_ErrorCallback below.
    HAL_CAN_ActivateNotification(&hcan, CAN_IT_ERROR | CAN_IT_BUSOFF);

    // NVIC vector for CAN RX - without this, HAL_CAN_RxFifo0MsgPendingCallback
    // never runs: the hardware flags a pending message, but the CPU never
    // services the interrupt, and the board never responds to a command.
    HAL_NVIC_SetPriority(CAN_RX0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(CAN_RX0_IRQn);
    // Error/bus-off interrupts route through the separate CAN_SCE vector,
    // not CAN_RX0 - this is needed in addition to the notification above
    // for HAL_CAN_ErrorCallback to ever actually fire.
    HAL_NVIC_SetPriority(CAN_SCE_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(CAN_SCE_IRQn);
}

void MX_TIM3_Full_Init(void) {
    // A pure time-base - the drill/laser/layer-fan PWM lives on
    // TIM1/PA8 instead, so TIM3 doesn't need to double as a PWM source and
    // isn't constrained to a period that also has to work as a
    // usable PWM base for those tools.
    __HAL_RCC_TIM3_CLK_ENABLE();
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 63; // 64MHz/64 = 1MHz tick, same resolution as before
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 1000;  // Fine step resolution (0.1%)
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&htim3);

    // NVIC vector for TIM3's update interrupt - this is what actually
    // drives HAL_TIM_PeriodElapsedCallback, which consumes steps_remaining
    // (set via CAN) to move the 6 stepper-motor tools.
    HAL_NVIC_SetPriority(TIM3_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    HAL_TIM_Base_Start_IT(&htim3);
}

// Drill/laser/layer-fan PWM on TIM1_CH1 (PA8), a fully independent timer
// from TIM3's step-tick duty. Takes the target period so each tool gets
// an appropriate rate: the layer fan needs its documented exact 25kHz
// (EFB0424VHD-CP0, see PINOUT_CONNECTORS.TXT), drill/laser run at ~20kHz.
// HAL_TIM_PWM_Start
// automatically enables TIM1's Master Output Enable (MOE) bit for
// break-capable advanced timers like this one - no extra step needed beyond
// the standard PWM start call.
void MX_TIM1_DrillLaserFan_Init(uint32_t period) {
    TIM_OC_InitTypeDef sConfigOC = {0};
    __HAL_RCC_TIM1_CLK_ENABLE();

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = period;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    HAL_TIM_PWM_Init(&htim1);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    // Belt-and-suspenders: HAL_TIM_PWM_Start already enables MOE automatically
    // for break-capable advanced timers like this one (confirmed in this
    // exact HAL version's source), but that behavior isn't part of the
    // documented public contract - an explicit call costs nothing and
    // survives a future HAL version changing that internal detail.
    __HAL_TIM_MOE_ENABLE(&htim1);
}

// Hotend fan PWM (CONN_FAN1, PA5). Same 25kHz target as the layer fan, on its
// own timer (TIM2, confirmed free - AF2 on PA5, verified against ST's official
// datasheet Table 14) so it doesn't interfere with TIM3's stepper tick or the
// layer fan's TIM17.
void MX_TIM2_HotendFan_Init(void) {
    TIM_OC_InitTypeDef sConfigOC = {0};
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 2559; // 64MHz / 2560 = 25.000 kHz exactly
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&htim2);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1);

    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
}

void MX_ADC_Init(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    __HAL_RCC_ADC12_CLK_ENABLE();
    hadc.Instance = ADC1;
    // Synchronous clock derived directly from AHB (64MHz/2=32MHz) - simpler
    // than routing through the shared ADC12 PLL clock block, and comfortably
    // under this chip's ADC clock ceiling.
    hadc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
    hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc.Init.LowPowerAutoWait = DISABLE;
    hadc.Init.ContinuousConvMode = DISABLE;
    hadc.Init.NbrOfConversion = 1;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc.Init.DMAContinuousRequests = DISABLE;
    hadc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    HAL_ADC_Init(&hadc);

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    // This chip's sampling-time steps are entirely different from the F0
    // equivalent (no 41.5-cycle option exists here) - 61.5 cycles is the
    // nearest available step that still errs toward more settling time
    // rather than less, for a resistive-divider/thermocouple-amplifier
    // source that doesn't need fast conversions anyway.
    sConfig.SamplingTime = ADC_SAMPLETIME_61CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    HAL_ADC_ConfigChannel(&hadc, &sConfig);

    // This chip's calibration call takes an explicit single-ended/differential
    // parameter that F0's equivalent didn't need.
    HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED);
}

// =============================================================================
// 13. MAIN ENTRY POINT AND REASSIGNABLE STATE-MACHINE LOOP
// =============================================================================
int main(void) {
    // This application lives at 0x08008000, not 0x08000000 (see
    // STM32F303CCTx_APP.ld) - a bootloader and its metadata page occupy
    // the space ahead of it. The vector table moved with it, so the core
    // needs to be told where to find it before anything can rely on an
    // interrupt firing correctly.
    SCB->VTOR = 0x08008000;

    uint32_t tick_heartbeat = 0;
    uint32_t tick_screen = 0;
    uint32_t tick_pid = 0;
    uint32_t tick_fram_check = 0;
    
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init_Early();

    // I2C2 (shared by the OLED and the F-RAM - see MX_I2C2_Init_Early's
    // own comment) needs to be up before Identify_PhysicalTool() below,
    // specifically for the case where the ID-jumper reading is 0x1F -
    // that function needs to read the F-RAM's free-configuration
    // register to resolve a real tool in that case, and this is the
    // earliest point in boot it can do that from.
    MX_I2C2_Init_Early();

    // Tool identification happens here - right after the ID-jumper pins are
    // configured, before Post_Init - so the laser's safe PB6 default can be
    // set correctly on the very first write; see MX_GPIO_Post_Init.
    Identify_PhysicalTool();

    MX_GPIO_Post_Init();
    MX_CAN_Init();
    MX_TIM3_Full_Init();
    MX_DMA_Init();  // must come before MX_SPI1_Init, which links the two
    MX_SPI1_Init();

    // Specific ADC channel mapping based on the ID that was read
    // Explicit ANALOG mode for whichever pin is in use - the reset-default
    // state happens to already be analog on this chip, but leaving it
    // implicit is fragile (anything upstream that touches this port, now or
    // in a future revision, could leave it as a digital input/pull-up
    // instead, silently loading down the NTC/thermocouple divider).
    GPIO_InitTypeDef GPIO_AdcPin = {0};
    GPIO_AdcPin.Mode = GPIO_MODE_ANALOG;
    GPIO_AdcPin.Pull = GPIO_NOPULL;
    if (active_tool == TOOL_SOLDERING_IRON) {
        GPIO_AdcPin.Pin = T12_ADC_PIN;
        HAL_GPIO_Init(GPIOA, &GPIO_AdcPin);
        MX_ADC_Init(ADC_CHANNEL_1); // PA0 = ADC1_IN1 on this chip, for the T12 thermocouple only
    } else if (active_tool == TOOL_VACUUM_PICKUP || active_tool == TOOL_3D_PRINTER) {
        // The 3D printer's hotend NTC is wired to PB0/CONN_SEN (with R40's
        // 4.7k pull-up), not PA0/CONN_T12 - shares the vacuum pickup's ADC
        // channel since neither tool ever runs at the same time as the other.
        GPIO_AdcPin.Pin = TCRT_A0_ADC_PIN;
        HAL_GPIO_Init(GPIOB, &GPIO_AdcPin);
        MX_ADC_Init(ADC_CHANNEL_11); // PB0 = ADC1_IN11 on this chip, for vacuum pickup sensing and 3D hotend NTC
    } else {
        GPIO_AdcPin.Pin = T12_ADC_PIN;
        HAL_GPIO_Init(GPIOA, &GPIO_AdcPin);
        MX_ADC_Init(ADC_CHANNEL_1); // PA0 = ADC1_IN1 on this chip
    }

    // Drill/laser/layer-fan PWM is consolidated onto TIM1_CH1 (PA8), fully
    // independent of the step-tick timer. The layer fan
    // needs its documented exact 25kHz; drill/laser get the same independence -
    // there's no reason not to give them a proper PWM rate too.
    if (active_tool == TOOL_3D_PRINTER) {
        MX_TIM1_DrillLaserFan_Init(2559); // 64MHz/2560 = 25.000kHz exactly

        // Hotend fan: PA5 as TIM2_CH1/AF1 PWM output, PA6 as an EXTI6
        // falling-edge input for its tachometer. Both pins are only ever used
        // in 3D printer mode via CONN_FAN1 - genuinely free otherwise.
        GPIO_InitTypeDef GPIO_HotendFanPWM = {0};
        GPIO_HotendFanPWM.Pin = HOTEND_FAN_PWM_PIN;
        GPIO_HotendFanPWM.Mode = GPIO_MODE_AF_PP;
        GPIO_HotendFanPWM.Pull = GPIO_NOPULL;
        GPIO_HotendFanPWM.Speed = GPIO_SPEED_FREQ_HIGH;
        GPIO_HotendFanPWM.Alternate = GPIO_AF1_TIM2;
        HAL_GPIO_Init(HOTEND_FAN_PWM_PORT, &GPIO_HotendFanPWM);
        MX_TIM2_HotendFan_Init();

        // No dedicated external pull-up populated for FAN_FG in the current BOM
        // (R41/R42 are series protection, not pull-ups) - using the MCU's
        // internal pull-up for this open-collector FG line. Fine for a short
        // on-board/short-harness run; if the fan cable ends up long, consider
        // adding a proper external 4.7k-10k pull-up like DRILL_FGIN's R16.
        GPIO_InitTypeDef GPIO_HotendFanFG = {0};
        GPIO_HotendFanFG.Pin = HOTEND_FAN_FG_PIN;
        GPIO_HotendFanFG.Mode = GPIO_MODE_IT_FALLING;
        GPIO_HotendFanFG.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(HOTEND_FAN_FG_PORT, &GPIO_HotendFanFG);
        // NVIC enable for EXTI9_5_IRQn already happens unconditionally in
        // MX_GPIO_Post_Init, alongside EXTI3_IRQn - no need to repeat it here.
    } else if (active_tool == TOOL_DRILL || active_tool == TOOL_LASER_ENGRAVER) {
        MX_TIM1_DrillLaserFan_Init(3199); // 64MHz/3200 = 20kHz - was 1kHz when
                                            // this shared TIM3's step tick
    }

    // PB3 reconfiguration: only just configured as a push-pull STEP
    // output above, and only for the 6 stepper-based tools. Every other
    // tool - including TOOL_SCAN_PROBE and the vacuum/endstop group below -
    // reaches this point with PB3 exactly as it was left by
    // MX_GPIO_Init_Early(), i.e. untouched, so this is the only place
    // their mode ever gets set:
    if (active_tool == TOOL_SCAN_PROBE) {
        // Touch probe: EXTI3 falling edge. This is also what actually routes
        // EXTI3 to Port B - the unconditional DRILL_FGIN_PIN config elsewhere
        // wires EXTI3 to Port A (PA3) for the drill/3D-printer tachometer, and
        // only one port can feed EXTI line 3 at a time, so this must run for
        // every OTHER tool to claim it back for PB3.
        GPIO_InitTypeDef GPIO_Probe = {0};
        GPIO_Probe.Pin = TOUCH_IN_PIN;
        GPIO_Probe.Mode = GPIO_MODE_IT_FALLING;
        GPIO_Probe.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOB, &GPIO_Probe);
    } else if (active_tool != TOOL_PASTE_DISPENSER && active_tool != TOOL_LIQUID_DISPENSER &&
               active_tool != TOOL_SCREWDRIVER && active_tool != TOOL_GRIPPER_GIMBAL &&
               active_tool != TOOL_GRIPPER_NEMA && active_tool != TOOL_3D_PRINTER) {
        // Plain polled digital input: vacuum pickup's LM393 comparator, and the
        // new generic endstop/limit-switch input (Soldering iron, Drill, Laser,
        // AOI - NEW). Active low, so the internal pull-up gives a clean idle-high.
        GPIO_InitTypeDef GPIO_Endstop = {0};
        GPIO_Endstop.Pin = TOUCH_IN_PIN;
        GPIO_Endstop.Mode = GPIO_MODE_INPUT;
        GPIO_Endstop.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOB, &GPIO_Endstop);
    }

    // Hardware independent watchdog (IWDG): this was entirely absent before.
    // Software watchdogs (Watchdog_Safety_Laser etc.) only work if the main
    // loop is still running at all - if the CPU genuinely hangs (I2C bus
    // lockup, a memory fault, an infinite loop), those never get a chance to
    // fire, and every PWM output stays latched at whatever it was last set
    // to: a heater, a 10W laser, a motor - powered until someone manually
    // cuts power. LSI-clocked (independent of the main clock tree, so it
    // still runs even if HSI/PLL fails), ~800ms timeout. Started here, before
    // OLED_Init, rather than after the splash - I2C is notoriously
    // ESD-sensitive, and starting the watchdog before OLED_Init's first
    // transactions means a bus lockup right there can't hang the
    // board forever with no recovery.
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 999; // (999+1) * 32 / 40000 ~= 800ms @ ~40kHz LSI
    HAL_IWDG_Init(&hiwdg);

    // Graphics subsystem init via hardware I2C2 - initializes the I2C2
    // peripheral itself as part of its own setup, before its first
    // transactions.
    OLED_Init();
    // Parameter-persistence F-RAM shares I2C2 with the OLED above - see
    // the FRAM_I2C_ADDR comment for why. Loads (but doesn't act on)
    // whatever was saved before the last shutdown - see SavedState_Load's
    // own comment for the safety reasoning.
    SavedState_Load();
    // Expansion connector's bit-banged SPI bus - see the function's own
    // comment and PINOUT_CONNECTORS.TXT for why this isn't a hardware
    // SPI peripheral.
    MX_ExpansionSPI_Init();
    // Expansion connector's bit-banged I2C bus - see the function's own
    // comment for why this isn't a hardware I2C peripheral either.
    MX_ExpansionI2C_Init();
    Render_SplashScreen(0);
    // Big JuanenBOT/URTC splash, 5s total (CAN already active, no frames are
    // lost) - broken into small chunks with a refresh in between instead of
    // one HAL_Delay(5000), since that would trip the watchdog above on its own.
    // The face (pages 2-6, the white area) is redrawn every ~800ms to
    // animate without the overhead/flicker of redrawing the whole splash
    // each step - the yellow strip (URTC + version) never changes, so it's
    // never touched again after the first draw.
    for (uint8_t i = 0; i < 50; i++) {
        HAL_IWDG_Refresh(&hiwdg);
        if (i > 0 && i % 8 == 0) {
            uint8_t face_buf[128];
            uint8_t frame = (i / 8) % 4;
            for (uint8_t p = 0; p < 5; p++) {
                memcpy(face_buf, SplashFace[frame][p], 128);
                OLED_SetCursor(p + 2, 0);
                OLED_WriteData(face_buf, 128);
            }
        }
        // CAN is already live during this delay, so a laser/heater command
        // arriving right after boot would start actuating with none of the
        // safety watchdogs below running yet - they only get called from
        // the main loop, which hasn't started. Mirroring the
        // safety-critical subset here closes that window.
        Watchdog_Safety_Laser();
        Watchdog_Safety_Drill();
        Watchdog_Safety_LayerFan();
        Watchdog_Safety_SolderIron();
        Watchdog_Safety_HotendHeater();
        Control_SolderingIron_PID();
        Control_3D_Hotend_PID();
        HAL_Delay(100);
    }

    boot_sequence_active = 0; // splash is done - movement commands are now safe to act on
    for(uint8_t page=0; page<8; page++) OLED_ClearPage(page);

    while (1) {
        HAL_IWDG_Refresh(&hiwdg);

        // CAN bus-off recovery - can_bus_error_flag is set by
        // HAL_CAN_ErrorCallback and consumed here to actually restart the
        // peripheral, not just report the condition via the 0x110 status
        // response. A genuine bus-off condition (realistic in this
        // environment, given the EMI nearby motor drivers can put on the
        // bus) would otherwise leave this tool permanently disconnected
        // until a power cycle. Restarting is a low-risk, self-retrying
        // attempt - if the bus is still bad, the error callback just
        // fires again and this tries once more next loop iteration.
        if (can_bus_error_flag) {
            can_bus_error_flag = 0;
            HAL_CAN_Start(&hcan);
        }

        // Visual heartbeat and hardware alert-state indicator (PA15)
        if (HAL_GetTick() - tick_heartbeat >= (system_error_flag ? 100 : 500)) {
            tick_heartbeat = HAL_GetTick();
            HAL_GPIO_TogglePin(LED_DIAG_PORT, LED_DIAG_PIN);
        }

        // Cyclic safe thermal control loop (every 20ms)
        if (HAL_GetTick() - tick_pid >= 20) {
            tick_pid = HAL_GetTick();
            Control_SolderingIron_PID();
            Control_3D_Hotend_PID();
            Watchdog_Safety_Laser();
            Watchdog_Safety_Drill();
            Watchdog_Safety_LayerFan();
            Watchdog_Safety_HotendFan();
            Watchdog_Safety_SolderIron();
            Watchdog_Safety_HotendHeater();
        }

        // Parameter-persistence check (every 500ms) - cheap when nothing's
        // changed (a memcmp), the actual F-RAM write is separately
        // rate-limited inside SavedState_MaybeSave itself.
        if (HAL_GetTick() - tick_fram_check >= 500) {
            tick_fram_check = HAL_GetTick();
            SavedState_MaybeSave();
        }

        // Asynchronous loop for secondary telemetry and UI refresh (every 150ms)
        if (HAL_GetTick() - tick_screen >= 150) {
            tick_screen = HAL_GetTick();
            Control_SensorTelemetry(); // vacuum pickup's own actual CAN send (0x145) happens separately, in the telemetry TX block below
            Control_EndstopTelemetry();
            Telemetry_Drill();            Telemetry_LayerFan();
            Telemetry_HotendFan();

            static uint8_t anim_counter = 0;
            if (++anim_counter >= 3) {        // ~450ms per frame: animation is visible but not frantic
                anim_counter = 0;
                animation_frame = (animation_frame + 1) % 4;
            }
            Render_ToolScreen();
            
            CAN_TxHeaderTypeDef txH;
            uint8_t txD[3];
            uint32_t mb;
            txH.RTR = CAN_RTR_DATA;
            txH.IDE = CAN_ID_STD;
            txH.TransmitGlobalTime = DISABLE;

            // Gated by active_tool - sends the temperature telemetry frame
            // only for the tool that actually owns current_temperature at
            // the moment (soldering iron or 3D printer hotend), rather than
            // on a flat timer regardless of which tool is active.
            if (active_tool == TOOL_SOLDERING_IRON) {
                txH.StdId = 0x135;
                txH.DLC = 3; // extended: byte 2 = endstop
                txD[0] = (uint8_t)((current_temperature >> 8) & 0xFF);
                txD[1] = (uint8_t)(current_temperature & 0xFF);
                txD[2] = endstop_triggered;
                if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
                }
            } else if (active_tool == TOOL_3D_PRINTER) {
                // bxCAN has exactly 3 TX mailboxes, and these 3 sends happen
                // faster than the bus can necessarily drain them - checking
                // GetTxMailboxesFreeLevel before each send (below) skips
                // cleanly when none is free, rather than calling blind and
                // silently dropping a failed send. Not worth a retry here;
                // this is periodic telemetry, not a one-shot command.
                txH.StdId = 0x175;
                txH.DLC = 2;
                txD[0] = (uint8_t)((current_temperature >> 8) & 0xFF);
                txD[1] = (uint8_t)(current_temperature & 0xFF);
                if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
                }

                // Layer fan RPM telemetry (0x177)
                txH.StdId = 0x177;
                txH.DLC = 2;
                txD[0] = (uint8_t)((layer_fan_actual_rpm >> 8) & 0xFF);
                txD[1] = (uint8_t)(layer_fan_actual_rpm & 0xFF);
                if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
                }

                // Hotend fan RPM telemetry (0x179)
                txH.StdId = 0x179;
                txH.DLC = 2;
                txD[0] = (uint8_t)((hotend_fan_actual_rpm >> 8) & 0xFF);
                txD[1] = (uint8_t)(hotend_fan_actual_rpm & 0xFF);
                if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
                }
            }
            // 0x145: vacuum pickup analog/digital sensor telemetry, per CANBUS.TXT
            else if (active_tool == TOOL_VACUUM_PICKUP) {
                txH.StdId = 0x145;
                txH.DLC = 3;
                txD[0] = (uint8_t)((sensor_analog_reading >> 8) & 0xFF);
                txD[1] = (uint8_t)(sensor_analog_reading & 0xFF);
                txD[2] = sensor_digital_reading;
                if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
                }
            }
            // 0x147: drill RPM + endstop telemetry
            else if (active_tool == TOOL_DRILL) {
                txH.StdId = 0x147;
                txH.DLC = 3; // extended: byte 2 = endstop
                __disable_irq();
                uint16_t drill_rpm_snapshot = drill_actual_rpm; // atomic snapshot, defensive against a mid-read IRQ update
                __enable_irq();
                txD[0] = (uint8_t)((drill_rpm_snapshot >> 8) & 0xFF);
                txD[1] = (uint8_t)(drill_rpm_snapshot & 0xFF);
                txD[2] = endstop_triggered;
                if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
                }
            }
            // The laser and AOI endstop telemetry frames are minimal 1-byte
            // IDs, placed next to each tool's own command ID (0x160/0x150)
            // the same way 0x140->0x147 and 0x130->0x135 are already paired.
            else if (active_tool == TOOL_LASER_ENGRAVER) {
                txH.StdId = 0x165;
                txH.DLC = 1;
                txD[0] = endstop_triggered;
                if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
                }
            }
            else if (active_tool == TOOL_AOI_INSPECTION) {
                txH.StdId = 0x155;
                txH.DLC = 1;
                txD[0] = endstop_triggered;
                if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
                }
            }
        }

        // Background processing of flags raised from CAN bus interrupts,
        // checked every loop iteration (not tied to the 150ms tick above)
        // for near-immediate response, with the AOI strobe as its own
        // independent check rather than nested inside the ring-flag one.
        if (oled_mode_pending_flag) {
            Process_OLED_NightModeChange();
        }

        Update_StatusLED_AutoColor();
        if (update_led_ring_flag) {
            // Only clears the flag once the status LED update actually
            // goes out over SPI/DMA - clearing it unconditionally first
            // would silently drop a color update that arrived mid-transfer,
            // with nothing left to retry it. The ring (bit-banged, always
            // synchronous) still always runs regardless.
            if (Update_StatusLED_SPI_DMA()) {
                update_led_ring_flag = 0;
            }
            Update_RingLEDs_BitBang();
        }

        // AOI stroboscopic sync pulse: flashes the CONN_LED2 ring at the
        // color last set via 0x100, for aoi_strobe_period milliseconds.
        if (active_tool == TOOL_AOI_INSPECTION && trigger_aoi_strobe_flag) {
            trigger_aoi_strobe_flag = 0;
            // Clamp: aoi_strobe_period comes straight from a CAN command
            // with no prior validation. A strobe/flash sync pulse should
            // be brief (single-digit to low-double-digit ms); without a
            // cap, a misconfigured or garbled command could keep the LEDs
            // on far longer than intended.
            uint16_t raw_period = aoi_strobe_period; // single atomic read
            uint16_t safe_period = (raw_period > 500) ? 500 : (raw_period < 1) ? 1 : raw_period;
            for (int i = 0; i < 8; i++) {
                ring_pixels[i].R = ring_color_r;
                ring_pixels[i].G = ring_color_g;
                ring_pixels[i].B = ring_color_b;
            }
            Update_RingLEDs_BitBang();
            // Records when to turn the LEDs back off instead of blocking
            // here with HAL_Delay(safe_period) - the rest of this loop
            // (PID, watchdogs, telemetry, OLED) keeps running normally
            // during the pulse instead of freezing for up to 500ms.
            aoi_strobe_off_tick = HAL_GetTick() + safe_period;
            aoi_strobe_active = 1;
        }
        if (aoi_strobe_active && (int32_t)(HAL_GetTick() - aoi_strobe_off_tick) >= 0) {
            aoi_strobe_active = 0;
            for (int i = 0; i < 8; i++) {
                ring_pixels[i].R = 0;
                ring_pixels[i].G = 0;
                ring_pixels[i].B = 0;
            }
            Update_RingLEDs_BitBang();
        }
    }
}

// =============================================================================
// 14. PARAMETER PERSISTENCE (FM24CL64B F-RAM, SHARED I2C2)
// =============================================================================
// Saves a snapshot of the active tool's setpoints and the global LED/OLED
// settings periodically, so a sudden power loss doesn't leave the state
// "before" a shutdown as unknowable as the shutdown itself was unplanned.
// Deliberately does NOT auto-apply a recovered setpoint to anything that
// could re-energize on its own after boot (a heater, the laser, a spinning
// motor) - see SavedState_Load()'s own comment for the full reasoning.
// This is a diagnostic/recovery aid, not a "resume exactly where it left
// off" feature.

// Low-level chunked write. addr/len are trusted to be within
// FRAM_SIZE_BYTES - this is an internal helper, not exposed over CAN,
// so the only caller is this same file. Returns 1 on success, 0 on any
// HAL error (I2C bus fault, chip not present/not acking, etc.) - every
// caller treats a failure as "couldn't save this time, try again later"
// rather than anything fatal, since nothing about this board's core
// operation depends on this chip being present or working.
uint8_t FRAM_WriteBytes(uint16_t addr, const uint8_t *data, uint16_t len) {
    while (len > 0) {
        uint16_t space_in_chunk = FRAM_WRITE_CHUNK - (addr % FRAM_WRITE_CHUNK);
        uint16_t chunk = (len < space_in_chunk) ? len : space_in_chunk;
        // HAL_I2C_Mem_Write handles the 2-byte (16-bit) internal memory
        // address this chip's 8KB capacity needs (MEMADD_SIZE_16BIT) -
        // confirmed against the FM24CL64B's own documented control-byte
        // + address-byte protocol, the same scheme a serial EEPROM of the
        // same capacity would use (this chip is a drop-in replacement by
        // design). No delay follows the write below: F-RAM writes
        // complete at bus speed with no internal write cycle to wait for
        // ("NoDelay" writes, per the datasheet - unlike a real EEPROM,
        // there's nothing here to poll or wait out).
        if (HAL_I2C_Mem_Write(&hi2c2, FRAM_I2C_ADDR, addr, I2C_MEMADD_SIZE_16BIT,
                               (uint8_t *)data, chunk, 50) != HAL_OK) {
            return 0;
        }
        addr += chunk;
        data += chunk;
        len -= chunk;
    }
    return 1;
}

// Low-level read - one single transaction regardless of len, same as a
// write would be if FRAM_WRITE_CHUNK's conservative chunking weren't
// applied to it (reads never needed that chunking to begin with, on
// this chip or a real EEPROM either).
uint8_t FRAM_ReadBytes(uint16_t addr, uint8_t *data, uint16_t len) {
    return HAL_I2C_Mem_Read(&hi2c2, FRAM_I2C_ADDR, addr, I2C_MEMADD_SIZE_16BIT,
                             data, len, 50) == HAL_OK;
}

static uint8_t SavedState_Checksum(const SavedState_t *s) {
    // CRC-8 (polynomial 0x07, the standard CRC-8/SMBUS variant) over
    // every byte except the checksum field itself. Upgraded from a plain
    // additive checksum - that detected "blank/grossly corrupted" F-RAM
    // fine, but two bytes corrupted in a way that happens to cancel out
    // (one +1, one -1) would pass it undetected. Still not a security
    // boundary the way the OTA update's HMAC-SHA256 is (this data is
    // passive display settings, not anything safety-relevant - see
    // SavedState_Load's own comment on what it does and doesn't act on),
    // just a meaningfully stronger version of the same "was this
    // corrupted" check. Bit-by-bit rather than a 256-entry lookup table:
    // this runs only at F-RAM save/load, never in a timing-critical
    // path, so the table's flash cost isn't worth it for 27 bytes.
    const uint8_t *bytes = (const uint8_t *)s;
    uint8_t crc = 0x00;
    for (size_t i = 0; i < sizeof(SavedState_t) - 1; i++) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// Called once, early in main()'s boot sequence, after active_tool is known
// but before anything reads recovered_state. Populates recovered_state and
// had_valid_recovered_state - it does NOT apply any of it to a live
// setpoint. Recovering a heater/laser/motor setpoint and quietly
// re-arming it the moment the board boots would mean a power blip (or a
// deliberate power cycle for entirely unrelated reasons) could resume a
// potentially hazardous operation with nobody watching, which defeats the
// point of every communication watchdog elsewhere in this firmware. What
// this DOES provide: recovered_state is readable over CAN (0x190/0x191 -
// see CANBUS.TXT) so a master/diagnostic tool can learn what the board was
// doing before the outage and make an informed decision - including
// deliberately re-sending the same setpoint if that's actually wanted.
// The safe, passive settings (LED colors, OLED mode) ARE applied directly
// here, since there's no hazard in restoring what color an LED was.
void SavedState_Load(void) {
    SavedState_t s;
    had_valid_recovered_state = 0;
    if (!FRAM_ReadBytes(SAVEDSTATE_FRAM_ADDR, (uint8_t *)&s, sizeof(s))) {
        return; // F-RAM not present or not responding - board still boots fine without it
    }
    if (s.magic != SAVEDSTATE_MAGIC || s.struct_version != SAVEDSTATE_VERSION) {
        return; // blank chip, or a layout from a different firmware version - don't trust it
    }
    if (SavedState_Checksum(&s) != s.checksum) {
        return; // corrupted - a partial/interrupted write, most likely
    }
    recovered_state = s;
    had_valid_recovered_state = 1;
    last_saved_state = s; // so the very first change-check in SavedState_MaybeSave() compares against what's genuinely on the chip

    // Passive, hazard-free settings only - restored directly.
    led_state_pixel.R = s.status_r;
    led_state_pixel.G = s.status_g;
    led_state_pixel.B = s.status_b;
    for (int i = 0; i < 8; i++) {
        ring_pixels[i].R = s.ring_r;
        ring_pixels[i].G = s.ring_g;
        ring_pixels[i].B = s.ring_b;
    }
    oled_night_mode = s.oled_night_mode;
    expansion_board_type = s.expansion_board_type; // passive config, not an actuator setpoint - safe to restore directly like the settings above
    free_tool_selection = s.free_tool_selection;    // same reasoning - passive config, not an actuator setpoint
    device_serial_number = s.device_serial_number;  // same reasoning - passive config, not an actuator setpoint
}

// Called every main-loop iteration. Cheap when nothing's changed (just a
// memcmp and a tick check) - the actual write only happens when something
// is both different AND at least FRAM_MIN_SAVE_INTERVAL_MS has passed
// since the last one. Unlike a real EEPROM, this isn't protecting a
// limited write-cycle budget - the FM24CL64B's endurance is rated in the
// trillions of cycles, effectively unlimited for anything this board
// would ever do. The interval is kept anyway for a different, more mundane
// reason: if a setpoint were ever adjusted rapidly (a UI slider being
// dragged, say), there's no reason to write every single intermediate
// value to the bus instead of just the one it settles on.
#define FRAM_MIN_SAVE_INTERVAL_MS 3000
void SavedState_MaybeSave(void) {
    SavedState_t s = {0};
    // Snapshotting every field with interrupts disabled - each individual
    // read below is already atomic on its own (single-byte/word volatile
    // reads), but without this a CAN message arriving mid-copy could
    // still leave a logically inconsistent mix of old/new values spread
    // across different fields (e.g. a new laser_power_setpoint but the
    // interlock pin state from before that same command). Brief enough
    // not to risk any timing-sensitive interrupt (SysTick, CAN RX) - this
    // is a few dozen register/memory reads, not a loop or a blocking call.
    __disable_irq();
    s.magic = SAVEDSTATE_MAGIC;
    s.struct_version = SAVEDSTATE_VERSION;
    s.active_tool_at_save = (uint8_t)active_tool;
    s.solder_setpoint = (active_tool == TOOL_SOLDERING_IRON) ? target_temperature : 0;
    s.printer_nozzle_setpoint = (active_tool == TOOL_3D_PRINTER) ? target_temperature : 0;
    s.drill_speed = (active_tool == TOOL_DRILL) ? drill_speed : 0;
    // Direction and interlock have no dedicated RAM variable - the CAN
    // handlers write these straight to their GPIO pin, so the pin's own
    // current output state (readable even though it's configured as an
    // output - HAL_GPIO_ReadPin reflects what's actually being driven,
    // not just an external voltage) is the only source of truth for
    // "what was it last commanded to" without adding a new variable and
    // touching those existing handlers.
    s.drill_direction = (active_tool == TOOL_DRILL)
        ? (HAL_GPIO_ReadPin(DRILL_FRIN_PORT, DRILL_FRIN_PIN) == GPIO_PIN_SET) : 0;
    s.laser_power = (active_tool == TOOL_LASER_ENGRAVER) ? laser_power_setpoint : 0;
    s.laser_interlock = (active_tool == TOOL_LASER_ENGRAVER)
        ? (HAL_GPIO_ReadPin(TMC_ENN_PORT, LASER_SAFETY_PIN) == GPIO_PIN_SET) : 0;
    s.layer_fan_power = (active_tool == TOOL_3D_PRINTER) ? layer_fan_duty : 0;
    s.hotend_fan_power = (active_tool == TOOL_3D_PRINTER) ? hotend_fan_duty : 0;
    s.status_r = led_state_pixel.R; s.status_g = led_state_pixel.G; s.status_b = led_state_pixel.B;
    s.ring_r = ring_pixels[0].R; s.ring_g = ring_pixels[0].G; s.ring_b = ring_pixels[0].B;
    s.ring_on = (ring_pixels[0].R || ring_pixels[0].G || ring_pixels[0].B) ? 1 : 0;
    s.oled_night_mode = oled_night_mode;
    s.had_critical_error = system_error_flag;
    s.expansion_board_type = expansion_board_type;
    s.free_tool_selection = free_tool_selection;
    s.device_serial_number = device_serial_number;
    __enable_irq();
    s.checksum = SavedState_Checksum(&s);

    if (memcmp(&s, &last_saved_state, sizeof(s) - 1) == 0) {
        return; // nothing meaningful changed since the last save
    }
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_fram_write_tick) < FRAM_MIN_SAVE_INTERVAL_MS) {
        return; // changed, but rate-limited - will be picked up on a later call once the interval passes
    }
    if (FRAM_WriteBytes(SAVEDSTATE_FRAM_ADDR, (const uint8_t *)&s, sizeof(s))) {
        last_saved_state = s;
        last_fram_write_tick = now;
    }
    // On failure (EEPROM missing/unresponsive), last_saved_state is
    // deliberately left unchanged, so the next call's memcmp still sees a
    // difference and retries - rather than silently giving up after one
    // failed attempt.
}
