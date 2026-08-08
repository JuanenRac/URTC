// =============================================================================
// URTC Firmware - main() and boot sequence
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// This is the entry point of the partitioned build of the firmware -
// same behavior as the single-file STM32F303CC.C, split across
// several dozen .c/.h files for readability. Both are maintained side
// by side.
// =============================================================================
#include "firmware_common.h"
#include "firmware_init_clocks.h"
#include "firmware_init_gpio.h"
#include "firmware_init_can.h"
#include "firmware_init_timers.h"
#include "firmware_init_buses.h"
#include "firmware_expansion_spi.h"
#include "firmware_expansion_i2c.h"
#include "melexis_mlx90640/firmware_mlx90640_app.h"
#include "melexis_mlx90641/firmware_mlx90641_app.h"
#include "melexis_mlx90642/firmware_mlx90642_app.h"
#include "firmware_identify.h"
#include "firmware_oled_driver.h"
#include "firmware_render.h"
#include "firmware_led.h"
#include "firmware_control_thermal.h"
#include "firmware_control_sensors.h"
#include "firmware_telemetry_watchdog.h"
#include "firmware_can_weldpulse.h"
#include "firmware_persistence.h"

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
volatile uint8_t mlx_sensor_variant = MLX_VARIANT_NONE; // 0=no sensor
                                            // configured (safe default) -
                                            // restored from F-RAM at boot, updated
                                            // by 0x1A6 at runtime. Used both for this
                                            // board's own direct sensor support
                                            // (expansion_board_type==6, see
                                            // firmware_can_thermalinspection.c) and
                                            // relayed to the expansion slave chip's
                                            // own REG_MLX_SENSOR_VARIANT at boot on
                                            // the Advanced variants (see this file's
                                            // own init sequence below).
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
// hardware peripheral - see firmware_expansion_i2c.c's own note on why.
// No HAL handle needed for a bit-banged bus.
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
volatile uint8_t  weld_pulse_active = 0;
volatile uint32_t weld_pulse_start_tick = 0;
volatile uint16_t weld_pulse_duration_ms = 0;
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
uint8_t animation_frame = 0;               // 0/1 - toggles the tool icon's animation frame


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
    } else if (active_tool == TOOL_DRILL || active_tool == TOOL_LASER_ENGRAVER ||
               active_tool == TOOL_UV_CURING || active_tool == TOOL_HOTAIR_REWORK) {
        // UV Curing (doc #8) and Hot Air Rework's own blower (doc #10)
        // both share this same TIM1_CH1 channel at the drill/laser's
        // 20kHz rate - added here after discovering TOOL_UV_CURING's own
        // PWM handler (firmware_can_uvcuring.c) had already been written
        // assuming this init happened, when it never actually did for
        // that tool - this condition is what makes that handler's own
        // __HAL_TIM_SET_COMPARE calls meaningful instead of writing to a
        // timer nothing ever started.
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
               active_tool != TOOL_GRIPPER_NEMA && active_tool != TOOL_3D_PRINTER &&
               active_tool != TOOL_SMT_PICKPLACE && active_tool != TOOL_VACUUM_GRIPPER_LG) {
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
    // Expansion connector's bit-banged I2C bus - see
    // firmware_expansion_i2c.c's own comment for why this isn't a
    // hardware I2C peripheral either.
    MX_ExpansionI2C_Init();
    // Basic+MLX9064x expansion board's own sensor, read directly over
    // the bus just brought up above - only attempted when that specific
    // board variant is actually configured (expansion_board_type, just
    // loaded from F-RAM by SavedState_Load() above), same as every
    // other expansion-board-specific init in this project. Which of the
    // 3 supported sensors is actually populated is decided by
    // mlx_sensor_variant - MLX_VARIANT_NONE (the safe default)
    // deliberately initializes nothing at all, rather than guessing
    // which real sensor might be there.
    if (expansion_board_type == 6 && mlx_sensor_variant == MLX_VARIANT_90640) {
        MLX90640_Direct_Init();
    } else if (expansion_board_type == 6 && mlx_sensor_variant == MLX_VARIANT_90641) {
        MLX90641_Direct_Init();
    } else if (expansion_board_type == 6 && mlx_sensor_variant == MLX_VARIANT_90642) {
        MLX90642_Direct_Init();
    }
    // Advanced boards: tell the expansion slave chip which MLX9064x
    // variant it's actually got, since it has no persistent storage of
    // its own to remember this across its own resets - see
    // slave_common.h's own REG_MLX_SENSOR_VARIANT comment. Skipped
    // entirely on any other board variant, since there's no slave chip
    // to relay to.
    if (expansion_board_type == 3 || expansion_board_type == 4) {
        ExpansionI2C_SlaveWriteRegister(0x14, (uint8_t*)&mlx_sensor_variant, 1); // REG_MLX_SENSOR_VARIANT
    }
    // Expansion connector's bit-banged SPI bus - see the function's own
    // comment and PINOUT_CONNECTORS.TXT for why this isn't a hardware
    // SPI peripheral.
    MX_ExpansionSPI_Init();
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
            WeldPulse_Tick();
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
            // the moment (soldering iron, Hot Air Rework - which shares
            // this exact same thermal loop, see CANBUS.TXT's own 0x1E0 -
            // or 3D printer hotend), rather than on a flat timer
            // regardless of which tool is active.
            if (active_tool == TOOL_SOLDERING_IRON || active_tool == TOOL_HOTAIR_REWORK) {
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
            // 0x243: Flying Probe's own basic-mode reading (this tool's
            // own "CONN_SEN (ADC)" path) - same shared analog channel/ADC
            // as vacuum pickup's own 0x145 above, own dedicated CAN ID
            // rather than reusing 0x145 so a host can't mistake which
            // tool actually answered.
            else if (active_tool == TOOL_FLYING_PROBE) {
                txH.StdId = 0x243;
                txH.DLC = 2;
                txD[0] = (uint8_t)((sensor_analog_reading >> 8) & 0xFF);
                txD[1] = (uint8_t)(sensor_analog_reading & 0xFF);
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
