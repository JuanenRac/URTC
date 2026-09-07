// =============================================================================
// URTC Firmware - Generic sensor and endstop telemetry
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_control_sensors.h"

void Control_SensorTelemetry(void) {
    if (active_tool != TOOL_VACUUM_PICKUP && active_tool != TOOL_FLYING_PROBE) return;
    
    // TCRT5000 analog phototransistor reading on Channel 11 (PB0). 2ms
    // timeout, matching the same shared ADC's own soldering-iron read
    // elsewhere in this firmware - a healthy conversion completes in
    // microseconds, so this only ever actually waits the full duration
    // when something's already wrong.
    HAL_ADC_Start(&hadc);
    if(HAL_ADC_PollForConversion(&hadc, 2) == HAL_OK) {
        sensor_analog_reading = HAL_ADC_GetValue(&hadc);
    } else {
        // Same reasoning as the thermal ADC reads in
        // firmware_control_thermal.c: a conversion that can't complete
        // within this 2ms budget is untrustworthy, and leaving
        // sensor_analog_reading frozen at its last value with no
        // indication anything's wrong would let a vacuum-pickup/Flying
        // Probe reading go stale silently.
        system_error_flag = 1;
    }
    HAL_ADC_Stop(&hadc);
    
    // LM393 digital input on PB3
    sensor_digital_reading = HAL_GPIO_ReadPin(GPIOB, TCRT_D0_DIG_PIN);
}

// Generic endstop/limit switch, active low. Shares PB3 with the tools
// above, but only makes sense for the 3 that don't already use that pin for
// something else - reconfigured as a plain input for exactly these 3 in main().
// TOOL_SOLDERING_IRON used to be a 4th tool here - removed once its own
// PB3 became the solder-wire-feeder's STEP output instead (doc #16); see
// CANBUS.TXT's own 0x135 note for the full reasoning.
void Control_EndstopTelemetry(void) {
    if (active_tool != TOOL_DRILL &&
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

