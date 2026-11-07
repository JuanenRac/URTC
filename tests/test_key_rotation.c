// =============================================================================
// URTC Firmware - Host tests of the rotation of the signing key
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed. The suite itself is the copy of the shared one (tests/vectors/test_key_rotation.c, kept equal by the lock
// file of that folder); this file only lists it with the other host suites. It holds src/common/urtc_key_rotation.h, the decision of which fingerprints a
// bootloader accepts and announces while a rotation is in progress, to the shared vectors. No bootloader uses it yet.
#include "vectors/test_key_rotation.c"
