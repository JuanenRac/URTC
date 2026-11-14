// =============================================================================
// URTC Firmware - Host logic tests for urtc_percent.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The percentage used by the progress bars and heartbeats of the bootloader: the same byte as the 64-bit product it replaces for every total up to the exact limit,
// the ends (0, 100, a part above the total that wraps like the byte it replaces), a total of zero, a total above the limit, and a sweep with a fixed seed against a model.
#include <stdint.h>
#include <stdio.h>

#include "test_runner.h"
#include "urtc_percent.h"

static uint32_t rng_state = 0x5EEDBEEFu;

static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static uint8_t model(uint32_t part, uint32_t total)
{
    return (uint8_t)(((uint64_t)part * 100u) / total);
}

void run_percent_tests(int *failures)
{
    TEST_ASSERT(urtc_percent_of(0u, 1000u) == 0u, "nothing done is 0 %");
    TEST_ASSERT(urtc_percent_of(1000u, 1000u) == 100u, "everything done is 100 %");
    TEST_ASSERT(urtc_percent_of(500u, 1000u) == 50u && urtc_percent_of(1u, 3u) == 33u && urtc_percent_of(2u, 3u) == 66u, "the figure is cut, not rounded");
    TEST_ASSERT(urtc_percent_of(5u, 0u) == 0u && urtc_percent_of(0u, 0u) == 0u, "a total of zero gives 0, never a division by zero");
    TEST_ASSERT(urtc_percent_of(300u, 100u) == model(300u, 100u), "a part above the total wraps like the byte it replaces");
    TEST_ASSERT(urtc_percent_of(URTC_PERCENT_EXACT_TOTAL, URTC_PERCENT_EXACT_TOTAL) == 100u, "the largest exact total reaches 100 %");
    TEST_ASSERT(urtc_percent_of(URTC_PERCENT_EXACT_TOTAL - 1u, URTC_PERCENT_EXACT_TOTAL) == model(URTC_PERCENT_EXACT_TOTAL - 1u, URTC_PERCENT_EXACT_TOTAL), "one below the largest exact total");
    TEST_ASSERT(urtc_percent_of(0xFFFFFFFFu, 0xFFFFFFFFu) == 100u && urtc_percent_of(0x80000000u, 0xFFFFFFFFu) == 50u, "a total above the limit is scaled down and keeps the bar");
    TEST_ASSERT(urtc_percent_of(1u, 0xFFFFFFFFu) == 0u, "a scaled part that vanishes is 0 %");

    int sweep_ok = 1;
    for (int i = 0; i < 20000; ++i) {
        uint32_t total = 1u + rng() % (i % 2 ? 262144u : URTC_PERCENT_EXACT_TOTAL);
        uint32_t part = rng() % (total + 1u);
        if (urtc_percent_of(part, total) != model(part, total)) {
            sweep_ok = 0;
            printf("percent differs for %u of %u\n", (unsigned)part, (unsigned)total);
            break;
        }
    }
    TEST_ASSERT(sweep_ok, "20000 seeded pairs give the 64-bit result");

    int big_ok = 1;
    for (int i = 0; i < 2000; ++i) {
        uint32_t total = URTC_PERCENT_EXACT_TOTAL + 1u + rng() % 0xF0000000u;
        uint32_t part = rng() % (total + 1u);
        int diff = (int)urtc_percent_of(part, total) - (int)model(part, total);
        if (diff < -1 || diff > 1) big_ok = 0;
    }
    TEST_ASSERT(big_ok, "above the exact limit the bar is off by one step at most");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - urtc_percent.h\n");
    run_percent_tests(&failures);
    if (failures == 0) {
        printf("  OK   all percent host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
