// =============================================================================
// URTC Firmware - CAN dispatch declarations - global commands shared across all tools
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CAN_GLOBAL_H
#define FIRMWARE_CAN_GLOBAL_H

// Called first, before the first error-gate - read-only diagnostics and
// configuration that stay available even during a declared critical error.
void Handle_CAN_GlobalCommands_PreErrorGate(void);

// Called after the first error-gate passes (system_error_flag clear, or
// the ID was specifically 0x100) - everything here is skipped otherwise.
void Handle_CAN_GlobalCommands_PostErrorGate(void);

#endif // FIRMWARE_CAN_GLOBAL_H
