// =============================================================================
// URTC Firmware - Trim of the internal oscillator against the CAN bit time (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "hsi_trim.h"

void HsiTrim_Init(HsiTrim_t *t, uint8_t initial_trim) {
    t->trim = initial_trim > HSI_TRIM_MAX ? HSI_TRIM_DEFAULT : initial_trim;
    t->initial = t->trim;
    t->max_excursion = 6;
    t->ppm_per_step = 3000;
    t->deadband_ppm = 1500;
    t->min_samples = 16;
    t->samples = 0;
    t->sum_ppm = 0;
    t->min_ppm = 0;
    t->max_ppm = 0;
    t->max_spread_ppm = 6000;
}

int32_t HsiTrim_ErrorPpm(uint32_t expected_ticks, uint32_t measured_ticks) {
    if (expected_ticks == 0) return 0;
    int64_t diff = (int64_t)measured_ticks - (int64_t)expected_ticks;
    return (int32_t)(diff * 1000000 / (int64_t)expected_ticks);
}

static void clear_window(HsiTrim_t *t) {
    t->samples = 0;
    t->sum_ppm = 0;
    t->min_ppm = 0;
    t->max_ppm = 0;
}

uint8_t HsiTrim_Update(HsiTrim_t *t, uint32_t expected_ticks, uint32_t measured_ticks) {
    if (expected_ticks == 0) return 0;
    int32_t ppm = HsiTrim_ErrorPpm(expected_ticks, measured_ticks);
    if (t->samples == 0) { t->min_ppm = ppm; t->max_ppm = ppm; }
    if (ppm < t->min_ppm) t->min_ppm = ppm;
    if (ppm > t->max_ppm) t->max_ppm = ppm;
    t->sum_ppm += ppm;
    t->samples++;
    if (t->samples < t->min_samples) return 0;

    uint32_t spread = (uint32_t)(t->max_ppm - t->min_ppm);
    int32_t mean = t->sum_ppm / (int32_t)t->samples;
    clear_window(t);
    if (spread > t->max_spread_ppm) return 0;                   // too noisy to trust
    uint32_t magnitude = (uint32_t)(mean < 0 ? -mean : mean);
    if (magnitude <= t->deadband_ppm) return 0;                 // good enough: leave it
    // one step at a time, towards the correct frequency: a fast oscillator (positive error) needs a lower trim value
    uint8_t before = t->trim;
    int next = (int)t->trim + (mean > 0 ? -1 : 1);
    int excursion = next - (int)t->initial;
    if (next >= (int)HSI_TRIM_MIN && next <= (int)HSI_TRIM_MAX && excursion >= -(int)t->max_excursion && excursion <= (int)t->max_excursion)
        t->trim = (uint8_t)next;
    return t->trim != before;
}
