// =============================================================================
// URTC Firmware - Saved-state load/save declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_PERSISTENCE_H
#define FIRMWARE_PERSISTENCE_H

// Loads recovered_state from F-RAM at boot (validating magic/version/
// checksum), restores the passive settings (LED colors, night mode,
// expansion board type) directly, and leaves everything else for the
// host to query over CAN (0x190/0x191) and decide what to do with.
void SavedState_Load(void);

// Called every main loop iteration - only actually writes to F-RAM when
// the current state differs from what's already saved AND at least
// FRAM_MIN_SAVE_INTERVAL_MS has passed since the last write.
void SavedState_MaybeSave(void);

#endif // FIRMWARE_PERSISTENCE_H
