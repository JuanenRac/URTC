// =============================================================================
// URTC Firmware - Heater PID with anti-windup and relay autotune (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "pid_control.h"

#define PI_F 3.14159265f

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Pid_Init(Pid_t *p, float kp, float ki, float kd, float out_min, float out_max) {
    p->kp = kp; p->ki = ki; p->kd = kd;
    p->out_min = out_min; p->out_max = out_max;
    p->deriv_tau_s = 0.5f;
    Pid_Reset(p);
}

void Pid_Reset(Pid_t *p) {
    p->integral = 0.0f;
    p->prev_meas = 0.0f;
    p->d_filtered = 0.0f;
    p->primed = 0;
}

float Pid_Update(Pid_t *p, float setpoint, float measurement, float dt_s) {
    float error = setpoint - measurement;
    if (dt_s <= 0.0f) return clampf(p->kp * error + p->integral, p->out_min, p->out_max);
    if (!p->primed) {
        p->prev_meas = measurement;
        p->d_filtered = 0.0f;
        p->primed = 1;
    }
    float raw_d = -(measurement - p->prev_meas) / dt_s;                 // derivative of the measurement
    float alpha = dt_s / (p->deriv_tau_s + dt_s);
    p->d_filtered += alpha * (raw_d - p->d_filtered);
    p->prev_meas = measurement;

    float p_term = p->kp * error;
    float d_term = p->kd * p->d_filtered;
    float unsat = p_term + p->integral + d_term;
    float out = clampf(unsat, p->out_min, p->out_max);

    // Conditional integration: do not accumulate while the output is saturated and the error pushes the same way.
    int pushing_out = (unsat > p->out_max && error > 0.0f) || (unsat < p->out_min && error < 0.0f);
    if (!pushing_out) {
        p->integral += p->ki * error * dt_s;
        // The integral never needs to hold more than the whole output range.
        p->integral = clampf(p->integral, p->out_min - p->out_max, p->out_max - p->out_min);
    }
    return out;
}

void PidTune_Start(PidTune_t *t, float setpoint, float hysteresis, float out_high, float out_low, float max_time_s) {
    t->state = TUNE_RUNNING;
    t->setpoint = setpoint;
    t->hysteresis = hysteresis > 0.0f ? hysteresis : 1.0f;
    t->out_high = out_high;
    t->out_low = out_low;
    t->max_time_s = max_time_s;
    t->elapsed_s = 0.0f;
    t->output_high = 1;
    t->peak_max = -1.0e30f;
    t->peak_min = 1.0e30f;
    t->last_cross_s = 0.0f;
    t->cycles = 0;
    t->started_cycle = 0;
    t->sum_amplitude = 0.0f;
    t->sum_period = 0.0f;
    t->kp = t->ki = t->kd = 0.0f;
}

void PidTune_Abort(PidTune_t *t) {
    if (t->state == TUNE_RUNNING) t->state = TUNE_IDLE;
}

static float finish(PidTune_t *t) {
    float n = (float)TUNE_CYCLES_MEASURED;
    float amp = t->sum_amplitude / n;
    float tu = t->sum_period / n;
    float d = (t->out_high - t->out_low) / 2.0f;
    if (amp <= 0.0f || tu <= 0.0f || d <= 0.0f) {
        t->state = TUNE_FAILED;
        return t->out_low;
    }
    float ku = 4.0f * d / (PI_F * amp);
    // Tyreus-Luyben: Kp = Ku / 2.2, Ti = 2.2 Tu, Td = Tu / 6.3
    t->kp = ku / 2.2f;
    t->ki = t->kp / (2.2f * tu);
    t->kd = t->kp * (tu / 6.3f);
    t->state = TUNE_DONE;
    return t->out_low;
}

float PidTune_Update(PidTune_t *t, float measurement, float dt_s) {
    if (t->state != TUNE_RUNNING) return t->out_low;
    t->elapsed_s += dt_s;
    if (t->elapsed_s > t->max_time_s) {
        t->state = TUNE_FAILED;
        return t->out_low;
    }
    if (measurement > t->peak_max) t->peak_max = measurement;
    if (measurement < t->peak_min) t->peak_min = measurement;

    if (t->output_high && measurement > t->setpoint + t->hysteresis) {
        t->output_high = 0;
    } else if (!t->output_high && measurement < t->setpoint - t->hysteresis) {
        t->output_high = 1;
        // A rising crossing closes one full oscillation: measure the one that has just ended.
        if (t->started_cycle) {
            if (t->cycles >= TUNE_CYCLES_DISCARDED) {
                t->sum_amplitude += (t->peak_max - t->peak_min) / 2.0f;
                t->sum_period += t->elapsed_s - t->last_cross_s;
            }
            t->cycles++;
            if (t->cycles >= TUNE_CYCLES_DISCARDED + TUNE_CYCLES_MEASURED) return finish(t);
        }
        t->started_cycle = 1;
        t->last_cross_s = t->elapsed_s;
        t->peak_max = measurement;
        t->peak_min = measurement;
    }
    return t->output_high ? t->out_high : t->out_low;
}
