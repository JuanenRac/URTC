// =============================================================================
// URTC Firmware - Host logic tests for pid_control.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The PID (anti-windup, derivative on the measurement) and the relay autotune, driven against a
// simulated heater: a first-order thermal mass with losses and a sensor lag, roughly a T12 tip.
// No hardware is involved; the real tip has not been used to check these numbers.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "pid_control.h"

typedef struct {
    double temp, sensor;
    double ambient, power_w, loss_w_per_k, capacity_j_per_k, sensor_tau_s;
} Plant;

static void plant_init(Plant *p)
{
    p->ambient = 25.0;
    p->temp = p->sensor = p->ambient;
    p->power_w = 60.0;
    p->loss_w_per_k = 0.126;     // steady state at full power: 25 + 60 / 0.126 = about 500 C
    p->capacity_j_per_k = 1.2;   // about 50 C per second at full power when cold
    p->sensor_tau_s = 0.3;
}

static void plant_step(Plant *p, double duty_percent, double dt)
{
    double heat = p->power_w * duty_percent / 100.0;
    p->temp += dt * (heat - p->loss_w_per_k * (p->temp - p->ambient)) / p->capacity_j_per_k;
    p->sensor += dt * (p->temp - p->sensor) / p->sensor_tau_s;
}

static void test_limits_and_antiwindup(int *failures)
{
    Pid_t pid;
    Pid_Init(&pid, 8.0f, 0.2f, 0.0f, 0.0f, 100.0f);
    TEST_ASSERT(Pid_Update(&pid, 300.0f, 25.0f, 0.02f) == 100.0f, "a large error saturates at the top");
    TEST_ASSERT(Pid_Update(&pid, 25.0f, 300.0f, 0.02f) == 0.0f, "a negative error saturates at the bottom");
    TEST_ASSERT(Pid_Update(&pid, 100.0f, 100.0f, 0.0f) >= 0.0f, "a zero time step does not divide by zero");

    // 60 seconds of saturation must not wind the integral up
    Pid_Reset(&pid);
    for (int i = 0; i < 3000; i++) Pid_Update(&pid, 400.0f, 25.0f, 0.02f);
    TEST_ASSERT(pid.integral <= 100.0f && pid.integral >= -100.0f, "the integral stays inside the output range while saturated");

    // and the output leaves the limit as soon as the error changes sign
    int steps_to_leave = 0;
    for (; steps_to_leave < 500; steps_to_leave++) {
        if (Pid_Update(&pid, 400.0f, 420.0f, 0.02f) < 100.0f) break;
    }
    TEST_ASSERT(steps_to_leave < 5, "no windup: the output leaves the limit at once after the error changes sign");

    // derivative acts on the measurement: a step of the setpoint does not kick the output
    Pid_t d;
    Pid_Init(&d, 0.0f, 0.0f, 10.0f, -1000.0f, 1000.0f);
    Pid_Update(&d, 100.0f, 100.0f, 0.02f);
    float out = Pid_Update(&d, 500.0f, 100.0f, 0.02f);
    TEST_ASSERT(out == 0.0f, "derivative on the measurement: a setpoint step gives no kick");
    out = Pid_Update(&d, 500.0f, 110.0f, 0.02f);
    TEST_ASSERT(out < 0.0f, "a rising measurement gives a braking derivative");
}

static void test_closed_loop(int *failures)
{
    Plant plant;
    Pid_t pid;
    plant_init(&plant);
    Pid_Init(&pid, 6.0f, 0.15f, 12.0f, 0.0f, 100.0f);
    double dt = 0.02, max_t = 0;
    for (int i = 0; i < 100 * 50; i++) {
        double duty = Pid_Update(&pid, 320.0f, (float)plant.sensor, (float)dt);
        plant_step(&plant, duty, dt);
        if (plant.temp > max_t) max_t = plant.temp;
    }
    TEST_ASSERT(fabs(plant.temp - 320.0) < 3.0, "closed loop: the tip settles within 3 C of the setpoint");
    TEST_ASSERT(max_t < 320.0 + 25.0, "closed loop: overshoot below 25 C");
    // a disturbance: the pad pulls heat out of the tip
    for (int i = 0; i < 100 * 20; i++) {
        double duty = Pid_Update(&pid, 320.0f, (float)plant.sensor, (float)dt);
        plant.ambient = 25.0;
        plant.loss_w_per_k = 0.15;
        plant_step(&plant, duty, dt);
    }
    TEST_ASSERT(fabs(plant.temp - 320.0) < 6.0, "closed loop: holds the setpoint while a pad draws heat");
}

static void test_autotune(int *failures)
{
    Plant plant;
    PidTune_t t;
    plant_init(&plant);
    PidTune_Start(&t, 320.0f, 2.0f, 100.0f, 0.0f, 900.0f);
    double dt = 0.02, time_s = 0;
    for (int i = 0; i < 100 * 600 && t.state == TUNE_RUNNING; i++) {
        double duty = PidTune_Update(&t, (float)plant.sensor, (float)dt);
        plant_step(&plant, duty, dt);
        time_s += dt;
    }
    TEST_ASSERT(t.state == TUNE_DONE, "the relay autotune finishes on a plant that oscillates");
    TEST_ASSERT(time_s < 400.0, "and does not take more than a few minutes");
    TEST_ASSERT(t.kp > 0.0f && t.ki > 0.0f && t.kd > 0.0f, "positive gains");
    TEST_ASSERT(t.ki < t.kp, "integral gain below the proportional gain (per second)");

    // the gains it found control the same plant
    Pid_t pid;
    plant_init(&plant);
    Pid_Init(&pid, t.kp, t.ki, t.kd, 0.0f, 100.0f);
    double max_t = 0;
    for (int i = 0; i < 100 * 120; i++) {
        double duty = Pid_Update(&pid, 320.0f, (float)plant.sensor, (float)dt);
        plant_step(&plant, duty, dt);
        if (plant.temp > max_t) max_t = plant.temp;
    }
    TEST_ASSERT(fabs(plant.temp - 320.0) < 3.0, "autotuned gains: the tip settles within 3 C");
    TEST_ASSERT(max_t < 320.0 + 40.0, "autotuned gains: overshoot below 40 C");

    // an abort leaves the relay off and no result
    PidTune_Start(&t, 320.0f, 2.0f, 100.0f, 0.0f, 900.0f);
    PidTune_Update(&t, 25.0f, 0.02f);
    PidTune_Abort(&t);
    TEST_ASSERT(t.state == TUNE_IDLE && PidTune_Update(&t, 25.0f, 0.02f) == 0.0f, "abort: idle, relay off");

    // a plant that never reaches the setpoint (a tip that is not there) makes it give up
    PidTune_Start(&t, 320.0f, 2.0f, 100.0f, 0.0f, 30.0f);
    for (int i = 0; i < 100 * 40 && t.state == TUNE_RUNNING; i++) PidTune_Update(&t, 25.0f, 0.02f);
    TEST_ASSERT(t.state == TUNE_FAILED, "no oscillation within the time limit: failed, not stuck");
    TEST_ASSERT(PidTune_Update(&t, 25.0f, 0.02f) == 0.0f, "failed: relay off");
}

void run_pid_control_tests(int *failures)
{
    test_limits_and_antiwindup(failures);
    test_closed_loop(failures);
    test_autotune(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - pid_control.c\n");
    run_pid_control_tests(&failures);
    if (failures == 0) {
        printf("  OK   all PID host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
