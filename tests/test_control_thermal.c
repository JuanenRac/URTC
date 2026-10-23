// =============================================================================
// URTC Firmware - Host logic tests for the heater control loops (firmware_control_thermal.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The loops that keep the soldering iron / hot air (T12 element, thermocouple on the ADC) and the 3D printer hotend (NTC) at their set-point,
// and cut the heater when anything looks wrong. The real firmware_control_thermal.c is compiled against the fake HAL, with the real thermal
// guard, thermal statistics, feature switches and PID behind it; the ADC returns the value a test sets. What is asserted: the mapping of a
// reading to a temperature, what an impossible reading does, the ceilings on the set-point, the hysteresis, the stuck-heater detection, the
// guard faults that latch the error and stop the heater, and that the thermal statistics hear of a fault. The loops keep static state, so
// every scenario runs in a child process that starts clean.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>

#include "firmware_common.h"
#include "firmware_control_thermal.h"
#include "firmware_features.h"
#include "firmware_thermal_stats.h"
#include "test_runner.h"
#include "thermal_guard.h"

// The bench functions of the feature module (internal sensors, oscillator trim) are not part of what this suite checks.
int16_t Features_PlatformMcuTempC(uint16_t *vdda_mv) { if (vdda_mv) *vdda_mv = 3300; return 25; }
uint8_t Features_PlatformHsiMeasure(uint32_t *expected, uint32_t *measured) { (void)expected; (void)measured; return 0; }
void Features_PlatformHsiWriteTrim(uint8_t trim) { (void)trim; }

#define PIN_HIGH GPIO_PIN_SET
#define PIN_LOW GPIO_PIN_RESET

static void world(ToolMode_t tool)
{
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    fake_fram_write_calls = 0;
    fake_tick = 1000;
    fake_tick_step = 0;
    fake_adc_value = 0;
    fake_adc_status = HAL_OK;
    active_tool = tool;
    system_error_flag = 0;
    thermal_fault_code = 0;
    target_temperature = 0;
    current_temperature = 0;
    fake_pin_set(T12_PWM_PORT, T12_PWM_PIN, PIN_LOW);
}

// The last level the heater pin was driven to since the last reset of the recording; -1 when it was not written.
static int heater_pin(void)
{
    int level = -1;
    for (int i = 0; i < fake_gpio_write_count; i++) {
        if (fake_gpio_writes[i].port == T12_PWM_PORT && fake_gpio_writes[i].pin == T12_PWM_PIN) {
            level = (int)fake_gpio_writes[i].state;
        }
    }
    return level;
}

static void solder_at(uint32_t adc)
{
    fake_adc_value = adc;
    fake_gpio_write_count = 0;
    Control_SolderingIron_PID();
}

static void hotend_at(uint32_t adc)
{
    fake_adc_value = adc;
    fake_gpio_write_count = 0;
    Control_3D_Hotend_PID();
}

// ---- the soldering iron and the hot air station ----------------------------------------------------------------------------------
static void other_tools(int *failures)
{
    world(TOOL_DRILL);
    target_temperature = 300;
    solder_at(1000);
    TEST_ASSERT(fake_gpio_write_count == 0 && fake_adc_starts == 0, "another tool: the loop does not read the ADC and does not touch the heater pin");
    hotend_at(1000);
    TEST_ASSERT(fake_gpio_write_count == 0 && fake_adc_starts == 0, "another tool: the hotend loop is idle too");
}

static void mapping(int *failures)
{
    world(TOOL_SOLDERING_IRON);
    solder_at(2048);
    TEST_ASSERT(current_temperature == 225, "an ADC reading of 2048 of 4095 is 225 C of the 450 C full scale");
    solder_at(1000);
    TEST_ASSERT(current_temperature == 110, "1000 is 110 C (rounded to the nearest degree)");
    solder_at(4000);
    TEST_ASSERT(current_temperature == 440, "4000 is 440 C");
    solder_at(16);
    TEST_ASSERT(current_temperature == 2, "the lowest valid reading (16) is 2 C");
    TEST_ASSERT(fake_adc_starts == 4 && fake_adc_stops == 4, "every conversion is started and stopped");
    TEST_ASSERT(system_error_flag == 0, "valid readings raise no error");
    world(TOOL_HOTAIR_REWORK);
    solder_at(2048);
    TEST_ASSERT(current_temperature == 225, "the hot air station uses the same loop");
}

