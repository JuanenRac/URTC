// =============================================================================
// URTC Firmware - Host tests of the bootloader authentication and addressing
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed. The suite itself is the copy of the shared one (tests/vectors/test_bootloader_auth.c, kept equal by the
// lock file of that folder); this file only lists it with the other host suites. It holds src/common/urtc_ed25519.c, urtc_auth.h and urtc_bl_select.h to RFC 8032 and
// to the shared vectors.
#include "vectors/test_bootloader_auth.c"
