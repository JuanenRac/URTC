// =============================================================================
// URTC Firmware - Host logic tests for firmware_telemetry_watchdog.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The communication watchdogs (a command that has to be refreshed or the
// output is cut) and the RPM telemetry. firmware_telemetry_watchdog.c is
// compiled unchanged against the fake HAL in tests/host_hal/; the clock is the
// fake tick counter, so the exact timeout boundaries and the wrap-around of the
// 32-bit millisecond counter can be tested.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"
#include "firmware_telemetry_watchdog.h"

TIM_HandleTypeDef htim1, htim2, htim3;
volatile ToolMode_t active_tool;
volatile uint8_t system_error_flag;
volatile uint16_t target_temperature;
volatile uint8_t laser_power_setpoint, drill_speed, layer_fan_duty, uv_curing_duty, hotair_blower_duty, hotend_fan_duty;
volatile uint32_t laser_last_kick_tick, drill_last_kick_tick, solder_iron_last_kick_tick, hotend_heater_last_kick_tick;
volatile uint32_t layer_fan_last_kick_tick, uv_curing_last_kick_tick, hotair_blower_last_kick_tick, hotend_fan_last_kick_tick;
volatile uint32_t drill_rpm_pulses, drill_actual_rpm, layer_fan_rpm_pulses, layer_fan_actual_rpm, hotend_fan_rpm_pulses, hotend_fan_actual_rpm;

static void reset_all(void)
{
    fake_hal_reset_recording();
    memset(fake_tim_compare, 0x5A, sizeof fake_tim_compare);
    active_tool = TOOL_SOLDERING_IRON;
    system_error_flag = 0;
    target_temperature = 0;
    laser_power_setpoint = drill_speed = layer_fan_duty = uv_curing_duty = hotair_blower_duty = hotend_fan_duty = 0;
    laser_last_kick_tick = drill_last_kick_tick = solder_iron_last_kick_tick = hotend_heater_last_kick_tick = 0;
    layer_fan_last_kick_tick = uv_curing_last_kick_tick = hotair_blower_last_kick_tick = hotend_fan_last_kick_tick = 0;
    drill_rpm_pulses = drill_actual_rpm = layer_fan_rpm_pulses = layer_fan_actual_rpm = hotend_fan_rpm_pulses = hotend_fan_actual_rpm = 0;
    fake_tick = 0;
    fake_tick_step = 0;
}

// Runs `check` once at kick+timeout (must not trigger) and once at kick+timeout+1 (must trigger).
typedef struct {
    const char *name;
    ToolMode_t tool;
    uint32_t timeout_ms;
    void (*arm)(void);          // sets the output on and the kick tick to fake_tick
    void (*run)(void);
    int (*is_off)(void);        // 1 when the output has been cut
    int cuts_pwm;
    int forces_error;
} WatchdogCase;

static void arm_laser(void) { laser_power_setpoint = 80; laser_last_kick_tick = fake_tick; }
static int laser_off(void) { return laser_power_setpoint == 0; }
static void arm_drill(void) { drill_speed = 120; drill_last_kick_tick = fake_tick; }
static int drill_off(void) { return drill_speed == 0; }
static void arm_solder(void) { target_temperature = 350; solder_iron_last_kick_tick = fake_tick; }
static int heater_off(void) { return target_temperature == 0; }
static void arm_hotend(void) { target_temperature = 210; hotend_heater_last_kick_tick = fake_tick; }
static void arm_layer(void) { layer_fan_duty = 90; layer_fan_last_kick_tick = fake_tick; }
static int layer_off(void) { return layer_fan_duty == 0; }
static void arm_uv(void) { uv_curing_duty = 200; uv_curing_last_kick_tick = fake_tick; }
static int uv_off(void) { return uv_curing_duty == 0; }
static void arm_blower(void) { hotair_blower_duty = 60; hotair_blower_last_kick_tick = fake_tick; }
static int blower_off(void) { return hotair_blower_duty == 0; }

static const WatchdogCase CASES[] = {
    { "laser",              TOOL_LASER_ENGRAVER,  250,  arm_laser,  Watchdog_Safety_Laser,        laser_off,  1, 1 },
    { "drill",              TOOL_DRILL,           250,  arm_drill,  Watchdog_Safety_Drill,        drill_off,  1, 1 },
    { "soldering iron",     TOOL_SOLDERING_IRON,  250,  arm_solder, Watchdog_Safety_SolderIron,   heater_off, 0, 1 },
    { "hot air heater",     TOOL_HOTAIR_REWORK,   250,  arm_solder, Watchdog_Safety_SolderIron,   heater_off, 0, 1 },
    { "hotend heater",      TOOL_3D_PRINTER,      250,  arm_hotend, Watchdog_Safety_HotendHeater, heater_off, 0, 1 },
    { "layer fan",          TOOL_3D_PRINTER,      1000, arm_layer,  Watchdog_Safety_LayerFan,     layer_off,  1, 1 },
    { "UV lamp",            TOOL_UV_CURING,       250,  arm_uv,     Watchdog_Safety_UVCuring,     uv_off,     1, 1 },
    { "hot air blower",     TOOL_HOTAIR_REWORK,   1000, arm_blower, Watchdog_Safety_HotAirBlower, blower_off, 1, 1 },
};

