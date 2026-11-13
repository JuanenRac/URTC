// =============================================================================
// URTC Firmware - Queue of the frames received by the CAN interrupt
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The rule that the queue makes possible: the interrupt of the CAN receiver only COPIES the frame (identifier, kind, length and data) into this ring, and the main loop is
// the one that decides what to do with it. Until now the whole dispatcher ran inside the interrupt, and so did the handlers that talk to the I2C bus of the screen and the
// F-RAM, to the bit-bang bus of the expansion board and to the CAN transmitter, which a main loop that was in the middle of the same bus could be interrupted in.
//
// The queue is a ring for one producer (the interrupt) and one consumer (the main loop): the interrupt writes only the head, the main loop writes only the tail, so no lock
// is needed. When it is full the NEW frame is dropped and counted (an old command is more likely to be still wanted than a newer one that arrives while the loop is
// stalled; the count is visible to the host).
//
// It is a switch: build with -DURTC_CAN_RX_QUEUE=1 to use it. The default is 0: the dispatcher runs in the interrupt as it always did, and the queue is not even compiled
// into the callback.
#ifndef CAN_RX_QUEUE_H
#define CAN_RX_QUEUE_H

#include <stdint.h>

#ifndef URTC_CAN_RX_QUEUE
#define URTC_CAN_RX_QUEUE 0
#endif

#ifndef CAN_RX_QUEUE_DEPTH
#define CAN_RX_QUEUE_DEPTH 32u // a power of two
#endif

_Static_assert((CAN_RX_QUEUE_DEPTH & (CAN_RX_QUEUE_DEPTH - 1u)) == 0u && CAN_RX_QUEUE_DEPTH >= 2u && CAN_RX_QUEUE_DEPTH <= 256u,
               "the depth of the CAN receive queue must be a power of two between 2 and 256");

typedef struct {
    uint32_t std_id;
    uint32_t ide;
    uint32_t rtr;
    uint32_t dlc;
    uint8_t data[8];
} CanRxEntry_t;

void CanRxQueue_Reset(void);
// Called by the interrupt: 1 when the frame was stored, 0 when the queue was full (the frame is dropped and counted).
uint8_t CanRxQueue_Push(const CanRxEntry_t *entry);
// Called by the main loop: 1 and the oldest frame, or 0 when there is none.
uint8_t CanRxQueue_Pop(CanRxEntry_t *entry);
uint32_t CanRxQueue_Count(void);
uint32_t CanRxQueue_Dropped(void);

#endif // CAN_RX_QUEUE_H
