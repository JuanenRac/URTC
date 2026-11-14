// =============================================================================
// URTC Firmware - Queue of the frames the firmware sends on the CAN bus
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Until now every handler put its frame straight into one of the three hardware mailboxes, from the receive interrupt, from the EXTI interrupt or from the main loop, and the
// ones that sent several frames in a row (the 32-byte answers, the images, the dumps) gave up the rest of the answer as soon as the mailboxes were full. This is the queue that
// goes between them: every sender puts its frame in the ring, and one function empties the ring into the mailboxes, in the order the frames came, only while a mailbox is free.
//
// The ring is first in, first out. Several contexts write to it, so the glue (firmware_can_tx.c) keeps interrupts masked while it touches the ring and the mailboxes; this file
// is the pure logic of the ring and holds no lock. A full ring drops the NEW frame and counts it (an old answer is more likely to be wanted than a newer one).
//
// It is a switch: build with -DURTC_CAN_TX_QUEUE=1 to use it. The default is 0: every handler sends straight to the mailboxes as it always did, and neither the ring nor the
// glue is compiled into the firmware.
#ifndef CAN_TX_QUEUE_H
#define CAN_TX_QUEUE_H

#include <stdint.h>

#ifndef URTC_CAN_TX_QUEUE
#define URTC_CAN_TX_QUEUE 0
#endif

#ifndef CAN_TX_QUEUE_DEPTH
#define CAN_TX_QUEUE_DEPTH 64u // a power of two; the longest answer (the image of the thermal camera) is sent in separate requests of a few frames
#endif

_Static_assert((CAN_TX_QUEUE_DEPTH & (CAN_TX_QUEUE_DEPTH - 1u)) == 0u && CAN_TX_QUEUE_DEPTH >= 4u && CAN_TX_QUEUE_DEPTH <= 256u,
               "the depth of the CAN transmit queue must be a power of two between 4 and 256");

typedef struct {
    uint32_t std_id;
    uint32_t ide;
    uint32_t rtr;
    uint32_t dlc;
    uint8_t data[8];
} CanTxEntry_t;

void CanTxQueue_Reset(void);
// 1 when the frame was stored, 0 when the queue was full (the frame is dropped and counted).
uint8_t CanTxQueue_Push(const CanTxEntry_t *entry);
// The oldest frame without taking it: 1 and the frame, or 0 when there is none.
uint8_t CanTxQueue_Peek(CanTxEntry_t *entry);
// Takes the oldest frame: 1 and the frame, or 0 when there is none.
uint8_t CanTxQueue_Pop(CanTxEntry_t *entry);
// Empties the queue; what it held is counted as dropped. Returns how many frames it held.
uint32_t CanTxQueue_Clear(void);
uint32_t CanTxQueue_Count(void);
uint32_t CanTxQueue_Dropped(void);

#endif // CAN_TX_QUEUE_H
