// =============================================================================
// URTC Firmware - Per-tool telemetry (RPM) and communication watchdogs
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_telemetry_watchdog.h"

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
