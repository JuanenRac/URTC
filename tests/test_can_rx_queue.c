// =============================================================================
// URTC Firmware - Host logic tests for can_rx_queue.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The ring that the interrupt of the CAN receiver fills and the main loop empties: first in, first out, a full ring drops the NEW frame and counts it, nothing is lost
// or reordered across the wrap of the indices (also across the wrap of the 32-bit counters), and a sweep with a fixed seed against a model.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "can_rx_queue.h"

static uint32_t rng_state = 0xC0DEC0DEu;

static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static CanRxEntry_t entry_of(uint32_t id)
{
    CanRxEntry_t e;
    memset(&e, 0, sizeof e);
    e.std_id = id;
    e.dlc = (id % 9u);
    for (unsigned i = 0; i < 8; i++) e.data[i] = (uint8_t)(id + i);
    return e;
}

void run_can_rx_queue_tests(int *failures)
{
    CanRxEntry_t out;

    CanRxQueue_Reset();
    TEST_ASSERT(URTC_CAN_RX_QUEUE == 0, "the default build does not use the queue");
    TEST_ASSERT(CanRxQueue_Count() == 0 && CanRxQueue_Dropped() == 0 && CanRxQueue_Pop(&out) == 0, "an empty queue has nothing to give");

    for (uint32_t i = 1; i <= 5; i++) {
        CanRxEntry_t e = entry_of(0x100 + i);
        TEST_ASSERT(CanRxQueue_Push(&e) == 1, "a frame is stored while there is room");
    }
    TEST_ASSERT(CanRxQueue_Count() == 5, "the count is what was stored");
    for (uint32_t i = 1; i <= 5; i++) {
        TEST_ASSERT(CanRxQueue_Pop(&out) == 1 && out.std_id == 0x100 + i && out.dlc == ((0x100 + i) % 9u) && out.data[7] == (uint8_t)(0x100 + i + 7), "frames come out in the order they went in, whole");
    }
    TEST_ASSERT(CanRxQueue_Count() == 0 && CanRxQueue_Pop(&out) == 0, "and the queue is empty again");

    // full: the new frame is dropped and counted, the old ones are kept
    CanRxQueue_Reset();
    for (uint32_t i = 0; i < CAN_RX_QUEUE_DEPTH; i++) {
        CanRxEntry_t e = entry_of(i);
        TEST_ASSERT(CanRxQueue_Push(&e) == 1, "the queue takes exactly its depth");
    }
    CanRxEntry_t extra = entry_of(0x777);
    TEST_ASSERT(CanRxQueue_Push(&extra) == 0 && CanRxQueue_Push(&extra) == 0 && CanRxQueue_Dropped() == 2 && CanRxQueue_Count() == CAN_RX_QUEUE_DEPTH, "past its depth the new frames are dropped and counted");
    TEST_ASSERT(CanRxQueue_Pop(&out) == 1 && out.std_id == 0, "the oldest frame is the first one out");
    TEST_ASSERT(CanRxQueue_Push(&extra) == 1 && CanRxQueue_Dropped() == 2, "one slot freed takes one new frame");
    for (uint32_t i = 1; i < CAN_RX_QUEUE_DEPTH; i++) {
        TEST_ASSERT(CanRxQueue_Pop(&out) == 1 && out.std_id == i, "the old frames are all still there, in order");
    }
    TEST_ASSERT(CanRxQueue_Pop(&out) == 1 && out.std_id == 0x777 && CanRxQueue_Pop(&out) == 0, "and the one that came after them last");

    // many times round the ring
    CanRxQueue_Reset();
    uint32_t next_in = 0, next_out = 0;
    for (uint32_t round = 0; round < 1000; round++) {
        uint32_t burst = 1 + (round % (CAN_RX_QUEUE_DEPTH - 1));
        for (uint32_t i = 0; i < burst; i++) { CanRxEntry_t e = entry_of(next_in++); CanRxQueue_Push(&e); }
        for (uint32_t i = 0; i < burst; i++) {
            TEST_ASSERT(CanRxQueue_Pop(&out) == 1 && out.std_id == next_out, "going round the ring many times loses and reorders nothing");
            next_out++;
        }
    }
    TEST_ASSERT(CanRxQueue_Dropped() == 0, "and drops nothing while the consumer keeps up");

    // a sweep with a fixed seed against a model: push and pop at random
    CanRxQueue_Reset();
    uint32_t pending[CAN_RX_QUEUE_DEPTH * 2];
    uint32_t model_head = 0, model_tail = 0, model_dropped = 0, next_id = 1;
    for (int i = 0; i < 50000; i++) {
        if ((rng() & 3u) != 0u) {
            CanRxEntry_t e = entry_of(next_id);
            uint8_t stored = CanRxQueue_Push(&e);
            if (model_head - model_tail < CAN_RX_QUEUE_DEPTH) {
                pending[model_head % (CAN_RX_QUEUE_DEPTH * 2)] = next_id;
                model_head++;
                TEST_ASSERT(stored == 1, "the model says there was room: the queue stored it");
            } else {
                model_dropped++;
                TEST_ASSERT(stored == 0, "the model says it was full: the queue dropped it");
            }
            next_id++;
        } else {
            uint8_t got = CanRxQueue_Pop(&out);
            if (model_head == model_tail) {
                TEST_ASSERT(got == 0, "nothing was there");
            } else {
                TEST_ASSERT(got == 1 && out.std_id == pending[model_tail % (CAN_RX_QUEUE_DEPTH * 2)], "the oldest frame comes out");
                model_tail++;
            }
        }
        TEST_ASSERT(CanRxQueue_Count() == model_head - model_tail && CanRxQueue_Dropped() == model_dropped, "the count and the dropped counter follow the model");
    }
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - can_rx_queue.c\n");
    run_can_rx_queue_tests(&failures);
    if (failures == 0) {
        printf("  OK   all CAN receive queue host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
