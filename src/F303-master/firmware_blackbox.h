// =============================================================================
// URTC Firmware - "Black box" fault recorder declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_BLACKBOX_H
#define FIRMWARE_BLACKBOX_H

// Call once per ~150ms tick (same cadence as Render_ToolScreen - see
// STM32F303CC_main.c's own "Asynchronous loop for secondary telemetry
// and UI refresh" block). Appends one sample to the in-RAM ring buffer
// and, on the rising edge of system_error_flag (false -> true), flushes
// the last ~10s of samples to F-RAM exactly once for that fault - see
// firmware_blackbox.c's own header comment for the full reasoning.
void BlackBox_Sample(void);

#endif // FIRMWARE_BLACKBOX_H
