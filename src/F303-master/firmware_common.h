// =============================================================================
// URTC Firmware - Shared types, defines, and cross-module externs
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_COMMON_H
#define FIRMWARE_COMMON_H

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
// this board already uses for EXP_TMC_STEP/DIR/EN/EXP_SPI_MOSI. SPI3's default
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
#define EXP_I2C2_SCL_PIN  GPIO_PIN_10  // PB10 - bit-banged, see firmware_expansion_i2c.c's own note on why
#define EXP_I2C2_SDA_PORT GPIOB
#define EXP_I2C2_SDA_PIN  GPIO_PIN_11  // PB11 - bit-banged, see firmware_expansion_i2c.c's own note on why
#define EXP_SPI_MISO_PORT GPIOB
#define EXP_SPI_MOSI_PIN  GPIO_PIN_15  // PB15
#define EXP_SPI_MOSI_PORT GPIOB

// Expansion driver interface (CONN_EXPANSION pins 5/6/15) - STEP/DIR/EN for
// whatever stepper driver (TMC2209 or TMC5160A) is populated on the
// expansion board, universal to either per PINOUT_CONNECTORS.TXT. Existed
// as real hardware pins since the connector's own design, but had no
// #define here until TOOL_CRIMPING_ACTUATOR (doc #12) became the first
// tool actually driving a motor through the expansion board's own driver
// instead of the onboard one - previously mentioned only in a comment
// (and briefly, incorrectly, as "EXP_PWM" - there is no dedicated PWM
// signal on this connector, confirmed against the same pinout doc).
#define EXP_TMC_STEP_PIN  GPIO_PIN_12  // PB12
#define EXP_TMC_STEP_PORT GPIOB
#define EXP_TMC_DIR_PIN   GPIO_PIN_13  // PB13
#define EXP_TMC_DIR_PORT  GPIOB
#define EXP_TMC_EN_PIN    GPIO_PIN_14  // PB14
#define EXP_TMC_EN_PORT   GPIOB

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
    // 15-tool expansion - IDs follow that
    // document's own tool numbering for traceability. Document tools #2
    // (Metrology Touch Probe) and #9 (Micro-Spindle) are explicitly
    // identical to TOOL_SCAN_PROBE and TOOL_DRILL respectively - no new ID,
    // they're just alternate physical end-effectors using the exact same
    // firmware profile, so they don't appear here at all.
    TOOL_SMT_PICKPLACE      = 12, // doc #1
    TOOL_ELECTROMAGNET      = 13, // doc #3
    TOOL_SPOT_WELDER        = 14, // doc #4
    TOOL_CONFORMAL_COATING  = 15, // doc #5
    TOOL_VACUUM_GRIPPER_LG  = 16, // doc #6
    TOOL_FLYING_PROBE       = 17, // doc #7  - ADS1115 (0x48-0x4B)
    TOOL_UV_CURING          = 18, // doc #8
    TOOL_HOTAIR_REWORK      = 19, // doc #10
    TOOL_PRESSFIT_INSERTER  = 20, // doc #11
    TOOL_CRIMPING_ACTUATOR  = 21, // doc #12 - expansion driver (TMC2209/5160A)
    TOOL_THERMAL_INSPECTION = 22, // doc #13 - MLX90640 (0x33)
    TOOL_PASTE_JETTING      = 23, // doc #14
    TOOL_ULTRASONIC_WELDER  = 24, // doc #15
    // The 5-bit ID scheme can read 0-31, but only 0-24 map to a real tool
    // head today. 25-31 (no jumpers matching any assigned tool - most
    // commonly unsoldered jumpers, a wiring fault, or simply an ID not yet
    // assigned to a tool) get their own explicit, deliberately inert state
    // here rather than silently mapping to any real tool - falling through
    // to an existing tool profile would make no sense: it isn't that tool,
    // and every other subsystem in the firmware would still treat it as if
    // it were one. This also leaves headroom for
    // up to 6 more tool heads to be added later without another ID-scheme
    // change (25-30 - 31 stays reserved for FREE CONFIGURATION).
    TOOL_INVALID         = 25
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
                                    // 3=advanced TMC2209+STM32F303CBT6, 4=advanced
                                    // TMC5160A+STM32F303CBT6, 5=basic ADS1115 (sensor
                                    // only, no driver, no slave MCU - the ADS1115
                                    // itself talks directly to this chip over the
                                    // same bit-banged I2C bus that reaches the slave
                                    // MCU on variants 3-4, just a different I2C
                                    // address and protocol since there's no slave
                                    // chip relaying it here), 6=basic MLX9064x (same
                                    // direct-connection reasoning, for the thermal
                                    // sensor instead) - see EXPANSION.TXT.
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
                                    // reading), 1-25 = one of the 25 currently
                                    // supported tool profiles (stored as id+1, not
                                    // the raw 0-24 ToolMode_t value, so 0 can
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

