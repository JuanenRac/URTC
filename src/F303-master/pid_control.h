// =============================================================================
// URTC Firmware - Heater PID with anti-windup and relay autotune (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// No HAL, no timer, no global: the caller passes the time step. The glue that feeds it the
// thermocouple reading of the soldering iron and turns the output into heater on-time is in
// firmware_thermal_pid.c, behind the THERMAL_PID switch.
//
//  * The derivative acts on the measurement (a change of the setpoint does not kick the output)
//    through a first-order filter.
//  * Anti-windup: the integral stops growing while the output is at a limit and the error
//    pushes it further, and is clamped to what the output range can use.
//  * Relay autotune (Astrom-Hagglund): the output switches between two levels around the
//    setpoint; the amplitude and period of the oscillation give the ultimate gain and period,
//    and the gains follow the Tyreus-Luyben rules (gentle, little overshoot, suited to a heater).
#ifndef PID_CONTROL_H
#define PID_CONTROL_H

#include <stdint.h>

typedef struct {
    float kp, ki, kd;        // ki per second, kd in seconds
    float out_min, out_max;
    float deriv_tau_s;       // time constant of the derivative filter
    float integral;
    float prev_meas;
    float d_filtered;
    uint8_t primed;          // 0 until the first update (no derivative on the first sample)
} Pid_t;

void  Pid_Init(Pid_t *p, float kp, float ki, float kd, float out_min, float out_max);
void  Pid_Reset(Pid_t *p);
// One control step; returns the output within [out_min, out_max].
float Pid_Update(Pid_t *p, float setpoint, float measurement, float dt_s);

#define TUNE_IDLE     0u
#define TUNE_RUNNING  1u
#define TUNE_DONE     2u
#define TUNE_FAILED   3u

#define TUNE_CYCLES_DISCARDED 1u   // the first full oscillation carries the start-up transient
#define TUNE_CYCLES_MEASURED  4u   // full oscillations that are averaged

typedef struct {
    uint8_t state;
    float setpoint, hysteresis;
    float out_high, out_low;
    float max_time_s, elapsed_s;
    uint8_t output_high;           // the relay level in force
    float peak_max, peak_min;      // extremes of the current oscillation
    float last_cross_s;            // time of the last rising crossing
    uint8_t cycles;                // full oscillations seen
    uint8_t started_cycle;         // 0 until the first rising crossing has been seen
    float sum_amplitude, sum_period;
    float kp, ki, kd;              // the result, valid in TUNE_DONE
} PidTune_t;

void  PidTune_Start(PidTune_t *t, float setpoint, float hysteresis, float out_high, float out_low, float max_time_s);
void  PidTune_Abort(PidTune_t *t);
// Feed one measurement; returns the relay output to apply. Moves to TUNE_DONE or TUNE_FAILED by itself.
float PidTune_Update(PidTune_t *t, float measurement, float dt_s);

#endif // PID_CONTROL_H
