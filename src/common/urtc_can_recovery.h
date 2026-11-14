// =============================================================================
// URTC - what the main loop does when the CAN peripheral reports an error
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Plain C99 with no STM32 HAL dependency, so tests/test_can_recovery.c exercises exactly the decision the main loop makes.
//
// The real recovery of the bus is made by the peripheral: the controller is built with AutoBusOff, so after a bus-off it comes back on its own once it has seen 128
// occurrences of 11 recessive bits. HAL_CAN_Start() on a controller that is already started returns an error and does nothing, so calling it is not the recovery.
// What the main loop adds is to throw away what was waiting in the three transmit mailboxes (a message sent after the bus comes back would carry data from before the
// fault), and that is worth doing only after a REAL bus-off. An error that is not a bus-off (a stuff error, a CRC error, one bit error) is retried by the controller by
// itself, and aborting the mailboxes at each of them threw away answers (0x7F9, 0x7F5...) that were still good.
#ifndef URTC_CAN_RECOVERY_H
#define URTC_CAN_RECOVERY_H

#include <stdint.h>

// CAN_ESR bit 2: the controller is in bus-off.
#define URTC_CAN_ESR_BOFF 0x00000004u

// 1 when the transmit mailboxes must be aborted after an error callback, given the error status register read at that moment.
static inline uint8_t urtc_can_recovery_abort_tx(uint32_t esr)
{
    return (esr & URTC_CAN_ESR_BOFF) ? 1u : 0u;
}

#endif // URTC_CAN_RECOVERY_H
