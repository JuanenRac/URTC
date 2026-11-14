// =============================================================================
// URTC Firmware - Host logic tests for can_tx_queue.c and firmware_can_tx.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The ring that every sender of the application puts its frame in (URTC_CAN_TX_QUEUE=1) and the glue that empties it into the three mailboxes: first in, first out; a full ring
// drops the NEW frame and counts it; nothing is sent while no mailbox is free and everything is sent, in order, as soon as one is; a burst longer than the three mailboxes is
// no longer cut after the third frame; what waits in the ring is discarded after a bus-off. A sweep with a fixed seed is compared with a model.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { .Instance = &can_regs };
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
volatile ToolMode_t active_tool;
volatile uint8_t system_error_flag;

static uint32_t rng_state = 0xC0DE5EEDu;

static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static HAL_StatusTypeDef send(uint32_t id, uint8_t dlc)
{
    CAN_TxHeaderTypeDef h;
    uint8_t data[8];
    uint32_t mb;
    memset(&h, 0, sizeof h);
    h.StdId = id;
    h.IDE = CAN_ID_STD;
    h.RTR = CAN_RTR_DATA;
    h.DLC = dlc;
    for (unsigned i = 0; i < 8; i++) data[i] = (uint8_t)(id + i);
    return HAL_CAN_AddTxMessage(&hcan, &h, data, &mb); // the macro of firmware_common.h: it goes into the queue
}

static void fresh(void)
{
    fake_hal_reset_recording();
    CanTxQueue_Reset();
    fake_tx_mailboxes_free = 3;
}