static void test_cases(int *failures)
{
    char label[160];
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        const WatchdogCase *c = &CASES[i];

        // Refreshed in time: exactly at the timeout the output stays on (the test is "older than").
        reset_all();
        active_tool = c->tool;
        fake_tick = 5000;
        c->arm();
        fake_tick = 5000 + c->timeout_ms;
        c->run();
        snprintf(label, sizeof label, "%s watchdog: output still on exactly at the timeout", c->name);
        TEST_ASSERT(!c->is_off() && !system_error_flag, label);

        // One millisecond later: cut, PWM forced to 0, error latched.
        fake_tick = 5000 + c->timeout_ms + 1;
        memset(fake_tim_compare, 0x5A, sizeof fake_tim_compare);
        c->run();
        snprintf(label, sizeof label, "%s watchdog: output cut one millisecond after the timeout", c->name);
        TEST_ASSERT(c->is_off(), label);
        snprintf(label, sizeof label, "%s watchdog: a critical error is latched", c->name);
        TEST_ASSERT(system_error_flag == 1, label);
        if (c->cuts_pwm) {
            snprintf(label, sizeof label, "%s watchdog: the PWM compare is forced to 0", c->name);
            TEST_ASSERT(fake_tim_compare[0] == 0, label);
        }

        // Another tool active: the watchdog of this family does nothing.
        reset_all();
        active_tool = TOOL_ELECTROMAGNET;
        fake_tick = 100;
        c->arm();
        fake_tick = 100000;
        c->run();
        snprintf(label, sizeof label, "%s watchdog: ignored while another tool is active", c->name);
        TEST_ASSERT(!system_error_flag, label);

        // Nothing running: a stale kick tick alone is not a fault.
        reset_all();
        active_tool = c->tool;
        fake_tick = 1000000;
        c->run();
        snprintf(label, sizeof label, "%s watchdog: nothing to cut when the output is already off", c->name);
        TEST_ASSERT(!system_error_flag, label);

        // Wrap-around of the 32-bit millisecond counter: elapsed time stays correct.
        reset_all();
        active_tool = c->tool;
        fake_tick = 0xFFFFFFF0u;
        c->arm();
        fake_tick = 0xFFFFFFF0u + c->timeout_ms;      // wraps past zero for every timeout
        c->run();
        snprintf(label, sizeof label, "%s watchdog: no false trip across the tick wrap-around", c->name);
        TEST_ASSERT(!system_error_flag && !c->is_off(), label);
        fake_tick = 0xFFFFFFF0u + c->timeout_ms + 1;
        c->run();
        snprintf(label, sizeof label, "%s watchdog: still trips right after the wrap-around", c->name);
        TEST_ASSERT(system_error_flag == 1 && c->is_off(), label);
    }
}

static void test_shared_families(int *failures)
{
    // The soldering-iron watchdog serves two tools but not the 3D printer; the hotend one serves only the printer.
    reset_all();
    active_tool = TOOL_3D_PRINTER;
    target_temperature = 200; solder_iron_last_kick_tick = 0; hotend_heater_last_kick_tick = 1000000;
    fake_tick = 1000100;
    Watchdog_Safety_SolderIron();
    TEST_ASSERT(target_temperature == 200 && !system_error_flag, "iron watchdog does not act on the printer's heater");
    Watchdog_Safety_HotendHeater();
    TEST_ASSERT(target_temperature == 200 && !system_error_flag, "hotend watchdog is satisfied by its own recent kick");

    reset_all();
    active_tool = TOOL_HOTAIR_REWORK;
    hotair_blower_duty = 50; hotair_blower_last_kick_tick = 0;
    target_temperature = 300; solder_iron_last_kick_tick = 0;
    fake_tick = 500;
    Watchdog_Safety_HotAirBlower();
    TEST_ASSERT(hotair_blower_duty == 50 && !system_error_flag, "blower is allowed 1000 ms (500 ms is fine)");
    Watchdog_Safety_SolderIron();
    TEST_ASSERT(target_temperature == 0 && system_error_flag, "the hot-air heater of the same tool is held to 250 ms");
}

