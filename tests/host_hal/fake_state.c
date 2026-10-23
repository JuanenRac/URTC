// =============================================================================
// URTC Firmware - the global state of the firmware for host-side tests: fake_state.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// firmware_common.h declares the peripheral handles and the shared state that "is defined once in STM32F303CC_main.c"; that file
// is the start-up code of the board and cannot be built on the host. This file defines the same names, all zero, so a test can link
// the real handler of a tool, the control loops or the recorder and read and write the state they share. A suite that needs only a few
// of them keeps defining its own (the older suites do); a suite that links this file defines none of them again.
#include "firmware_common.h"

CAN_HandleTypeDef hcan;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;
I2C_HandleTypeDef hi2c2;
ADC_HandleTypeDef hadc;
IWDG_HandleTypeDef hiwdg;
volatile ToolMode_t active_tool;
SavedState_t recovered_state;
uint8_t had_valid_recovered_state;
volatile uint8_t expansion_board_type;
volatile uint8_t free_tool_selection;
volatile uint8_t device_serial_number;
volatile uint8_t mlx_sensor_variant;
uint8_t raw_id_pin_value;
SavedState_t last_saved_state;
volatile LED_RGB_t led_state_pixel;
volatile LED_RGB_t ring_pixels[8];
volatile uint32_t steps_remaining;
volatile uint8_t step_pulse_high;
volatile uint32_t total_steps_setpoint;
volatile int32_t solder_spool_position;
volatile uint16_t current_temperature;
volatile uint16_t target_temperature;
volatile uint16_t sensor_analog_reading;
volatile uint8_t sensor_digital_reading;
volatile uint8_t drill_speed;
volatile uint32_t drill_rpm_pulses;
volatile uint32_t drill_actual_rpm;
volatile uint8_t layer_fan_duty;
volatile uint32_t layer_fan_rpm_pulses;
volatile uint32_t layer_fan_actual_rpm;
volatile uint8_t system_error_flag;
// weak: firmware_control_thermal.c defines the real one, and a suite that links it keeps that definition
__attribute__((weak)) volatile uint8_t thermal_fault_code;
volatile uint8_t last_reset_cause;
volatile uint8_t boot_sequence_active;
volatile uint8_t can_bus_error_flag;
volatile uint8_t laser_power_setpoint;
volatile uint32_t laser_last_kick_tick;
volatile uint8_t  weld_pulse_active;
volatile uint32_t weld_pulse_start_tick;
volatile uint16_t weld_pulse_duration_ms;
volatile uint32_t drill_last_kick_tick;
volatile uint32_t layer_fan_last_kick_tick;
volatile uint8_t hotend_fan_duty;
volatile uint32_t hotend_fan_rpm_pulses;
volatile uint32_t hotend_fan_actual_rpm;
volatile uint32_t hotend_fan_last_kick_tick;
volatile uint32_t solder_iron_last_kick_tick;
volatile uint32_t hotend_heater_last_kick_tick;
volatile uint8_t uv_curing_duty;
volatile uint32_t uv_curing_last_kick_tick;
volatile uint8_t hotair_blower_duty;
volatile uint32_t hotair_blower_last_kick_tick;
volatile uint8_t aoi_mode;
volatile uint16_t aoi_strobe_period;
volatile uint8_t aoi_strobe_active;
volatile uint32_t aoi_strobe_off_tick;
volatile uint8_t oled_night_mode;
volatile uint8_t oled_mode_pending_flag;
volatile uint8_t oled_night_mode_cmd;
volatile uint8_t update_led_ring_flag;
volatile uint8_t ring_color_r;
volatile uint8_t ring_color_g;
volatile uint8_t ring_color_b;
volatile uint8_t trigger_aoi_strobe_flag;
volatile uint32_t probe_impact_counter;
volatile uint8_t endstop_triggered;
volatile uint32_t can_led_tick;
volatile uint8_t led_host_override_active;
volatile uint32_t led_host_override_expire_tick;
uint8_t animation_frame;
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
