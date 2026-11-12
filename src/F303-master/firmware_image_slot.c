// =============================================================================
// URTC Firmware - the header slot of the main board application image
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The image carries its own identity: 32 bytes at the fixed offset 0x200, after the vector table (STM32F303CCTx_APP.ld puts this section there and fails the link when
// the table would not fit before it). The firmware states what it knows when it is compiled: the hardware it is for and its version, with the four components. The
// length, the CRC-32 and the header CRC are filled in by the step after the link (tools/seal_image.py), because they depend on the whole image; until then the slot
// is refused as "never sealed". The flash map and the vector table do not move, and the image is no longer than it was.
#include "firmware_common.h"
#include "urtc_image_header.h"

__attribute__((section(".urtc_image_header"), used))
const uint8_t urtc_image_header_slot[URTC_IMAGE_HEADER_SIZE] =
    URTC_IMAGE_EMBED_SLOT(THIS_HARDWARE_ID, FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH, FIRMWARE_VERSION_BUILD);