// =============================================================================
// Shared peripheral handles and global state - defined once in the main
// partitioned file (STM32F303CC.C), declared extern here since almost
// every module below reads or writes several of these.
// =============================================================================
extern CAN_HandleTypeDef hcan;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern I2C_HandleTypeDef hi2c2; // Hardware I2C2 for the OLED+F-RAM (PA9/PA10 - AF4 on these pins is I2C2, not I2C1, confirmed against DS9118)
extern ADC_HandleTypeDef hadc;
extern IWDG_HandleTypeDef hiwdg;

extern volatile ToolMode_t active_tool;
extern SavedState_t recovered_state;
extern uint8_t had_valid_recovered_state;
extern volatile uint8_t expansion_board_type;
extern volatile uint8_t free_tool_selection;
extern volatile uint8_t device_serial_number;
extern uint8_t raw_id_pin_value;
extern SavedState_t last_saved_state;

extern volatile LED_RGB_t led_state_pixel;
extern volatile LED_RGB_t ring_pixels[8];

extern volatile uint32_t steps_remaining;
extern volatile uint8_t step_pulse_high;
extern volatile uint32_t total_steps_setpoint;
extern volatile uint16_t current_temperature;
extern volatile uint16_t target_temperature;
extern volatile uint16_t sensor_analog_reading;
extern volatile uint8_t sensor_digital_reading;
extern volatile uint8_t drill_speed;
extern volatile uint32_t drill_rpm_pulses;
extern volatile uint32_t drill_actual_rpm;
extern volatile uint8_t layer_fan_duty;
extern volatile uint32_t layer_fan_rpm_pulses;
extern volatile uint32_t layer_fan_actual_rpm;
extern volatile uint8_t system_error_flag;
extern volatile uint8_t boot_sequence_active;
extern volatile uint8_t can_bus_error_flag;
extern volatile uint8_t laser_power_setpoint;
extern volatile uint32_t laser_last_kick_tick;
extern volatile uint8_t  weld_pulse_active;
extern volatile uint32_t weld_pulse_start_tick;
extern volatile uint16_t weld_pulse_duration_ms;
extern volatile uint32_t drill_last_kick_tick;
extern volatile uint32_t layer_fan_last_kick_tick;
extern volatile uint8_t hotend_fan_duty;
extern volatile uint32_t hotend_fan_rpm_pulses;
extern volatile uint32_t hotend_fan_actual_rpm;
extern volatile uint32_t hotend_fan_last_kick_tick;
extern volatile uint32_t solder_iron_last_kick_tick;
extern volatile uint32_t hotend_heater_last_kick_tick;
extern volatile uint8_t aoi_mode;
extern volatile uint16_t aoi_strobe_period;
extern volatile uint8_t aoi_strobe_active;
extern volatile uint32_t aoi_strobe_off_tick;
extern volatile uint8_t oled_night_mode;
extern volatile uint8_t oled_mode_pending_flag;
extern volatile uint8_t oled_night_mode_cmd;
extern volatile uint8_t update_led_ring_flag;
extern volatile uint8_t ring_color_r;
extern volatile uint8_t ring_color_g;
extern volatile uint8_t ring_color_b;
extern volatile uint8_t trigger_aoi_strobe_flag;
extern volatile uint32_t probe_impact_counter;
extern volatile uint8_t endstop_triggered;
extern volatile uint32_t can_led_tick;
extern volatile uint8_t led_host_override_active;
extern volatile uint32_t led_host_override_expire_tick;
extern uint8_t animation_frame; // toggles the tool icon's animation frame - written in main(), read by the render module

#define LED_HOST_OVERRIDE_DURATION_MS 10000
#define LED_FUNCTIONING_WINDOW_MS     1500

// String-building helpers (fully self-contained, declared here since
// they're used throughout the OLED/display module and nowhere else,
// but small enough not to need their own header)
uint8_t append_str(char *dest, const char *src);
uint8_t fmt_uint(char *dest, uint32_t value, uint8_t width);

// Defined in firmware_init.c - called from firmware_oled_driver.c's
// OLED_Init(), which needs I2C2 configured before it can probe the display.
void MX_I2C2_Init(void);

// The current incoming CAN message - populated by HAL_CAN_GetRxMessage in
// the main dispatcher (see firmware_can_dispatch.c) before any per-tool
// handler function is called. Genuinely global, not per-tool state: every
// firmware_can_*.c handler reads these to see what was just received.
extern CAN_RxHeaderTypeDef rxHeader;
extern uint8_t rxData[8];

// Defined in firmware_persistence.c (F-RAM read/write) - declared here
// since firmware_can_global_post.c calls FRAM_WriteBytes directly for the
// 0x192 erase command.
uint8_t FRAM_WriteBytes(uint16_t addr, const uint8_t *data, uint16_t len);
uint8_t FRAM_ReadBytes(uint16_t addr, uint8_t *data, uint16_t len);
uint8_t SavedState_Checksum(const SavedState_t *s);

#endif // FIRMWARE_COMMON_H
