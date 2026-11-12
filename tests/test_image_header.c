// =============================================================================
// URTC Firmware - Host tests of the header of a firmware image
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed. The suite itself is the copy of the shared one (tests/vectors/test_image_header.c, kept equal by the lock
// file of that folder); this file only lists it with the other host suites. It holds src/common/urtc_image_header.h, the header that sits inside the image of the
// application at the fixed offset 0x200 (the slot the firmware emits and the check of a sealed image), to the shared vectors.
#include "vectors/test_image_header.c"
