// =============================================================================
// URTC-COMMON - tests/c/test_image_header.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// firmware_base/urtc_image_header.h held to the shared vectors (generated/image_header_vectors.h, written from the Python reference): every image gets the
// status the reference gives, a good one gives the fields the reference reads and packs back to the same 32 bytes. Built by tests/test_image_header.py with the
// host compiler and the address and undefined-behaviour sanitizers.
#include <stdio.h>
#include <string.h>

#include "image_header_embedded_vectors.h"
#include "image_header_vectors.h"
#include "urtc_image_header.h"
#include "urtc_test.h"

int main(void)
{
    int count = 0;
    int *failures = &count;

    TEST_ASSERT(urtc_image_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u, "the CRC-32 check value of the text 123456789");
    TEST_ASSERT(urtc_image_crc32(NULL, 0) == 0u, "the CRC-32 of nothing is zero");
    const uint8_t seven[] = {1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT((urtc_image_crc32_update(urtc_image_crc32_update(0xFFFFFFFFu, seven, 3), seven + 3, 4) ^ 0xFFFFFFFFu) == urtc_image_crc32(seven, 7), "the CRC-32 is incremental");

    for (size_t i = 0; i < IMAGE_HEADER_VECTOR_COUNT; i++) {
        const image_header_vec_t *v = &IMAGE_HEADER_VECTORS[i];
        urtc_image_info_t info;
        memset(&info, 0xEE, sizeof info);
        const urtc_image_status_t status = urtc_image_verify(v->image, v->length, &info);
        if ((int)status != v->status) {
            printf("  FAIL: %s: status %d, expected %d\n", v->name, (int)status, v->status);
            count++;
            continue;
        }
        if (!v->has_info) {
            continue;
        }
        const int same = info.hardware_id == v->hardware_id && info.version_major == v->major && info.version_minor == v->minor && info.version_patch == v->patch &&
                         info.version_build == v->build && info.flags == v->flags && info.payload_length == v->payload_length && info.payload_crc32 == v->payload_crc32;
        TEST_ASSERT(same, v->name);
        uint8_t packed[URTC_IMAGE_HEADER_SIZE];
        urtc_image_header_pack(&info, packed);
        TEST_ASSERT(memcmp(packed, v->image, URTC_IMAGE_HEADER_SIZE) == 0, "a good header packs back to the same 32 bytes");
    }

    // the embedded form: the header inside the binary at a fixed offset
    for (size_t i = 0; i < IMAGE_HEADER_EMBEDDED_VECTOR_COUNT; i++) {
        const image_header_embedded_vec_t *v = &IMAGE_HEADER_EMBEDDED_VECTORS[i];
        urtc_image_info_t info;
        memset(&info, 0xEE, sizeof info);
        const urtc_image_status_t status = urtc_image_verify_embedded(v->image, v->length, &info);
        if ((int)status != v->status) {
            printf("  FAIL: embedded: %s: status %d, expected %d\n", v->name, (int)status, v->status);
            count++;
            continue;
        }
        if (!v->has_info) {
            continue;
        }
        const int same = info.hardware_id == v->hardware_id && info.version_major == v->major && info.version_minor == v->minor && info.version_patch == v->patch &&
                         info.version_build == v->build && info.flags == v->flags && info.payload_length == v->payload_length && info.payload_crc32 == v->payload_crc32;
        TEST_ASSERT(same, v->name);
        TEST_ASSERT((info.flags & URTC_IMAGE_FLAG_EMBEDDED) != 0u, "a good embedded header says it is embedded");
    }
    // the slot a firmware emits is the one the reference writes, and an image that carries it unsealed is refused as such
    {
        static const uint8_t slot[URTC_IMAGE_HEADER_SIZE] = URTC_IMAGE_EMBED_SLOT(0x0303CC01u, 0u, 8u, 1u, 3u);
        int found = 0;
        for (size_t i = 0; i < IMAGE_HEADER_EMBEDDED_VECTOR_COUNT; i++) {
            const image_header_embedded_vec_t *v = &IMAGE_HEADER_EMBEDDED_VECTORS[i];
            if (v->status == (int)URTC_IMAGE_NOT_SEALED) {
                found = 1;
                TEST_ASSERT(memcmp(v->image + URTC_IMAGE_EMBED_OFFSET, slot, URTC_IMAGE_HEADER_SIZE) == 0, "URTC_IMAGE_EMBED_SLOT is the slot of the reference");
            }
        }
        TEST_ASSERT(found, "the vectors hold an unsealed slot");
    }
    {
        urtc_image_info_t none;
        TEST_ASSERT(urtc_image_verify_embedded(NULL, 1024, &none) == URTC_IMAGE_SHORT, "a missing image is short");
        TEST_ASSERT(urtc_image_verify_embedded(IMAGE_HEADER_EMBEDDED_VECTORS[0].image, IMAGE_HEADER_EMBEDDED_VECTORS[0].length, NULL) == URTC_IMAGE_SHORT, "a missing result is refused");
    }

    // arguments that are missing, and a header read on its own
    urtc_image_info_t spare;
    TEST_ASSERT(urtc_image_header_parse(NULL, 64, &spare) == URTC_IMAGE_SHORT, "a missing header is short");
    TEST_ASSERT(urtc_image_header_parse(IMAGE_HEADER_VECTORS[0].image, 64, NULL) == URTC_IMAGE_SHORT, "a missing result is refused");
    TEST_ASSERT(urtc_image_header_parse(IMAGE_HEADER_VECTORS[0].image, URTC_IMAGE_HEADER_SIZE, &spare) == URTC_IMAGE_OK, "the header of an image reads without its payload");

    if (count == 0) {
        printf("all image header tests pass\n");
        return 0;
    }
    printf("%d image header test(s) failed\n", count);
    return 1;
}
