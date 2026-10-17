// =============================================================================
// URTC - version wire helpers shared by the main/slave bootloaders and the
// main application. Plain C99 with no STM32 HAL dependency, so
// tests/test_version_wire.c exercises exactly the code that runs on the chips.
//
// Why this exists: 0x7F9 and 0x7F4 already use all 8 bytes of a classic CAN
// frame, and 0x7F6/0x7FA are parsed by length in already-released flashers,
// so the PATCH number cannot ride in those frames without breaking them. It
// travels in three additive places instead (see docs/CANBUS.TXT, "Firmware
// PATCH number"):
//   1. A companion frame 0x7E9 sent right after 0x7F9 by the application and
//      by the bootloaders (app patch + capability flags).
//   2. The high byte of the 16-bit minor field of 0x7F4 / 0x214: it is always
//      0 in a legacy frame, so bit 7 marks "patch present" and bits 6..0
//      carry the patch.
//   3. Three extra bytes on the slave version answer (0x218).
// =============================================================================
#ifndef URTC_VERSION_WIRE_H
#define URTC_VERSION_WIRE_H

#include <stdint.h>

#define URTC_CAN_ID_VERSION_PATCH 0x7E9u // companion of 0x7F9, sent by whichever side answered 0x7F8

#define URTC_PATCH_UNKNOWN      0xFFu // the patch number is not known (legacy metadata, blank board)
#define URTC_MINOR_PATCH_FLAG   0x80u // in the high byte of a minor field: a patch number follows in bits 6..0
#define URTC_PATCH_MAX          0x7Fu

#define URTC_PATCH_FLAG_END_UPDATE_PATCH 0x01u // 0x7E9 flags bit 0: this bootloader reads the patch from the minor field of 0x7F4 / 0x214
#define URTC_SLAVE_VERSION_EXT_MARKER    0xE1u // first byte of the slave extended version register; never a status code

#define URTC_VERSION_PATCH_FRAME_DLC 3u
#define URTC_SLAVE_VERSION_EXT_LEN   4u

// Packs a minor number and an optional patch into the 16-bit minor field.
// patch == URTC_PATCH_UNKNOWN (or above URTC_PATCH_MAX) yields a legacy field.
static inline uint16_t urtc_minor_field_pack(uint8_t minor, uint8_t patch)
{
    if (patch > URTC_PATCH_MAX) {
        return (uint16_t)minor;
    }
    return (uint16_t)((((uint16_t)(URTC_MINOR_PATCH_FLAG | patch)) << 8) | minor);
}

static inline uint8_t urtc_minor_field_minor(uint32_t field)
{
    return (uint8_t)(field & 0xFFu);
}

static inline uint8_t urtc_minor_field_patch(uint32_t field)
{
    uint8_t hi = (uint8_t)((field >> 8) & 0xFFu);
    return (hi & URTC_MINOR_PATCH_FLAG) ? (uint8_t)(hi & URTC_PATCH_MAX) : URTC_PATCH_UNKNOWN;
}

// Returns 1 when the incoming image is OLDER than the installed one.
// major and minor always decide; the patch only decides when BOTH sides know
// it, so an image declared by a legacy flasher (no patch) is never rejected
// against a same major.minor installed with a patch, and the other way round.
static inline int urtc_version_is_older(uint32_t new_major, uint32_t new_minor_field,
                                        uint32_t cur_major, uint32_t cur_minor_field)
{
    uint8_t new_minor = urtc_minor_field_minor(new_minor_field);
    uint8_t cur_minor = urtc_minor_field_minor(cur_minor_field);
    uint8_t new_patch = urtc_minor_field_patch(new_minor_field);
    uint8_t cur_patch = urtc_minor_field_patch(cur_minor_field);

    if (new_major != cur_major) {
        return new_major < cur_major;
    }
    if (new_minor != cur_minor) {
        return new_minor < cur_minor;
    }
    if (new_patch != URTC_PATCH_UNKNOWN && cur_patch != URTC_PATCH_UNKNOWN) {
        return new_patch < cur_patch;
    }
    return 0;
}

// 0x7E9 payload: [0] answering side (same values as 0x7F9 byte 0),
// [1] installed application patch or URTC_PATCH_UNKNOWN, [2] flags.
static inline void urtc_version_patch_frame_build(uint8_t out[URTC_VERSION_PATCH_FRAME_DLC],
                                                  uint8_t side, uint8_t app_patch, uint8_t flags)
{
    out[0] = side;
    out[1] = app_patch;
    out[2] = flags;
}

// Slave extended version register / 0x218 tail:
// [0] marker, [1] installed slave application patch or unknown,
// [2] slave bootloader minor, [3] slave bootloader patch.
static inline void urtc_slave_version_ext_build(uint8_t out[URTC_SLAVE_VERSION_EXT_LEN],
                                                uint8_t app_patch, uint8_t boot_minor, uint8_t boot_patch)
{
    out[0] = URTC_SLAVE_VERSION_EXT_MARKER;
    out[1] = app_patch;
    out[2] = boot_minor;
    out[3] = boot_patch;
}

// Builds the 0x218 payload on the master from the 2 legacy bytes read from the
// slave plus the optional extension. Returns the DLC: 2 without the extension
// (legacy answer, byte for byte), 5 with it.
static inline uint8_t urtc_slave_version_part2_build(uint8_t out[5], const uint8_t legacy[2],
                                                     const uint8_t *ext /* NULL or 4 bytes */)
{
    out[0] = legacy[0];
    out[1] = legacy[1];
    if (ext == 0 || ext[0] != URTC_SLAVE_VERSION_EXT_MARKER) {
        return 2u;
    }
    out[2] = ext[1];
    out[3] = ext[2];
    out[4] = ext[3];
    return 5u;
}

#endif // URTC_VERSION_WIRE_H
