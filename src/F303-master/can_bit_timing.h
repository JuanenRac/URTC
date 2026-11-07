// =============================================================================
// URTC Firmware - CAN bit timing: calculation, oscillator tolerance and bit-rate diagnostic (pure logic, no HAL)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The boards run the CAN peripheral from the internal 8 MHz oscillator (HSI) through the PLL. An internal RC oscillator is far less exact than a
// crystal, so a bus that drops frames at 500 kbit/s can be a clock problem. This module answers three questions without touching any hardware:
//   1. which prescaler and segment lengths give a bit rate and sample point from a given peripheral clock (urtc_can_bt_compute);
//   2. how much oscillator error that setting tolerates, by the two conditions of the CAN specification (urtc_can_bt_tolerance_ppm);
//   3. whether the clock actually in use is good enough (urtc_can_bt_diag), given its own accuracy in ppm: the HSI of the chip is about +-1 percent
//      after the factory trim, a crystal a few tens of ppm.
// The firmware uses the fixed 500 kbit/s setting it always had. The calculation only replaces that setting when the build asks for it
// (URTC_CAN_BT_FROM_CONFIG=1, with URTC_CAN_BITRATE_BPS, URTC_CAN_SAMPLE_PERMILLE and URTC_CAN_SJW_TQ); and a crystal replaces the HSI only with URTC_USE_HSE=1.
// =============================================================================
#ifndef URTC_CAN_BIT_TIMING_H
#define URTC_CAN_BIT_TIMING_H

#include <stdint.h>

#ifndef URTC_CAN_BITRATE_BPS
#define URTC_CAN_BITRATE_BPS 500000u
#endif
#ifndef URTC_CAN_SAMPLE_PERMILLE
#define URTC_CAN_SAMPLE_PERMILLE 875u
#endif
#ifndef URTC_CAN_SJW_TQ
#define URTC_CAN_SJW_TQ 2u
#endif

typedef struct {
    uint32_t prescaler;   // 1..1024
    uint32_t bs1;         // time quanta, 1..16
    uint32_t bs2;         // time quanta, 1..8
    uint32_t sjw;         // time quanta, 1..4 and never above bs2
} urtc_can_bt_t;

#define URTC_CAN_BT_OK 0
#define URTC_CAN_BT_ERR_ARGS 1       // zero clock, rate, or a sample point outside 500..900 per thousand
#define URTC_CAN_BT_ERR_NO_FIT 2     // no prescaler and segments give this rate exactly from this clock

// The exact solution with the most time quanta (best resolution), whose sample point is closest to the requested one. Only exact rates are accepted:
// a bit rate that is only close would put every node of the bus out of step.
static inline int urtc_can_bt_compute(uint32_t pclk_hz, uint32_t bitrate_bps, uint32_t sample_permille, uint32_t sjw_tq, urtc_can_bt_t *out)
{
    if (pclk_hz == 0 || bitrate_bps == 0 || sample_permille < 500 || sample_permille > 900 || out == 0) return URTC_CAN_BT_ERR_ARGS;
    int found = 0;
    uint32_t best_tq = 0, best_err = 0xFFFFFFFFu;
    urtc_can_bt_t best = {0, 0, 0, 0};
    for (uint32_t tq = 25; tq >= 8; tq--) {
        uint64_t denom = (uint64_t)bitrate_bps * tq;
        if (denom == 0 || pclk_hz % denom != 0) continue;
        uint64_t presc = pclk_hz / denom;
        if (presc < 1 || presc > 1024) continue;
        // Sample point = (1 + bs1) / tq, so bs2 = tq - (1 + bs1).
        uint32_t bs2 = (uint32_t)(((uint64_t)tq * (1000 - sample_permille) + 500) / 1000);
        if (bs2 < 1) bs2 = 1;
        if (bs2 > 8) bs2 = 8;
        if (tq < 1 + bs2 + 1) continue;
        uint32_t bs1 = tq - 1 - bs2;
        if (bs1 < 1 || bs1 > 16) continue;
        uint32_t sample = (1000u * (1 + bs1)) / tq;
        uint32_t err = sample > sample_permille ? sample - sample_permille : sample_permille - sample;
        if (!found || err < best_err || (err == best_err && tq > best_tq)) {
            found = 1;
            best_err = err;
            best_tq = tq;
            best.prescaler = (uint32_t)presc;
            best.bs1 = bs1;
            best.bs2 = bs2;
        }
    }
    if (!found) return URTC_CAN_BT_ERR_NO_FIT;
    uint32_t sjw = sjw_tq < 1 ? 1 : (sjw_tq > 4 ? 4 : sjw_tq);
    if (sjw > best.bs2) sjw = best.bs2;
    best.sjw = sjw;
    *out = best;
    return URTC_CAN_BT_OK;
}

static inline uint32_t urtc_can_bt_total_tq(const urtc_can_bt_t *bt)
{
    return 1u + bt->bs1 + bt->bs2;
}

// The bit rate this setting produces from a clock, in bit/s (integer division; compare with the requested rate).
static inline uint32_t urtc_can_bt_rate(uint32_t pclk_hz, const urtc_can_bt_t *bt)
{
    uint32_t div = bt->prescaler * urtc_can_bt_total_tq(bt);
    return div ? pclk_hz / div : 0;
}

// The sample point in per thousand of the bit.
static inline uint32_t urtc_can_bt_sample_permille(const urtc_can_bt_t *bt)
{
    return (1000u * (1u + bt->bs1)) / urtc_can_bt_total_tq(bt);
}

