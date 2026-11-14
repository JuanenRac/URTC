// =============================================================================
// URTC Firmware - Queue of the frames the firmware sends on the CAN bus
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "can_tx_queue.h"

#include <string.h>

// head: where the next frame is written; tail: where the oldest one is read. Both only ever grow (they are masked when used), so a full queue is head - tail == DEPTH and the
// counters can wrap around 2^32 without confusing the difference. The caller makes the calls exclusive (see firmware_can_tx.c).
static volatile uint32_t queue_head = 0;
static volatile uint32_t queue_tail = 0;
static volatile uint32_t queue_dropped = 0;
static CanTxEntry_t queue_slots[CAN_TX_QUEUE_DEPTH];

void CanTxQueue_Reset(void)
{
    queue_head = 0;
    queue_tail = 0;
    queue_dropped = 0;
    memset(queue_slots, 0, sizeof queue_slots);
}

uint8_t CanTxQueue_Push(const CanTxEntry_t *entry)
{
    uint32_t head = queue_head;
    if ((uint32_t)(head - queue_tail) >= CAN_TX_QUEUE_DEPTH) {
        queue_dropped++;
        return 0;
    }
    queue_slots[head & (CAN_TX_QUEUE_DEPTH - 1u)] = *entry;
    queue_head = head + 1u; // published after the slot is written
    return 1;
}

uint8_t CanTxQueue_Peek(CanTxEntry_t *entry)
{
    uint32_t tail = queue_tail;
    if (queue_head == tail) {
        return 0;
    }
    *entry = queue_slots[tail & (CAN_TX_QUEUE_DEPTH - 1u)];
    return 1;
}

uint8_t CanTxQueue_Pop(CanTxEntry_t *entry)
{
    uint32_t tail = queue_tail;
    if (queue_head == tail) {
        return 0;
    }
    if (entry != NULL) {
        *entry = queue_slots[tail & (CAN_TX_QUEUE_DEPTH - 1u)];
    }
    queue_tail = tail + 1u; // the slot is free again only after it was copied
    return 1;
}

uint32_t CanTxQueue_Clear(void)
{
    uint32_t held = (uint32_t)(queue_head - queue_tail);
    queue_tail = queue_head;
    queue_dropped += held;
    return held;
}

uint32_t CanTxQueue_Count(void)
{
    return (uint32_t)(queue_head - queue_tail);
}

uint32_t CanTxQueue_Dropped(void)
{
    return queue_dropped;
}
