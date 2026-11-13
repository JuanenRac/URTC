// =============================================================================
// URTC Firmware - Host logic tests for fw_safety_policy.c (clear-error command and communication watchdog: the decisions)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The cases URTC-COMMON wrote from its Python reference (urtc_common.safety_policy) are replayed against the C functions, and the edges the vectors
// leave to chance are checked here: the temperature limit is exclusive, an unknown temperature is refused, the tool-identity error is never lifted
// whatever else is true, the silence limit never falls below its floor, never overflows and does not shrink when the move gets longer.
#include <stdint.h>
#include <stdio.h>

#include "test_runner.h"
#include "fw_safety_policy.h"
#include "urtc_can_ids.h"
#include "vectors/safety_policy_vectors.h"

static void test_vectors(int *failures)
{
    int bad = 0;
    for (int i = 0; i < SAFETY_VEC_COUNT(SAFETY_CLEAR_CASES); i++) {
        const safety_clear_case_t *c = &SAFETY_CLEAR_CASES[i];
        if (FwSafe_ClearDecision(c->feature_on, c->error_latched, c->reason, c->temperature_c, c->actuators_off) != c->result) bad++;
    }
    TEST_ASSERT(bad == 0, "every clear-error case of URTC-COMMON gives the same outcome");
    bad = 0;
    for (int i = 0; i < SAFETY_VEC_COUNT(SAFETY_LIMIT_CASES); i++) {
        const safety_limit_case_t *c = &SAFETY_LIMIT_CASES[i];
        if (FwSafe_StepperSilenceLimitMs(c->steps, c->speed_sps) != c->limit_ms) bad++;
    }
    TEST_ASSERT(bad == 0, "every silence limit of URTC-COMMON is the same");
    bad = 0;
    for (int i = 0; i < SAFETY_VEC_COUNT(SAFETY_STOP_CASES); i++) {
        const safety_stop_case_t *c = &SAFETY_STOP_CASES[i];
        if (FwSafe_StepperMustStop(c->feature_on, c->moving, c->silence_ms, c->limit_ms) != c->stop) bad++;
    }
    TEST_ASSERT(bad == 0, "every cut of a stepper move of URTC-COMMON is the same");
    bad = 0;
    for (int i = 0; i < SAFETY_VEC_COUNT(SAFETY_MAGNET_CASES); i++) {
        const safety_magnet_case_t *c = &SAFETY_MAGNET_CASES[i];
        if (FwSafe_MagnetMustRelease(c->watchdog_on, c->release_on, c->energised, c->silence_ms) != c->release) bad++;
    }
    TEST_ASSERT(bad == 0, "every release of the electromagnet of URTC-COMMON is the same");
}

static void test_clear_edges(int *failures)
{
    TEST_ASSERT(FwSafe_ClearDecision(1, 1, URTC_ERROR_REASON_THERMAL, URTC_CLEAR_ERROR_MAX_TEMP_C - 1, 1) == URTC_CLEAR_ERROR_CLEARED, "one degree under the limit is lifted");
    TEST_ASSERT(FwSafe_ClearDecision(1, 1, URTC_ERROR_REASON_THERMAL, URTC_CLEAR_ERROR_MAX_TEMP_C, 1) == URTC_CLEAR_ERROR_REFUSED_HOT, "the limit itself is refused");
    TEST_ASSERT(FwSafe_ClearDecision(1, 1, URTC_ERROR_REASON_SENSOR, FWSAFE_TEMPERATURE_UNKNOWN, 1) == URTC_CLEAR_ERROR_REFUSED_HOT, "an unknown temperature is refused");
    int tool_id_lifted = 0;
    for (unsigned temperature = 0; temperature < 400; temperature += 7) {
        for (int off = 0; off < 2; off++) {
            if (FwSafe_ClearDecision(1, 1, URTC_ERROR_REASON_TOOL_ID, (uint16_t)temperature, (uint8_t)off) == URTC_CLEAR_ERROR_CLEARED) tool_id_lifted++;
        }
    }
    TEST_ASSERT(tool_id_lifted == 0, "the error of a tool that is not valid is never lifted");
    int lifted_with_switch_off = 0;
    for (unsigned reason = 0; reason < 9; reason++) {
        if (FwSafe_ClearDecision(0, 1, (uint8_t)reason, 20, 1) == URTC_CLEAR_ERROR_CLEARED) lifted_with_switch_off++;
    }
    TEST_ASSERT(lifted_with_switch_off == 0, "with the switch off nothing is ever lifted");
}

static void test_silence_limits(int *failures)
{
    TEST_ASSERT(FwSafe_StepperSilenceLimitMs(0, 0) == URTC_STEPPER_SILENCE_MIN_MS, "no steps: the floor");
    TEST_ASSERT(FwSafe_StepperSilenceLimitMs(4000, 500) == 4000, "8 s of movement allow 4 s of silence");
    TEST_ASSERT(FwSafe_StepperSilenceLimitMs(4000, 0) == FwSafe_StepperSilenceLimitMs(4000, 500), "speed zero is the default speed");
    TEST_ASSERT(FwSafe_StepperSilenceLimitMs(0xFFFFFFFFu, 1) == 0xFFFFFFFFu, "a huge move saturates instead of wrapping");
    uint32_t previous = 0;
    int shrank = 0, below_floor = 0;
    for (uint32_t steps = 0; steps < 200000; steps += 997) {
        uint32_t limit = FwSafe_StepperSilenceLimitMs(steps, 500);
        if (limit < previous) shrank++;
        if (limit < URTC_STEPPER_SILENCE_MIN_MS) below_floor++;
        previous = limit;
    }
    TEST_ASSERT(shrank == 0 && below_floor == 0, "the limit never shrinks with more steps and never falls below the floor");
    TEST_ASSERT(FwSafe_StepperMustStop(1, 1, URTC_STEPPER_SILENCE_MIN_MS + 1, URTC_STEPPER_SILENCE_MIN_MS) == 1, "one millisecond over the limit cuts the move");
    TEST_ASSERT(FwSafe_StepperMustStop(1, 1, URTC_STEPPER_SILENCE_MIN_MS, URTC_STEPPER_SILENCE_MIN_MS) == 0, "the limit itself does not");
    TEST_ASSERT(FwSafe_MagnetMustRelease(1, 0, 1, 0xFFFFFFFFu) == 0, "without the release switch the coil is held however long the silence");
    TEST_ASSERT(FwSafe_MagnetMustRelease(0, 1, 1, 0xFFFFFFFFu) == 0, "without the watchdog switch the coil is held too");
    TEST_ASSERT(FwSafe_MagnetMustRelease(1, 1, 1, URTC_MAGNET_SILENCE_MS + 1) == 1, "both switches and a long silence let go");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - fw_safety_policy.c\n");
    test_vectors(&failures);
    test_clear_edges(&failures);
    test_silence_limits(&failures);
    if (failures == 0) {
        printf("  OK   all safety policy host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