// Largest oscillator error the setting tolerates in either direction, in ppm: the smaller of the two conditions of the CAN specification,
//   df1 = SJW / (20 * N)              (resynchronisation must catch up within 10 bits)
//   df2 = min(PS1, PS2) / (2 * (13 * N - PS2))   (the sample point must stay inside the bit over the longest frame)
// where N is the total quanta of a bit, PS2 is bs2 and PS1 is bs1 (propagation and phase 1 together). A ppm is one millionth.
static inline uint32_t urtc_can_bt_tolerance_ppm(const urtc_can_bt_t *bt)
{
    uint32_t n = urtc_can_bt_total_tq(bt);
    uint32_t ps1 = bt->bs1, ps2 = bt->bs2;
    uint32_t m = ps1 < ps2 ? ps1 : ps2;
    uint64_t df1 = ((uint64_t)bt->sjw * 1000000u) / (20u * n);
    uint32_t d2 = 2u * (13u * n - ps2);
    uint64_t df2 = d2 ? ((uint64_t)m * 1000000u) / d2 : 0;
    return (uint32_t)(df1 < df2 ? df1 : df2);
}

// Diagnostic of the clock in use. Two nodes of a bus drift apart by the sum of their errors, so the clock accuracy must stay below half of the
// tolerance for a bus of two equal nodes.
#define URTC_CAN_DIAG_OK 0           // the clock error is inside half of the tolerance
#define URTC_CAN_DIAG_MARGINAL 1     // inside the tolerance but not inside half: a second node with the same clock can fail
#define URTC_CAN_DIAG_FAIL 2         // outside the tolerance, or the setting does not give the requested rate
#define URTC_CAN_DIAG_BAD_ARGS 3

static inline int urtc_can_bt_diag(uint32_t pclk_hz, uint32_t bitrate_bps, const urtc_can_bt_t *bt, uint32_t clock_error_ppm, uint32_t *tolerance_ppm, int32_t *rate_error_ppm)
{
    if (pclk_hz == 0 || bitrate_bps == 0 || bt == 0 || bt->prescaler == 0) return URTC_CAN_DIAG_BAD_ARGS;
    uint32_t actual = urtc_can_bt_rate(pclk_hz, bt);
    int64_t diff = (int64_t)actual - (int64_t)bitrate_bps;
    int64_t ppm = (diff * 1000000) / (int64_t)bitrate_bps;
    uint32_t tol = urtc_can_bt_tolerance_ppm(bt);
    if (tolerance_ppm) *tolerance_ppm = tol;
    if (rate_error_ppm) *rate_error_ppm = (int32_t)ppm;
    uint32_t abs_setting = (uint32_t)(ppm < 0 ? -ppm : ppm);
    if (abs_setting > 0) return URTC_CAN_DIAG_FAIL;                // the setting itself is off the requested rate
    if (clock_error_ppm > tol) return URTC_CAN_DIAG_FAIL;
    if (clock_error_ppm > tol / 2u) return URTC_CAN_DIAG_MARGINAL;
    return URTC_CAN_DIAG_OK;
}

// The CAN peripheral takes the segment lengths as bit fields (a count n is written n - 1 at bit 16 for BS1, 20 for BS2 and 24 for SJW; these are the
// values of CAN_BS1_nTQ, CAN_BS2_nTQ and CAN_SJW_nTQ of the HAL). Used only by the builds that take the timing from the configuration.
#define URTC_CAN_HAL_BS1(n) ((uint32_t)(((n) - 1u) << 16))
#define URTC_CAN_HAL_BS2(n) ((uint32_t)(((n) - 1u) << 20))
#define URTC_CAN_HAL_SJW(n) ((uint32_t)(((n) - 1u) << 24))

// External crystal (URTC_USE_HSE=1, default 0 = the internal oscillator). URTC_HSE_HZ is the crystal; the PLL multiplier that gives the same 64 MHz
// as the internal oscillator is computed from it at build time: 8 MHz x 8 = 64 MHz, 16 MHz x 4, 12 MHz does not divide and is refused by the check below.
#ifndef URTC_USE_HSE
#define URTC_USE_HSE 0
#endif
#ifndef URTC_HSE_HZ
#define URTC_HSE_HZ 8000000u
#endif
#if URTC_USE_HSE && (64000000u % URTC_HSE_HZ != 0 || (64000000u / URTC_HSE_HZ) < 2 || (64000000u / URTC_HSE_HZ) > 16)
#error "URTC_HSE_HZ must divide 64 MHz with a PLL multiplier from 2 to 16 (8 MHz or 16 MHz crystals)"
#endif
#define URTC_HSE_PLLMUL_VALUE (64000000u / (URTC_HSE_HZ))
// RCC_PLL_MULn of the HAL: (n - 2) at bit 18.
#define URTC_HSE_PLLMUL_ENC(n) ((uint32_t)(((n) - 2u) << 18))

// Accuracy of the clock sources, in ppm, to feed urtc_can_bt_diag: the internal oscillator after the factory trim (+-1 percent over the temperature
// range of the data sheet), and a crystal (typical +-50 ppm over temperature and ageing).
#define URTC_CLOCK_HSI_PPM 10000u
#define URTC_CLOCK_HSE_PPM 50u

#endif // URTC_CAN_BIT_TIMING_H
