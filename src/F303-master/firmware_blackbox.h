// =============================================================================
// URTC Firmware - "Black box" fault recorder declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_BLACKBOX_H
#define FIRMWARE_BLACKBOX_H

#include <stdint.h>

// Call once per ~150ms tick (same cadence as Render_ToolScreen - see
// STM32F303CC_main.c's own "Asynchronous loop for secondary telemetry
// and UI refresh" block). Appends one sample to the in-RAM ring buffer
// and, on the rising edge of system_error_flag (false -> true), flushes
// the last ~10s of samples to F-RAM exactly once for that fault - see
// firmware_blackbox.c's own header comment for the full reasoning.
void BlackBox_Sample(void);

// CAN readback (firmware_can_global_pre.c, 0x1A8 request / 0x1A9
// response) for the flushed record sitting in F-RAM - see
// BlackBox_HandleReadbackRequest()'s own doc comment in
// firmware_blackbox.c for the full chunked-transfer protocol.
void BlackBox_HandleReadbackRequest(uint8_t chunk_index);

#endif // FIRMWARE_BLACKBOX_H
