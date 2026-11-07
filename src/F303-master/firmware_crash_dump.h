// =============================================================================
// URTC Firmware - crash capture: start-up check and the CAN readback (0x1BC asks, 0x1BD answers)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CRASH_DUMP_H
#define FIRMWARE_CRASH_DUMP_H

#include <stdint.h>

#include "crash_dump.h"

#if URTC_CRASH_DUMP
// Once at start-up, with the reset cause already latched (last_reset_cause): checks the record the previous run may have left and stamps this reset into it.
void CrashDump_AppBoot(uint8_t reset_cause);
// 0x1BC: operation 0 answers with the six frames of the record on 0x1BD, whatever the active tool or the error state (read-only). Other operations: no answer.
void CrashDump_HandleQuery(uint8_t operation);
#else
// Switched off: no code, 0x1BC is an identifier the firmware does not know.
static inline void CrashDump_AppBoot(uint8_t reset_cause) { (void)reset_cause; }
static inline void CrashDump_HandleQuery(uint8_t operation) { (void)operation; }
#endif

#endif // FIRMWARE_CRASH_DUMP_H
