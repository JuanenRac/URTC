// =============================================================================
// URTC Firmware - Host logic tests for spool_position.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The open-loop position of the solder wire feeder: the ordinary sums, the two ends of the range (the position saturates, it never jumps to the other sign), the largest
// command of 2^32 - 1 steps, and a sweep with a fixed seed against a model in 64 bits.
#include <stdint.h>
#include <stdio.h>

#include "test_runner.h"
#include "spool_position.h"

static uint32_t rng_state = 0xC0FFEE11u;

static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

void run_spool_position_tests(int *failures)
{
    TEST_ASSERT(SpoolPosition_Add(0, 100u, 1) == 100 && SpoolPosition_Add(0, 100u, 0) == -100, "from zero the sum goes both ways");
    TEST_ASSERT(SpoolPosition_Add(500, 200u, 0) == 300 && SpoolPosition_Add(-500, 200u, 1) == -300, "a move back and forth keeps its sign");
    TEST_ASSERT(SpoolPosition_Add(7, 0u, 1) == 7 && SpoolPosition_Add(7, 0u, 0) == 7, "zero steps change nothing");
    TEST_ASSERT(SpoolPosition_Add(INT32_MAX - 5, 5u, 1) == INT32_MAX, "exactly the top of the range is reached");
    TEST_ASSERT(SpoolPosition_Add(INT32_MAX - 5, 6u, 1) == INT32_MAX, "one step more stops at the top");
    TEST_ASSERT(SpoolPosition_Add(INT32_MAX, 1u, 1) == INT32_MAX, "at the top, forward stays there");
    TEST_ASSERT(SpoolPosition_Add(INT32_MIN + 5, 5u, 0) == INT32_MIN, "exactly the bottom of the range is reached");
    TEST_ASSERT(SpoolPosition_Add(INT32_MIN + 5, 6u, 0) == INT32_MIN, "one step more stops at the bottom");
    TEST_ASSERT(SpoolPosition_Add(INT32_MIN, 1u, 0) == INT32_MIN, "at the bottom, backward stays there");
    TEST_ASSERT(SpoolPosition_Add(0, 0xFFFFFFFFu, 1) == INT32_MAX, "the largest command forward stops at the top, not at a negative number");
    TEST_ASSERT(SpoolPosition_Add(0, 0xFFFFFFFFu, 0) == INT32_MIN, "the largest command backward stops at the bottom");
    TEST_ASSERT(SpoolPosition_Add(INT32_MIN, 0xFFFFFFFFu, 1) == INT32_MAX, "from the bottom the largest command forward reaches the top");
    TEST_ASSERT(SpoolPosition_Add(INT32_MAX, 0xFFFFFFFFu, 0) == INT32_MIN, "from the top the largest command backward reaches the bottom");

    for (int i = 0; i < 20000; i++) {
        int32_t position = (int32_t)rng();
        uint32_t steps = (rng() % 4u == 0u) ? rng() : (rng() % 1000u);
        int forward = (int)(rng() & 1u);
        int64_t expected = (int64_t)position + (forward ? (int64_t)steps : -(int64_t)steps);
        if (expected > INT32_MAX) expected = INT32_MAX;
        if (expected < INT32_MIN) expected = INT32_MIN;
        TEST_ASSERT(SpoolPosition_Add(position, steps, forward) == (int32_t)expected, "the sum equals the model in 64 bits, saturated at the ends");
        int32_t result = SpoolPosition_Add(position, steps, forward);
        TEST_ASSERT(forward ? (result >= position) : (result <= position), "forward never lowers the position and backward never raises it");
    }
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - spool_position.c\n");
    run_spool_position_tests(&failures);
    if (failures == 0) {
        printf("  OK   all spool position host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
