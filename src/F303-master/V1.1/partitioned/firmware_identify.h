// =============================================================================
// URTC Firmware - Physical tool identification (ID pin matrix) declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_IDENTIFY_H
#define FIRMWARE_IDENTIFY_H

// Reads the 5-bit ID-jumper matrix (ID0-ID4) with 3-consecutive-matching-
// reads debounce, and sets active_tool accordingly. Called once at boot,
// right after the ID pins are configured.
void Identify_PhysicalTool(void);

#endif // FIRMWARE_IDENTIFY_H
