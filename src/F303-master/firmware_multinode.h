// =============================================================================
// URTC Firmware - Several tool heads on one CAN bus, board side: declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The switch is OFF by default and is read from F-RAM once at boot (a blank or
// erased F-RAM reads as off). While it is off every function here returns at
// once and the board behaves exactly as a single head, as in earlier releases.
// The logic is in multinode.c; this file is the part that touches the HAL.
#ifndef FIRMWARE_MULTINODE_H
#define FIRMWARE_MULTINODE_H

#include <stdint.h>

// Once at boot, after the tool is identified and before the CAN peripheral starts.
void MultiNode_Init(void);

// First thing in the receive callback. Returns 1 when the frame must go no further
// (consumed by the extension, or ignored because this node is not selected); 0 when
// the single-head dispatcher handles it as before. Always 0 while the switch is off.
uint8_t MultiNode_FilterRx(void);

// Main loop: sends the discovery answer and the periodic status frame. No-op while off.
void MultiNode_Service(void);

// 1 when this board may send single-head telemetry. Always 1 while the switch is off.
uint8_t MultiNode_SingleHeadTxAllowed(void);

// 0x1AC: query or change the persistent switch (the change needs a restart to act).
void MultiNode_HandleConfigCommand(void);

// Supplied by firmware_multinode_platform.c (hardware only, not compiled on the host).
void MultiNode_PlatformSafeState(void);
uint32_t MultiNode_PlatformSerial(void);

#endif // FIRMWARE_MULTINODE_H
