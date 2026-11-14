// =============================================================================
// URTC Firmware - Queue of the frames received by the CAN interrupt
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "can_rx_queue.h"

#include <string.h>

// head: where the interrupt writes next; tail: where the main loop reads next. Both only ever grow (they are masked when used), so a full queue is head - tail == DEPTH
// and the counters can wrap around 2^32 without confusing the difference.
static volatile uint32_t queue_head = 0;
static volatile uint32_t queue_tail = 0;
static volatile uint32_t queue_dropped = 0;
static CanRxEntry_t queue_slots[CAN_RX_QUEUE_DEPTH];

void CanRxQueue_Reset(void)
{
    queue_head = 0;
    queue_tail = 0;
    queue_dropped = 0;
    memset(queue_slots, 0, sizeof queue_slots);
}

uint8_t CanRxQueue_Push(const CanRxEntry_t *entry)
{
    uint32_t head = queue_head;
    if ((uint32_t)(head - queue_tail) >= CAN_RX_QUEUE_DEPTH) {
        queue_dropped++;
        return 0;
    }
    queue_slots[head & (CAN_RX_QUEUE_DEPTH - 1u)] = *entry;
    queue_head = head + 1u; // published after the slot is written
    return 1;
}

uint8_t CanRxQueue_Pop(CanRxEntry_t *entry)
{
    uint32_t tail = queue_tail;
    if (queue_head == tail) {
        return 0;
    }
    *entry = queue_slots[tail & (CAN_RX_QUEUE_DEPTH - 1u)];
    queue_tail = tail + 1u; // the slot is free again only after it was copied
    return 1;
}

uint32_t CanRxQueue_Count(void)
{
    return (uint32_t)(queue_head - queue_tail);
}

uint32_t CanRxQueue_Dropped(void)
{
    return queue_dropped;
}
