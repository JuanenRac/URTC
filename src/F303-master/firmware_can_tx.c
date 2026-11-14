// =============================================================================
// URTC Firmware - Sending on the CAN bus through the transmit queue
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// With URTC_CAN_TX_QUEUE=1 firmware_common.h turns every HAL_CAN_AddTxMessage() of the application into CanTx_Submit() and every question about the free mailboxes into
// CanTx_FreeLevel(), so that no handler has to be changed: they put their frame in the ring (can_tx_queue.c) and this file empties the ring into the three hardware mailboxes,
// oldest first, while a mailbox is free. It is done right after each submission (so the latency is the same as before when the mailboxes are free) and once per pass of the main
// loop (for what did not fit). The ring and the mailboxes are only touched with interrupts masked, so the receive interrupt and the main loop never interleave inside them.
// The frame of the impact event, which must never wait, still goes straight to the mailboxes from its interrupt (firmware_interrupts_gpio.c): it can only run between two
// steps of this file, never inside one.
#include "firmware_common.h"

#if URTC_CAN_TX_QUEUE

// The functions below must reach the real HAL: firmware_common.h redirects the names for everybody else.
#undef HAL_CAN_AddTxMessage
#undef HAL_CAN_GetTxMailboxesFreeLevel

static volatile uint32_t tx_failed = 0; // frames that the HAL refused (nothing is retried: the sender of an answer already moved on)

static uint32_t TxLock(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void TxUnlock(uint32_t primask) {
    if (primask == 0u) {
        __enable_irq();
    }
}

// Moves frames from the ring to the mailboxes while there is a free one. The caller holds the lock.
static void TxPump(void) {
    CanTxEntry_t entry;
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0 && CanTxQueue_Peek(&entry)) {
        CAN_TxHeaderTypeDef txH;
        memset(&txH, 0, sizeof txH);
        txH.StdId = entry.std_id;
        txH.IDE = entry.ide;
        txH.RTR = entry.rtr;
        txH.DLC = entry.dlc;
        txH.TransmitGlobalTime = DISABLE;
        uint32_t mailbox;
        if (HAL_CAN_AddTxMessage(&hcan, &txH, entry.data, &mailbox) != HAL_OK) {
            tx_failed++;
        }
        CanTxQueue_Pop(NULL);
    }
}

HAL_StatusTypeDef CanTx_Submit(CAN_TxHeaderTypeDef *hdr, uint8_t *data, uint32_t *mailbox) {
    if (hdr == NULL || hdr->DLC > 8u || (hdr->DLC > 0u && data == NULL)) {
        return HAL_ERROR;
    }
    CanTxEntry_t entry;
    memset(&entry, 0, sizeof entry);
    entry.std_id = hdr->StdId;
    entry.ide = hdr->IDE;
    entry.rtr = hdr->RTR;
    entry.dlc = hdr->DLC;
    if (hdr->DLC > 0u) {
        memcpy(entry.data, data, hdr->DLC);
    }
    if (mailbox != NULL) {
        *mailbox = 0u; // the mailbox is not known yet: nobody reads it
    }
    uint32_t primask = TxLock();
    TxPump(); // makes room first if the mailboxes freed up since the last pass
    uint8_t stored = CanTxQueue_Push(&entry);
    TxPump();
    TxUnlock(primask);
    return stored ? HAL_OK : HAL_ERROR;
}

// What a sender asks before sending: the room left in the queue, not the mailboxes, so a burst of frames is no longer cut after the third.
uint32_t CanTx_FreeLevel(void) {
    uint32_t primask = TxLock();
    uint32_t room = CAN_TX_QUEUE_DEPTH - CanTxQueue_Count();
    TxUnlock(primask);
    return room;
}

void CanTx_Service(void) {
    uint32_t primask = TxLock();
    TxPump();
    TxUnlock(primask);
}

// After a bus-off the mailboxes are aborted so that stale answers are not sent in a burst; the frames waiting in the ring are as stale as those.
uint32_t CanTx_Discard(void) {
    uint32_t primask = TxLock();
    uint32_t held = CanTxQueue_Clear();
    TxUnlock(primask);
    return held;
}

uint32_t CanTx_Dropped(void) {
    return CanTxQueue_Dropped();
}

uint32_t CanTx_Failed(void) {
    return tx_failed;
}

#endif // URTC_CAN_TX_QUEUE
