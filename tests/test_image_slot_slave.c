// =============================================================================
// URTC Firmware - Host tests of the header slot of the expansion slave application image
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed. The slot the firmware emits (src/F303-slave/slave_image_slot.c) must state the hardware and the version
// of slave_common.h, be refused while it is not sealed, and, once the length and the CRC-32 of the whole image are filled in the way tools/seal_image.py does,
// be accepted by urtc_image_verify_embedded and by nothing that is not the same image.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32f3xx_hal.h"
#include "slave_common.h"
#include "urtc_image_header.h"
#include "vectors/urtc_test.h"

extern const uint8_t urtc_image_header_slot[URTC_IMAGE_HEADER_SIZE];

#define IMAGE_SIZE 1024u

// What the post-build step does: the length of the image and the CRC-32 of the image with the 32 bytes of the header taken as zeros.
static void seal(uint8_t *image, size_t total)
{
    uint8_t header[URTC_IMAGE_HEADER_SIZE];
    memcpy(header, image + URTC_IMAGE_EMBED_OFFSET, sizeof header);
    memset(image + URTC_IMAGE_EMBED_OFFSET, 0, URTC_IMAGE_HEADER_SIZE);
    urtc_image_info_t info;
    memset(&info, 0, sizeof info);
    info.hardware_id = urtc_image_get32(header + 8);
    info.version_major = urtc_image_get16(header + 12);
    info.version_minor = header[14];
    info.version_patch = header[15];
    info.version_build = urtc_image_get16(header + 16);
    info.flags = urtc_image_get16(header + 6);
    info.payload_length = (uint32_t)total;
    info.payload_crc32 = urtc_image_crc32(image, total);
    urtc_image_header_pack(&info, header);
    memcpy(image + URTC_IMAGE_EMBED_OFFSET, header, sizeof header);
}

int main(void)
{
    int count = 0;
    int *failures = &count;

    uint8_t image[IMAGE_SIZE];
    for (size_t i = 0; i < sizeof image; i++) {
        image[i] = (uint8_t)((i * 31u + 7u) & 0x7Fu);
    }
    memcpy(image + URTC_IMAGE_EMBED_OFFSET, urtc_image_header_slot, URTC_IMAGE_HEADER_SIZE);

    // what the slot states is what the headers of the firmware say
    TEST_ASSERT(urtc_image_get32(urtc_image_header_slot) == URTC_IMAGE_MAGIC, "the slot starts with the magic");
    TEST_ASSERT(urtc_image_get32(urtc_image_header_slot + 8) == (uint32_t)THIS_HARDWARE_ID, "the slot names the hardware of slave_common.h");
    TEST_ASSERT(urtc_image_get16(urtc_image_header_slot + 12) == FIRMWARE_VERSION_MAJOR && urtc_image_header_slot[14] == FIRMWARE_VERSION_MINOR &&
                urtc_image_header_slot[15] == FIRMWARE_VERSION_PATCH && urtc_image_get16(urtc_image_header_slot + 16) == 0,
                "the slot states the four components of the version");
    TEST_ASSERT((urtc_image_get16(urtc_image_header_slot + 6) & URTC_IMAGE_FLAG_EMBEDDED) != 0u, "the slot says it is the embedded form");
    TEST_ASSERT(urtc_image_get32(urtc_image_header_slot + 28) == 0u, "the header CRC is zero until the image is sealed");

    urtc_image_info_t info;
    TEST_ASSERT(urtc_image_verify_embedded(image, sizeof image, &info) == URTC_IMAGE_NOT_SEALED, "an image that was not sealed is refused as such");

    seal(image, sizeof image);
    memset(&info, 0, sizeof info);
    TEST_ASSERT(urtc_image_verify_embedded(image, sizeof image, &info) == URTC_IMAGE_OK, "the sealed image verifies");
    TEST_ASSERT(info.hardware_id == (uint32_t)THIS_HARDWARE_ID && info.payload_length == sizeof image, "and states the hardware and the length of the image");

    image[600] ^= 1u;
    TEST_ASSERT(urtc_image_verify_embedded(image, sizeof image, &info) == URTC_IMAGE_BAD_PAYLOAD_CRC, "one changed bit of the code is found");
    image[600] ^= 1u;
    image[4] ^= 1u;
    TEST_ASSERT(urtc_image_verify_embedded(image, sizeof image, &info) == URTC_IMAGE_BAD_PAYLOAD_CRC, "one changed bit of the vector table is found");
    image[4] ^= 1u;
    TEST_ASSERT(urtc_image_verify_embedded(image, sizeof image - 1u, &info) == URTC_IMAGE_BAD_LENGTH, "an image one byte short is found");

    if (count == 0) {
        printf("all image slot tests pass\n");
        return 0;
    }
    printf("%d image slot test(s) failed\n", count);
    return 1;
}
