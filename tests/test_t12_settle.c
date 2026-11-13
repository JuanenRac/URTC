// =============================================================================
// URTC Firmware - Host logic tests for the settling time of the T12 reading (t12_settle.h)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The wait between switching the T12 heater off and starting the conversion is a build option. Here: the default is the 10 microseconds of the first version, the
// conversion to passes of the counting loop is exact and monotonic, the maximum holds, and the control pass still reads the thermocouple with the heater off whatever
// the wait is (the suite is built twice: with the default and with 2000 microseconds). Evidence: simulator; the right value is measured with an oscilloscope.
#include <stdint.h>
#include <stdio.h>

#include "firmware_common.h"
#include "firmware_control_thermal.h"
#include "t12_settle.h"
#include "test_runner.h"

void Control_SolderingIron_PID(void);

static int first_write_is_low(int from)
{
    int level = -1;
    for (int i = from; i < fake_gpio_write_count; i++) if (fake_gpio_writes[i].port == T12_PWM_PORT && fake_gpio_writes[i].pin == T12_PWM_PIN) { level = (int)fake_gpio_writes[i].state; break; }
    return level == (int)GPIO_PIN_RESET;
}

static void test_settle(int *failures)
{
    TEST_ASSERT(urtc_t12_settle_loops(10) == 640u, "10 microseconds are the 640 passes the firmware has always used");
    TEST_ASSERT(urtc_t12_settle_loops(0) == 0u && urtc_t12_settle_loops(1) == URTC_T12_LOOPS_PER_US, "no wait is no pass, one microsecond is one step");
    uint32_t previous = 0;
    for (uint32_t us = 0; us <= URTC_T12_SETTLE_MAX_US; us += 7u) {
        uint32_t loops = urtc_t12_settle_loops(us);
        if (loops < previous || loops != us * URTC_T12_LOOPS_PER_US) { TEST_ASSERT(0, "the passes grow with the wait, exactly"); break; }
        previous = loops;
    }
    TEST_ASSERT(urtc_t12_settle_loops(URTC_T12_SETTLE_MAX_US + 1u) == urtc_t12_settle_loops(URTC_T12_SETTLE_MAX_US) && urtc_t12_settle_loops(0xFFFFFFFFu) == urtc_t12_settle_loops(URTC_T12_SETTLE_MAX_US),
                "a wait above the maximum, however large, is cut to it and cannot overflow");
    TEST_ASSERT(urtc_t12_settle_loops(URTC_T12_SETTLE_MAX_US) <= 20u * 1000u * URTC_T12_LOOPS_PER_US / 4u, "the longest wait is a quarter of the 20 ms control cycle at most");
    TEST_ASSERT(URTC_T12_SETTLE_US <= URTC_T12_SETTLE_MAX_US && URTC_T12_SETTLE_US >= 10u, "the build wait is within the limits and never shorter than the first version's");
}

static void test_control_pass(int *failures)
{
    fake_hal_reset_recording();
    active_tool = TOOL_SOLDERING_IRON;
    target_temperature = 200;
    fake_pin_set(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_SET);
    int from = fake_gpio_write_count;
    Control_SolderingIron_PID();
    TEST_ASSERT(first_write_is_low(from), "the heater is switched off first, before the thermocouple is read, whatever the wait");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the settling time of the T12 reading (%u us)\n", (unsigned)URTC_T12_SETTLE_US);
    test_settle(&failures);
    test_control_pass(&failures);
    if (failures == 0) {
        printf("  OK   all T12 settling host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