static void impossible_readings(int *failures)
{
    const uint32_t bad[] = { 0, 15, 4090, 4095 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        world(TOOL_SOLDERING_IRON);
        target_temperature = 300;
        solder_at(bad[i]);
        TEST_ASSERT(system_error_flag == 1 && heater_pin() == PIN_LOW, "a reading of an open or shorted sensor latches the error and keeps the heater off");
        TEST_ASSERT(fake_adc_stops == 1, "and the conversion is stopped");
    }
    world(TOOL_SOLDERING_IRON);
    fake_adc_status = HAL_TIMEOUT;
    target_temperature = 300;
    solder_at(1000);
    TEST_ASSERT(system_error_flag == 1 && heater_pin() == PIN_LOW && current_temperature == 0, "a conversion that does not complete latches the error and the temperature is not updated");
}

static void ceilings(int *failures)
{
    world(TOOL_SOLDERING_IRON);
    target_temperature = 446;
    solder_at(1000);
    TEST_ASSERT(target_temperature == 0 && system_error_flag == 1 && heater_pin() == PIN_LOW, "a set-point above 445 C is wiped and latches the error");
    world(TOOL_SOLDERING_IRON);
    target_temperature = 445;
    solder_at(1000);
    TEST_ASSERT(target_temperature == 445 && system_error_flag == 0, "445 C itself is allowed");
    world(TOOL_SOLDERING_IRON);
    system_error_flag = 1;
    target_temperature = 300;
    solder_at(1000);
    TEST_ASSERT(heater_pin() == PIN_LOW, "with the error latched the heater is forced off on every pass");
}

static void hysteresis(int *failures)
{
    world(TOOL_SOLDERING_IRON);
    target_temperature = 300;
    solder_at(1000);   // 110 C, far below
    TEST_ASSERT(heater_pin() == PIN_HIGH, "far below the set-point the heater is on");
    solder_at(2730);   // 300 C exactly
    TEST_ASSERT(current_temperature == 300 && heater_pin() == PIN_HIGH, "at the set-point inside the band of two degrees it stays as it was (on)");
    solder_at(2745);   // 302 C: exactly target + 2, still inside
    TEST_ASSERT(current_temperature == 302 && heater_pin() == PIN_HIGH, "two degrees above: still on");
    solder_at(2770);   // 304 C
    TEST_ASSERT(current_temperature == 304 && heater_pin() == PIN_LOW, "more than two degrees above: off");
    solder_at(2730);   // back to 300
    TEST_ASSERT(heater_pin() == PIN_LOW, "inside the band it stays off");
    solder_at(2690);   // 296 C: below target - 2
    TEST_ASSERT(current_temperature == 296 && heater_pin() == PIN_HIGH, "more than two degrees below: on again");
}

static void stuck_heater(int *failures)
{
    world(TOOL_SOLDERING_IRON);
    target_temperature = 0;
    solder_at(910);             // 100 C, heater off: the timer is armed
    fake_tick = 1000 + 3000;
    solder_at(970);             // 107 C, exactly 3000 ms later: not yet "more than 3000"
    TEST_ASSERT(system_error_flag == 0, "3000 ms are not yet more than 3000 ms");
    fake_tick = 1000 + 3001;
    solder_at(970);
    TEST_ASSERT(system_error_flag == 1 && heater_pin() == PIN_LOW, "heating by more than 5 C for over 3 s with the heater off latches the error (a stuck element)");
}

static void stuck_heater_noise(int *failures)
{
    world(TOOL_SOLDERING_IRON);
    target_temperature = 0;
    solder_at(910);
    fake_tick = 1000 + 5000;
    solder_at(950);             // 104 C: +4
    TEST_ASSERT(system_error_flag == 0, "a rise of 5 C or less is ambient noise, not an error");
}

static void guard_no_rise(int *failures)
{
    world(TOOL_SOLDERING_IRON);
    target_temperature = 300;
    ThermalStats_LoadFromFram();
    fake_fram_write_calls = 0;
    for (uint32_t t = 0; t <= 19500; t += 500) {
        fake_tick = 1000 + t;
        solder_at(910);   // 100 C and never rising
    }
    TEST_ASSERT(system_error_flag == 0 && thermal_fault_code == 0, "before the 20 s window ends nothing is latched");
    fake_tick = 1000 + 20000;
    solder_at(910);
    TEST_ASSERT(thermal_fault_code == THERMAL_GUARD_HEATING_NO_RISE && system_error_flag == 1, "heating for 20 s without a rise latches the fault");
    TEST_ASSERT(target_temperature == 0 && heater_pin() == PIN_LOW, "the set-point is wiped and the heater is off");
    TEST_ASSERT(fake_fram_write_calls > 0, "the lifetime statistics were told and saved the fault");
    fake_tick += 500;
    solder_at(910);
    TEST_ASSERT(heater_pin() == PIN_LOW && system_error_flag == 1, "and it stays off");
}

