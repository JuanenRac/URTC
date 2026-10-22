// =============================================================================
// URTC Firmware - Trim of the internal oscillator against the CAN bit time (pure logic)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The board runs from its internal oscillator, which drifts with temperature and from part to
// part; CAN needs the bit time within about half a percent. The idea: a reference interval of a
// known length measured on the bus (the nominal bit time of a received frame, or a stretch of
// known length) is counted with the timer that runs from the oscillator; if the count is high
// the oscillator is fast, and the trim register is lowered by a step, and the other way round.
//
// This file decides what to do with the counts; reading the bit time and writing the register
// (RCC_CR HSITRIM, 5 bits, 16 at reset) are the glue's job, and that part needs a bench: the
// size of one trim step and the quality of the measurement are only known on a real board.
#ifndef HSI_TRIM_H
#define HSI_TRIM_H

#include <stdint.h>

#define HSI_TRIM_MIN 0u
#define HSI_TRIM_MAX 31u
#define HSI_TRIM_DEFAULT 16u

typedef struct {
    uint8_t  trim;              // value in force
    uint8_t  initial;           // value at start, the limit of the correction is measured from it
    uint8_t  max_excursion;     // furthest the trim may move from the initial value
    uint32_t ppm_per_step;      // how far one step moves the frequency (about 3000 on this oscillator family; to be measured)
    uint32_t deadband_ppm;      // no change inside this error
    uint8_t  min_samples;       // measurements averaged before a decision
    uint8_t  samples;
    int32_t  sum_ppm;
    int32_t  min_ppm, max_ppm;  // spread of the window: a window that scatters more than max_spread_ppm is thrown away
    uint32_t max_spread_ppm;
} HsiTrim_t;

void HsiTrim_Init(HsiTrim_t *t, uint8_t initial_trim);

// Error in parts per million of a measurement: positive when the count is higher than expected (the oscillator is fast).
int32_t HsiTrim_ErrorPpm(uint32_t expected_ticks, uint32_t measured_ticks);

// Feed one measurement. Returns 1 when the trim changed (the glue then writes t->trim), else 0.
uint8_t HsiTrim_Update(HsiTrim_t *t, uint32_t expected_ticks, uint32_t measured_ticks);

#endif // HSI_TRIM_H
