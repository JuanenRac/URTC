// =============================================================================
// URTC Firmware - Thermal PID control (soldering iron, 3D printer hotend)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_control_thermal.h"

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