static void test_hotend_fan_stall(int *failures)
{
    reset_all();
    active_tool = TOOL_3D_PRINTER;
    hotend_fan_duty = 5;
    fake_tick = 10;
    Watchdog_Safety_HotendFan();   // low duty: the stall clock is held at "now"
    hotend_fan_duty = 100;
    hotend_fan_actual_rpm = 0;
    fake_tick = 3010;
    Watchdog_Safety_HotendFan();
    TEST_ASSERT(!system_error_flag, "fan at 100% with no rpm for exactly 3000 ms is not yet a fault");
    fake_tick = 3011;
    Watchdog_Safety_HotendFan();
    TEST_ASSERT(system_error_flag == 1, "fan at 100% with no rpm for more than 3000 ms latches the error");

    reset_all();
    active_tool = TOOL_3D_PRINTER;
    hotend_fan_duty = 100;
    hotend_fan_actual_rpm = 4000;
    fake_tick = 100000;
    Watchdog_Safety_HotendFan();   // spinning: the clock restarts
    hotend_fan_actual_rpm = 0;
    fake_tick = 102999;
    Watchdog_Safety_HotendFan();
    TEST_ASSERT(!system_error_flag, "a fan that just stopped has 3 s before it counts as stalled");
    fake_tick = 103001;
    Watchdog_Safety_HotendFan();
    TEST_ASSERT(system_error_flag == 1, "a fan that stopped for more than 3 s latches the error");

    reset_all();
    active_tool = TOOL_3D_PRINTER;
    hotend_fan_duty = 10;          // at or below 10 % the fan is considered off
    fake_tick = 50000;
    Watchdog_Safety_HotendFan();
    fake_tick = 90000;
    Watchdog_Safety_HotendFan();
    TEST_ASSERT(!system_error_flag, "a fan commanded at 10 % or less is never reported as stalled");

    reset_all();
    active_tool = TOOL_DRILL;
    hotend_fan_duty = 100;
    fake_tick = 99999;
    Watchdog_Safety_HotendFan();
    TEST_ASSERT(!system_error_flag, "the fan stall check only applies to the 3D printer");
}

static void test_rpm_telemetry(int *failures)
{
    reset_all();
    active_tool = TOOL_DRILL;
    drill_rpm_pulses = 12;
    Telemetry_Drill();
    TEST_ASSERT(drill_actual_rpm == 4800 && drill_rpm_pulses == 0, "drill rpm = pulses x 400, and the counter is taken (cleared)");
    drill_rpm_pulses = 1000;
    Telemetry_Drill();
    TEST_ASSERT(drill_actual_rpm == 9999, "drill rpm is capped at 9999");
    active_tool = TOOL_SOLDERING_IRON;
    drill_rpm_pulses = 5;
    Telemetry_Drill();
    TEST_ASSERT(drill_rpm_pulses == 5, "drill telemetry is left alone when another tool is active");

    active_tool = TOOL_3D_PRINTER;
    layer_fan_rpm_pulses = 7;
    Telemetry_LayerFan();
    TEST_ASSERT(layer_fan_actual_rpm == 1400 && layer_fan_rpm_pulses == 0, "layer fan rpm = pulses x 200");
    layer_fan_rpm_pulses = 500;
    Telemetry_LayerFan();
    TEST_ASSERT(layer_fan_actual_rpm == 9999, "layer fan rpm is capped at 9999");
    hotend_fan_rpm_pulses = 21;
    Telemetry_HotendFan();
    TEST_ASSERT(hotend_fan_actual_rpm == 4200 && hotend_fan_rpm_pulses == 0, "hotend fan rpm = pulses x 200");
    hotend_fan_rpm_pulses = 60;
    Telemetry_HotendFan();
    TEST_ASSERT(hotend_fan_actual_rpm == 9999, "hotend fan rpm is capped at 9999");
    active_tool = TOOL_DRILL;
    layer_fan_rpm_pulses = 3; hotend_fan_rpm_pulses = 3;
    Telemetry_LayerFan();
    Telemetry_HotendFan();
    TEST_ASSERT(layer_fan_rpm_pulses == 3 && hotend_fan_rpm_pulses == 3, "fan telemetry only runs for the 3D printer");
}

void run_telemetry_watchdog_tests(int *failures)
{
    test_cases(failures);
    test_shared_families(failures);
    test_hotend_fan_stall(failures);
    test_rpm_telemetry(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - firmware_telemetry_watchdog.c\n");
    run_telemetry_watchdog_tests(&failures);
    if (failures == 0) {
        printf("  OK   all telemetry and watchdog host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