static void guard_sag(int *failures)
{
    world(TOOL_SOLDERING_IRON);
    target_temperature = 300;
    fake_tick = 1000;
    solder_at(2730);   // 300 C: target reached
    // then the tip sags more than 40 C below the target while the heater is on, for 45 s
    for (uint32_t t = 500; t < 45000; t += 500) {
        fake_tick = 1000 + t;
        solder_at(2200);  // 241 C
    }
    TEST_ASSERT(system_error_flag == 0, "a sag shorter than 45 s is tolerated");
    fake_tick = 1000 + 45500;
    solder_at(2200);
    TEST_ASSERT(thermal_fault_code == THERMAL_GUARD_DROPPED_AT_TARGET && system_error_flag == 1 && heater_pin() == PIN_LOW, "a tip that reached the target and then stays 40 C below it for 45 s latches the fault");
}

static void changing_tool_resets_the_guard(int *failures)
{
    world(TOOL_SOLDERING_IRON);
    target_temperature = 300;
    for (uint32_t t = 0; t <= 15000; t += 500) {
        fake_tick = 1000 + t;
        solder_at(910);
    }
    active_tool = TOOL_DRILL;
    solder_at(910);   // the loop is idle and forgets the watch
    active_tool = TOOL_SOLDERING_IRON;
    for (uint32_t t = 15500; t <= 30000; t += 500) {
        fake_tick = 1000 + t;
        solder_at(910);
    }
    TEST_ASSERT(thermal_fault_code == 0 && system_error_flag == 0, "after another tool was active the 20 s watch starts again");
}

static void pid_instead_of_hysteresis(int *failures)
{
    world(TOOL_SOLDERING_IRON);
    FeatConfig_t config;
    Feat_ConfigBlank(&config);
    config.mask = FEAT_THERMAL_PID;
    Feat_ConfigSeal(&config);
    memcpy(&fake_fram[FEAT_CONFIG_FRAM_ADDR], &config, sizeof config);
    Features_Init();
    TEST_ASSERT(Features_Active(FEAT_THERMAL_PID), "the PID switch is on");
    target_temperature = 300;
    int on = 0;
    for (uint32_t t = 0; t < 2000; t += 20) {   // one PID window (1 s) and a bit
        fake_tick = 1000 + t;
        solder_at(1000);   // 110 C, far below: the output saturates at 100 percent
        on += heater_pin() == PIN_HIGH;
    }
    TEST_ASSERT(on > 80, "far below the set-point the PID drives the heater almost all the time");
    target_temperature = 0;
    fake_tick = 4000;
    solder_at(1000);
    TEST_ASSERT(heater_pin() == PIN_LOW, "with no set-point the PID output is zero");
}

// ---- the 3D printer hotend (NTC) ------------------------------------------------------------------------------------------------
static void ntc(int *failures)
{
    world(TOOL_3D_PRINTER);
    const struct { uint32_t raw; int celsius; } points[] = { { 116, 320 }, { 560, 200 }, { 1045, 160 }, { 2447, 100 }, { 3232, 70 }, { 4039, 0 } };
    for (unsigned i = 0; i < sizeof points / sizeof points[0]; i++) {
        hotend_at(points[i].raw);
        TEST_ASSERT(current_temperature == points[i].celsius, "a point of the thermistor table is read as its temperature");
    }
    hotend_at(20);
    TEST_ASSERT(current_temperature == 320, "below the first point of the table it stays at the first (hottest)");
    hotend_at(4080);
    TEST_ASSERT(current_temperature == 0, "above the last point it stays at the last (coldest)");
    uint16_t previous = 400;
    int monotonic = 1;
    for (uint32_t raw = 16; raw < 4090; raw++) {
        world(TOOL_3D_PRINTER);
        hotend_at(raw);
        monotonic = monotonic && current_temperature <= previous && current_temperature <= 320;
        previous = current_temperature;
    }
    TEST_ASSERT(monotonic, "a higher reading is never a higher temperature, over the whole valid range");
}