void run_can_tx_queue_tests(int *failures)
{
    CanTxEntry_t out;

    TEST_ASSERT(URTC_CAN_TX_QUEUE == 1, "this suite is built with the queue on");

    // ---- the ring ----
    fresh();
    TEST_ASSERT(CanTxQueue_Count() == 0 && CanTxQueue_Pop(&out) == 0 && CanTxQueue_Peek(&out) == 0, "an empty queue has nothing to give");
    for (uint32_t i = 1; i <= CAN_TX_QUEUE_DEPTH; i++) {
        CanTxEntry_t e;
        memset(&e, 0, sizeof e);
        e.std_id = i;
        e.dlc = 8;
        TEST_ASSERT(CanTxQueue_Push(&e) == 1, "the ring takes exactly its depth");
    }
    CanTxEntry_t extra;
    memset(&extra, 0, sizeof extra);
    extra.std_id = 0x777;
    TEST_ASSERT(CanTxQueue_Push(&extra) == 0 && CanTxQueue_Dropped() == 1 && CanTxQueue_Count() == CAN_TX_QUEUE_DEPTH, "past its depth the new frame is dropped and counted");
    TEST_ASSERT(CanTxQueue_Peek(&out) == 1 && out.std_id == 1 && CanTxQueue_Count() == CAN_TX_QUEUE_DEPTH, "peeking does not take the frame");
    TEST_ASSERT(CanTxQueue_Pop(&out) == 1 && out.std_id == 1 && CanTxQueue_Push(&extra) == 1, "the oldest comes out first and frees one slot");
    TEST_ASSERT(CanTxQueue_Clear() == CAN_TX_QUEUE_DEPTH && CanTxQueue_Count() == 0 && CanTxQueue_Dropped() == 1 + CAN_TX_QUEUE_DEPTH, "clearing counts what it held as dropped");

    // ---- the glue: nothing is sent without a free mailbox, everything in order when one frees up ----
    fresh();
    fake_tx_mailboxes_free = 0;
    for (uint32_t i = 0; i < 10; i++) {
        TEST_ASSERT(send(0x200 + i, 8) == HAL_OK, "a frame is accepted while there is room");
    }
    TEST_ASSERT(fake_tx_count == 0 && CanTxQueue_Count() == 10, "with no free mailbox nothing is sent and the frames wait");
    TEST_ASSERT(CanTx_FreeLevel() == CAN_TX_QUEUE_DEPTH - 10, "the room asked by the senders is the room of the queue");
    fake_tx_mailboxes_free = 1;
    CanTx_Service();
    TEST_ASSERT(fake_tx_count == 10 && CanTxQueue_Count() == 0, "the main loop sends what waited");
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT(fake_tx[i].id == (uint32_t)(0x200 + i) && fake_tx[i].dlc == 8 && fake_tx[i].data[3] == (uint8_t)(0x200 + i + 3), "in the order they were submitted, whole");
    }

    // a burst longer than the three mailboxes is not cut: the senders check the room, and the room is the queue
    fresh();
    fake_tx_mailboxes_free = 0;
    int sent_by_a_chunked_answer = 0;
    for (int frame = 0; frame < 20; frame++) {
        if (CanTx_FreeLevel() == 0) break;
        send(0x300, 8);
        sent_by_a_chunked_answer++;
    }
    TEST_ASSERT(sent_by_a_chunked_answer == 20, "an answer of 20 frames is not cut after three");

    // when the mailboxes are free the frame leaves at once, as before
    fresh();
    send(0x110, 1);
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].id == 0x110 && fake_tx[0].dlc == 1 && CanTxQueue_Count() == 0, "a frame leaves at once when a mailbox is free");

    // a full queue drops the new frame and says so to the sender
    fresh();
    fake_tx_mailboxes_free = 0;
    for (uint32_t i = 0; i < CAN_TX_QUEUE_DEPTH; i++) send(0x400 + i, 2);
    TEST_ASSERT(CanTx_FreeLevel() == 0 && send(0x7FF, 2) == HAL_ERROR && CanTx_Dropped() == 1, "a full queue refuses the new frame and counts it");
    fake_tx_mailboxes_free = 3;
    CanTx_Service();
    TEST_ASSERT(fake_tx_count == (int)CAN_TX_QUEUE_DEPTH && fake_tx[0].id == 0x400 && fake_tx[CAN_TX_QUEUE_DEPTH - 1].id == 0x400 + CAN_TX_QUEUE_DEPTH - 1, "the old frames are all still there, in order");

    // bad calls
    fresh();
    CAN_TxHeaderTypeDef bad;
    memset(&bad, 0, sizeof bad);
    bad.DLC = 9;
    uint8_t nine[16] = {0};
    TEST_ASSERT(HAL_CAN_AddTxMessage(&hcan, &bad, nine, NULL) == HAL_ERROR, "a length over 8 is refused");
    bad.DLC = 4;
    TEST_ASSERT(HAL_CAN_AddTxMessage(&hcan, &bad, NULL, NULL) == HAL_ERROR && CanTxQueue_Count() == 0, "a frame with data and no data is refused");
    bad.DLC = 0;
    TEST_ASSERT(HAL_CAN_AddTxMessage(&hcan, &bad, NULL, NULL) == HAL_OK && fake_tx_count == 1, "an empty frame needs no data");

    // after a bus-off what waits is discarded, not sent in a burst
    fresh();
    fake_tx_mailboxes_free = 0;
    for (uint32_t i = 0; i < 7; i++) send(0x500 + i, 8);
    TEST_ASSERT(CanTx_Discard() == 7 && CanTxQueue_Count() == 0, "the discard says how many frames it threw away");
    fake_tx_mailboxes_free = 3;
    CanTx_Service();
    TEST_ASSERT(fake_tx_count == 0, "and none of them is sent afterwards");

    // ---- a sweep against a model: random submissions, random mailbox availability ----
    fresh();
    uint32_t next_id = 1, model_waiting = 0, model_dropped = 0;
    uint32_t base_dropped = CanTx_Dropped();
    uint32_t last_seen = 0;
    for (int step = 0; step < 4000; step++) {
        uint32_t r = rng() % 8u;
        if (r < 4u) {
            fake_tx_mailboxes_free = (int)(rng() % 3u); // 0, 1 or 2 free
            if (send(next_id, (uint8_t)(rng() % 9u)) == HAL_OK) {
                if (fake_tx_mailboxes_free > 0) { model_waiting = 0; } else { model_waiting++; }
            } else {
                model_dropped++;
            }
            next_id++;
        } else if (r < 6u) {
            fake_tx_mailboxes_free = (int)(rng() % 4u);
            CanTx_Service();
            if (fake_tx_mailboxes_free > 0) { model_waiting = 0; }
        }
        TEST_ASSERT(CanTxQueue_Count() == model_waiting, "what waits in the queue follows the model");
        if (fake_tx_count > FAKE_MAX_TX - 16) { // the recorder is small: check the order and start it again
            for (int i = 0; i < fake_tx_count; i++) {
                TEST_ASSERT(fake_tx[i].id > last_seen, "frames leave in the order they came");
                last_seen = fake_tx[i].id;
            }
            fake_tx_count = 0;
        }
    }
    fake_tx_mailboxes_free = 3;
    CanTx_Service();
    TEST_ASSERT(CanTxQueue_Count() == 0, "the queue ends empty once the mailboxes are free");
    TEST_ASSERT(CanTx_Dropped() - base_dropped == model_dropped, "the dropped counter follows the model");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - can_tx_queue.c / firmware_can_tx.c\n");
    run_can_tx_queue_tests(&failures);
    if (failures == 0) {
        printf("  OK   all CAN transmit queue host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
