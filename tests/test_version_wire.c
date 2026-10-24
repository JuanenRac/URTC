// =============================================================================
// URTC Firmware - Host logic tests for urtc_version_wire.h (PATCH number
// travelling over CAN / the slave link)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "urtc_version_wire.h"
#include "vectors/urtc_fw_vectors.h" // written by URTC-COMMON from its conformance vectors

// What an already-released flasher does with the slave answer: it needs the
// first 8 bytes of 0x217 and the first 2 bytes of 0x218 and ignores the rest.
static void legacy_slave_decode(const uint8_t p1[8], const uint8_t *p2, uint8_t p2_len,
                                uint16_t *app_minor, uint8_t *boot_major, int *ok)
{
    *ok = (p2_len >= 2);
    if (!*ok) return;
    *app_minor = (uint16_t)((p1[7] << 8) | p2[0]);
    *boot_major = p2[1];
}

void run_version_wire_tests(int *failures)
{
    // The cases URTC-COMMON wrote for every implementation of the version fields (the three programs read the same ones).
    {
        int packed_bad = 0, accepting = 0, order_bad = 0;
        for (int i = 0; i < URTC_VEC_COUNT(URTC_VEC_MINOR); i++) {
            const urtc_vec_minor_t *c = &URTC_VEC_MINOR[i];
            if (!c->accepts_patch) continue; // a host decision (the bootloader predates the patch), not the firmware's
            accepting++;
            if (urtc_minor_field_pack(c->minor, c->patch) != c->expect) packed_bad++;
        }
        TEST_ASSERT(accepting >= 6 && packed_bad == 0, "urtc_minor_field_pack gives the minor fields of the shared vectors");
        for (int i = 0; i < URTC_VEC_COUNT(URTC_VEC_ORDER); i++) {
            const urtc_vec_order_t *c = &URTC_VEC_ORDER[i];
            int older = urtc_version_is_older(c->cand_major, urtc_minor_field_pack(c->cand_minor, c->cand_patch),
                                              c->inst_major, urtc_minor_field_pack(c->inst_minor, c->inst_patch));
            if (older != (c->expect < 0)) order_bad++;
        }
        TEST_ASSERT(URTC_VEC_COUNT(URTC_VEC_ORDER) >= 9 && order_bad == 0, "urtc_version_is_older agrees with every ordering of the shared vectors");
    }

    // Round trip over every minor and every patch, and the "unknown" encodings.
    int roundtrip_ok = 1;
    for (unsigned minor = 0; minor < 256; minor++) {
        for (unsigned patch = 0; patch <= URTC_PATCH_MAX; patch++) {
            uint16_t f = urtc_minor_field_pack((uint8_t)minor, (uint8_t)patch);
            if (urtc_minor_field_minor(f) != minor || urtc_minor_field_patch(f) != patch) roundtrip_ok = 0;
            if ((uint8_t)f != minor) roundtrip_ok = 0; // the legacy uint8 cast still yields the minor
        }
        uint16_t u = urtc_minor_field_pack((uint8_t)minor, URTC_PATCH_UNKNOWN);
        if (u != minor || urtc_minor_field_patch(u) != URTC_PATCH_UNKNOWN) roundtrip_ok = 0;
    }
    TEST_ASSERT(roundtrip_ok, "minor/patch pack and unpack round trip for every value");

    // A legacy frame (high byte 0) never claims a patch, including patch 0.
    TEST_ASSERT(urtc_minor_field_patch(0x0003) == URTC_PATCH_UNKNOWN, "legacy minor field has no patch");
    TEST_ASSERT(urtc_minor_field_patch(0x8003) == 0, "flag set with patch 0 is patch 0, not unknown");
    TEST_ASSERT(urtc_minor_field_pack(3, 0) == 0x8003, "patch 0 is encoded with the flag");
    TEST_ASSERT(urtc_minor_field_pack(3, 0x80) == 0x0003, "a patch above 127 is dropped, field stays legacy");

    // Anti-rollback ordering. (new_major, new_field, cur_major, cur_field)
    TEST_ASSERT(!urtc_version_is_older(0, urtc_minor_field_pack(3, 4), 0, urtc_minor_field_pack(3, 4)), "same version is not older");
    TEST_ASSERT(urtc_version_is_older(0, urtc_minor_field_pack(3, 3), 0, urtc_minor_field_pack(3, 4)), "lower patch is older when both known");
    TEST_ASSERT(!urtc_version_is_older(0, urtc_minor_field_pack(3, 5), 0, urtc_minor_field_pack(3, 4)), "higher patch is not older");
    TEST_ASSERT(!urtc_version_is_older(0, 3, 0, urtc_minor_field_pack(3, 4)), "legacy image over same major.minor with patch is accepted");
    TEST_ASSERT(!urtc_version_is_older(0, urtc_minor_field_pack(3, 1), 0, 3), "image with patch over legacy same major.minor is accepted");
    TEST_ASSERT(urtc_version_is_older(0, urtc_minor_field_pack(2, 9), 0, 3), "lower minor is older whatever the patch");
    TEST_ASSERT(!urtc_version_is_older(0, urtc_minor_field_pack(4, 0), 0, urtc_minor_field_pack(3, 9)), "higher minor beats a higher patch");
    TEST_ASSERT(urtc_version_is_older(0, 9, 1, 0), "lower major is older");
    TEST_ASSERT(!urtc_version_is_older(1, 0, 0, 9), "higher major is not older");
    TEST_ASSERT(!urtc_version_is_older(0, 3, 0, 3), "two legacy equal versions are not older");
    TEST_ASSERT(urtc_version_is_older(0, 2, 0, 3), "two legacy versions still compare by minor");

    // 0x7E9 companion frame.
    uint8_t pf[URTC_VERSION_PATCH_FRAME_DLC];
    urtc_version_patch_frame_build(pf, 0x01, 7, URTC_PATCH_FLAG_END_UPDATE_PATCH);
    const uint8_t want_pf[3] = {0x01, 0x07, 0x01};
    TEST_ASSERT(memcmp(pf, want_pf, 3) == 0, "0x7E9 payload is side, patch, flags");
    urtc_version_patch_frame_build(pf, 0x00, URTC_PATCH_UNKNOWN, 0);
    const uint8_t want_pf2[3] = {0x00, 0xFF, 0x00};
    TEST_ASSERT(memcmp(pf, want_pf2, 3) == 0, "0x7E9 from an application with no patch flag");
    TEST_ASSERT(URTC_CAN_ID_VERSION_PATCH == 0x7E9u, "0x7E9 is the companion ID");

    // Patch of the slave application: byte 9 of REG_APP_VERSION, 0 means "not sent".
    uint8_t av[URTC_SLAVE_APP_VERSION_LEN] = {0x00, 0x03, 0x03, 0xCB, 0x01, 0x00, 0x00, 0x00, 0x02, 0x04};
    {
        int listed = 0;
        for (int i = 0; i < URTC_VEC_COUNT(URTC_VEC_IDENTIFIERS); i++) if (URTC_VEC_IDENTIFIERS[i] == URTC_CAN_ID_SLAVE_APP_PATCH) listed = 1;
        TEST_ASSERT(listed, "0x222 is an identifier of the shared specification");
    }
    TEST_ASSERT(urtc_slave_app_patch_from_version(av) == 4, "the tenth byte is the patch of the slave application");
    av[9] = 0;
    TEST_ASSERT(urtc_slave_app_patch_from_version(av) == URTC_PATCH_UNKNOWN, "0 (the reserved byte of an older slave) is not a patch");
    av[9] = 0x7F;
    TEST_ASSERT(urtc_slave_app_patch_from_version(av) == 0x7F, "127 is the largest patch");
    av[9] = 0x80;
    TEST_ASSERT(urtc_slave_app_patch_from_version(av) == URTC_PATCH_UNKNOWN, "above 127 it is not a patch");
    av[9] = 0xFF;
    TEST_ASSERT(urtc_slave_app_patch_from_version(av) == URTC_PATCH_UNKNOWN, "0xFF stays the unknown marker");
    uint8_t apf[URTC_SLAVE_APP_PATCH_FRAME_DLC];
    urtc_slave_app_patch_frame_build(apf, 6, 12);
    TEST_ASSERT(apf[0] == 0x01 && apf[1] == 6 && apf[2] == 12 && URTC_CAN_ID_SLAVE_APP_PATCH == 0x222u && URTC_SLAVE_APP_PATCH_FRAME_DLC == 3u && URTC_SLAVE_APP_PATCH_FRAME_MIN_DLC == 2u, "0x222 payload is the register, the patch and the low byte of the minor");
    urtc_slave_app_patch_frame_build(apf, 6, 0);
    TEST_ASSERT(apf[2] == 0, "a minor of 0 is sent as a byte of 0, not left out");

    // Slave extension.
    uint8_t ext[URTC_SLAVE_VERSION_EXT_LEN];
    urtc_slave_version_ext_build(ext, 9, 2, 5);
    const uint8_t want_ext[4] = {URTC_SLAVE_VERSION_EXT_MARKER, 9, 2, 5};
    TEST_ASSERT(memcmp(ext, want_ext, 4) == 0, "slave extension is marker, app patch, boot minor, boot patch");
    TEST_ASSERT(URTC_SLAVE_VERSION_EXT_MARKER != 0x00 && URTC_SLAVE_VERSION_EXT_MARKER != 0xFF
                && (URTC_SLAVE_VERSION_EXT_MARKER > 0x07), "marker can never be mistaken for a status code");

    uint8_t legacy[2] = {0x04, 0x01};
    uint8_t part2[5];
    memset(part2, 0xAA, sizeof(part2));
    TEST_ASSERT(urtc_slave_version_part2_build(part2, legacy, 0) == 2, "no extension: legacy DLC 2");
    TEST_ASSERT(part2[0] == 0x04 && part2[1] == 0x01, "no extension: the 2 legacy bytes are untouched");
    uint8_t status_instead[4] = {0x01, 0, 0, 0}; // what an old slave bootloader answers: its status
    TEST_ASSERT(urtc_slave_version_part2_build(part2, legacy, status_instead) == 2, "status byte instead of marker: legacy DLC 2");
    TEST_ASSERT(urtc_slave_version_part2_build(part2, legacy, ext) == 5, "extension present: DLC 5");
    const uint8_t want_p2[5] = {0x04, 0x01, 9, 2, 5};
    TEST_ASSERT(memcmp(part2, want_p2, 5) == 0, "0x218 extended payload layout");

    // A released flasher reads the extended answer exactly as the legacy one.
    uint8_t p1[8] = {0x01, 0x03, 0x03, 0xCB, 0x01, 0x00, 0x00, 0x00};
    uint16_t minor_a = 0, minor_b = 0; uint8_t bmaj_a = 0, bmaj_b = 0; int ok_a = 0, ok_b = 0;
    uint8_t p2_legacy[2] = {0x04, 0x01};
    legacy_slave_decode(p1, p2_legacy, 2, &minor_a, &bmaj_a, &ok_a);
    legacy_slave_decode(p1, part2, 5, &minor_b, &bmaj_b, &ok_b);
    TEST_ASSERT(ok_a && ok_b && minor_a == minor_b && bmaj_a == bmaj_b, "legacy decoder gives identical results on DLC 2 and DLC 5");

    // The slave metadata minor reported to a legacy flasher carries no patch bits.
    uint16_t stored = urtc_minor_field_pack(4, 9);
    uint16_t reported = (uint16_t)urtc_minor_field_minor(stored);
    TEST_ASSERT(reported == 4, "reported slave minor has the patch stripped");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - urtc_version_wire.h\n");
    run_version_wire_tests(&failures);
    if (failures == 0) {
        printf("  OK   all version wire host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