static void hotend_impossible_readings(int *failures)
{
    const uint32_t bad[] = { 0, 15, 4090, 4095 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        world(TOOL_3D_PRINTER);
        target_temperature = 200;
        hotend_at(bad[i]);
        TEST_ASSERT(system_error_flag == 1 && heater_pin() == PIN_LOW, "an open or shorted thermistor latches the error and keeps the heater off");
    }
    world(TOOL_3D_PRINTER);
    fake_adc_status = HAL_ERROR;
    target_temperature = 200;
    hotend_at(1000);
    TEST_ASSERT(system_error_flag == 1, "a conversion that fails latches the error");
}

static void hotend_ceiling_and_hysteresis(int *failures)
{
    world(TOOL_3D_PRINTER);
    target_temperature = 301;
    hotend_at(1045);
    TEST_ASSERT(target_temperature == 0 && system_error_flag == 1 && heater_pin() == PIN_LOW, "a set-point above 300 C is wiped and latches the error");

    world(TOOL_3D_PRINTER);
    target_temperature = 200;
    hotend_at(1045);   // 160 C
    TEST_ASSERT(heater_pin() == PIN_HIGH, "below the set-point the hotend heats");
    hotend_at(560);    // 200 C
    TEST_ASSERT(heater_pin() == PIN_HIGH, "inside the band it keeps heating");
    hotend_at(482);    // 210 C
    TEST_ASSERT(heater_pin() == PIN_LOW, "above the band it cuts");
}

static void hotend_max_temperature(int *failures)
{
    world(TOOL_3D_PRINTER);
    target_temperature = 200;
    hotend_at(116);    // 320 C: above the hard ceiling of 310 C
    TEST_ASSERT(thermal_fault_code == THERMAL_GUARD_MAXTEMP && system_error_flag == 1 && target_temperature == 0 && heater_pin() == PIN_LOW, "above 310 C the guard latches the fault whatever the set-point");
}

static void hotend_stuck(int *failures)
{
    world(TOOL_3D_PRINTER);
    target_temperature = 0;
    hotend_at(1045);   // 160 C, heater off, timer armed
    fake_tick = 1000 + 3001;
    hotend_at(870);    // 172 C: a rise of more than 5 C with the heater off
    TEST_ASSERT(system_error_flag == 1, "a hotend that heats up by itself for over 3 s latches the error");
}

static void hotend_no_rise(int *failures)
{
    world(TOOL_3D_PRINTER);
    target_temperature = 250;
    for (uint32_t t = 0; t <= 40000; t += 1000) {
        fake_tick = 1000 + t;
        hotend_at(1045);   // 160 C and not rising
    }
    TEST_ASSERT(thermal_fault_code == THERMAL_GUARD_HEATING_NO_RISE, "the hotend watch gives up after 40 s without a rise of 2 C");
}

static int isolated(void (*scenario)(int *), const char *name)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        int fails = 0;
        scenario(&fails);
        fflush(stdout);
        exit(fails > 100 ? 100 : fails);  // exit(), not _exit(): the coverage counters of the child are written by the exit handlers
    }
    int status = 0;
    waitpid(pid, &status, 0);
    int bad = WIFEXITED(status) ? WEXITSTATUS(status) : 101;
    printf("  %s  %s\n", bad == 0 ? "ok  " : "FAIL", name);
    fflush(stdout);
    return bad;
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the heater control loops\n");
    fflush(stdout);
    failures += isolated(other_tools, "another tool leaves the heater alone");
    failures += isolated(mapping, "a reading is mapped to the temperature");
    failures += isolated(impossible_readings, "an impossible reading latches the error");
    failures += isolated(ceilings, "the ceilings on the set-point");
    failures += isolated(hysteresis, "the hysteresis of two degrees");
    failures += isolated(stuck_heater, "a heater that is off but heating");
    failures += isolated(stuck_heater_noise, "a small rise with the heater off is only noise");
    failures += isolated(guard_no_rise, "the guard: heating without a rise");
    failures += isolated(guard_sag, "the guard: dropped at the target");
    failures += isolated(changing_tool_resets_the_guard, "changing tool resets the watch");
    failures += isolated(pid_instead_of_hysteresis, "the PID replaces the hysteresis when its switch is on");
    failures += isolated(ntc, "the thermistor table");
    failures += isolated(hotend_impossible_readings, "the hotend: an impossible reading");
    failures += isolated(hotend_ceiling_and_hysteresis, "the hotend: the ceiling and the hysteresis");
    failures += isolated(hotend_max_temperature, "the hotend: above 310 C");
    failures += isolated(hotend_stuck, "the hotend: heating with the heater off");
    failures += isolated(hotend_no_rise, "the hotend: no rise");
    if (failures == 0) {
        printf("  OK   all heater control loop host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
