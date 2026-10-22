// =============================================================================
// URTC Firmware - Host logic tests for health_profile.c and hsi_trim.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The arithmetic behind the health frame (frame layout, supply voltage and chip temperature from
// the internal references, counters that saturate) and the decision logic of the oscillator trim
// (error in parts per million, dead band, averaging, noise rejection, one step at a time, limits),
// with a simulated oscillator whose frequency error moves with the trim register.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "health_profile.h"
#include "hsi_trim.h"

static void test_health(int *failures)
{
    HealthFrame_t h = { .tec = 1, .rec = 2, .bus_off_count = 3, .hsi_trim = 16, .loop_max_ms = 7, .mcu_temp_c = 25, .vdda_mv = 3300 };
    uint8_t f[8];
    Health_Encode(&h, f);
    const uint8_t want[8] = { 1, 2, 3, 16, 7, 65, 0x0C, 0xE4 };
    TEST_ASSERT(memcmp(f, want, 8) == 0, "0x1B8 layout: tec, rec, bus-off, trim, loop, temperature + 40, supply big-endian");
    h.mcu_temp_c = HEALTH_TEMP_NA;
    h.vdda_mv = HEALTH_VDDA_NA;
    Health_Encode(&h, f);
    TEST_ASSERT(f[5] == 0 && f[6] == 0 && f[7] == 0, "not available is 0 on the wire");
    h.mcu_temp_c = -40; Health_Encode(&h, f);
    TEST_ASSERT(f[5] == 0, "a temperature below -39 is reported as not available (plus 40 must be 1 or more)");
    h.mcu_temp_c = -39; Health_Encode(&h, f);
    TEST_ASSERT(f[5] == 1, "-39 is the lowest temperature on the wire");
    h.mcu_temp_c = 215; Health_Encode(&h, f);
    TEST_ASSERT(f[5] == 255, "215 is the highest");
    h.mcu_temp_c = 216; Health_Encode(&h, f);
    TEST_ASSERT(f[5] == 0, "above that: not available, never a wrapped value");

    TEST_ASSERT(Health_CountBusOff(0) == 1 && Health_CountBusOff(254) == 255 && Health_CountBusOff(255) == 255, "bus-off count saturates at 255");
    TEST_ASSERT(Health_LoopMax(3, 2) == 3 && Health_LoopMax(3, 9) == 9 && Health_LoopMax(3, 100000) == 255, "loop maximum keeps the largest and saturates");

    TEST_ASSERT(Health_VddaMv(1500, 1500) == 3300, "reading equal to the calibration: 3300 mV");
    TEST_ASSERT(Health_VddaMv(1650, 1500) == 3000, "a higher reference reading means a lower supply");
    TEST_ASSERT(Health_VddaMv(0, 1500) == HEALTH_VDDA_NA && Health_VddaMv(1500, 0) == HEALTH_VDDA_NA, "no reading, no calibration: not available");

    // factory points: 30 C reads 1700, 110 C reads 2100 (at 3300 mV)
    TEST_ASSERT(Health_McuTempC(1700, 3300, 1700, 2100) == 30, "the first calibration point");
    TEST_ASSERT(Health_McuTempC(2100, 3300, 1700, 2100) == 110, "the second calibration point");
    TEST_ASSERT(Health_McuTempC(1900, 3300, 1700, 2100) == 70, "halfway");
    TEST_ASSERT(Health_McuTempC(1500, 3300, 1700, 2100) == -10, "below the first point");
    TEST_ASSERT(Health_McuTempC(1700, 3000, 1700, 2100) != 30, "the reading is scaled to 3300 mV first");
    TEST_ASSERT(Health_McuTempC(1700, HEALTH_VDDA_NA, 1700, 2100) == HEALTH_TEMP_NA, "no supply reading: not available");
    TEST_ASSERT(Health_McuTempC(1700, 3300, 2100, 1700) == HEALTH_TEMP_NA, "inverted calibration: not available");
}

// A simulated oscillator: +0.30 % per trim step above 16, error in ppm = base + 3000 * (trim - 16)
static uint32_t measure(int base_ppm, uint8_t trim, int noise_ppm_peak, int i)
{
    int ppm = base_ppm + 3000 * ((int)trim - 16);
    ppm += (i % 2 ? 1 : -1) * noise_ppm_peak;
    return (uint32_t)(1000000 + ppm);
}

static void test_hsi(int *failures)
{
    HsiTrim_t t;
    HsiTrim_Init(&t, 16);
    TEST_ASSERT(t.trim == 16, "starts at the factory value");
    TEST_ASSERT(HsiTrim_ErrorPpm(1000000, 1001000) == 1000 && HsiTrim_ErrorPpm(1000000, 999000) == -1000, "error in ppm, positive when the count is high");
    TEST_ASSERT(HsiTrim_ErrorPpm(0, 5) == 0, "no division by zero");

    // 4 % too slow at trim 16 is not reachable in 6 steps; 0.9 % too fast is: it walks down by one step per window
    int base = 9000;                          // the oscillator is 0.9 % fast at trim 16
    int changes = 0;
    for (int i = 0; i < 16 * 8; i++) {
        if (HsiTrim_Update(&t, 1000000, measure(base, t.trim, 300, i))) changes++;
    }
    TEST_ASSERT(t.trim == 13, "a fast oscillator is trimmed down three steps and then left alone (dead band)");
    TEST_ASSERT(changes == 3, "one step per window of 16 samples, never two at once");

    // a slow oscillator is trimmed up
    HsiTrim_Init(&t, 16);
    for (int i = 0; i < 16 * 8; i++) HsiTrim_Update(&t, 1000000, measure(-6000, t.trim, 200, i));
    TEST_ASSERT(t.trim == 18, "a slow oscillator is trimmed up");

    // already inside the dead band: no change
    HsiTrim_Init(&t, 16);
    for (int i = 0; i < 16 * 8; i++) HsiTrim_Update(&t, 1000000, measure(1200, t.trim, 100, i));
    TEST_ASSERT(t.trim == 16, "inside the dead band the trim is not touched");

    // noisy measurements are thrown away
    HsiTrim_Init(&t, 16);
    for (int i = 0; i < 16 * 8; i++) HsiTrim_Update(&t, 1000000, measure(9000, t.trim, 8000, i));
    TEST_ASSERT(t.trim == 16, "a window that scatters too much is not trusted");

    // the correction is limited around the starting value, whatever the error
    HsiTrim_Init(&t, 16);
    for (int i = 0; i < 16 * 100; i++) HsiTrim_Update(&t, 1000000, 1100000);
    TEST_ASSERT(t.trim == 10, "never more than 6 steps from the start");
    HsiTrim_Init(&t, 2);
    for (int i = 0; i < 16 * 100; i++) HsiTrim_Update(&t, 1000000, 1100000);
    TEST_ASSERT(t.trim == 0, "never below the register minimum");
    HsiTrim_Init(&t, 30);
    for (int i = 0; i < 16 * 100; i++) HsiTrim_Update(&t, 1000000, 900000);
    TEST_ASSERT(t.trim == 31, "never above the register maximum");
    HsiTrim_Init(&t, 99);
    TEST_ASSERT(t.trim == HSI_TRIM_DEFAULT, "an impossible start value becomes the factory value");
    TEST_ASSERT(HsiTrim_Update(&t, 0, 5) == 0, "an expected count of 0 is ignored");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - health_profile.c, hsi_trim.c\n");
    test_health(&failures);
    test_hsi(&failures);
    if (failures == 0) {
        printf("  OK   all health and oscillator trim host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
