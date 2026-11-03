// =============================================================================
// URTC Firmware - Host logic tests of the release trust table of the bootloader
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// bootloader_trust.c is built here WITHOUT -DURTC_TRUST_DEV, as an official bootloader is: it holds the release table of keys/image_trust.json. While that
// file has no image-signing key the table is the closed one and the bootloader trusts nobody (it installs nothing: it fails closed); once the key is there
// the table holds it, and never the public development key.
#include <stdio.h>
#include <string.h>

#include "vectors/urtc_test.h"
#include "bootloader_trust.h"
#include "boot_sig_cases.h"
#include "urtc_trust_release.h"

int main(void)
{
    int failures_count = 0;
    int *failures = &failures_count;
    printf("URTC host logic tests - the release trust table\n");

    extern const uint8_t URTC_TRUST_TABLE[];
    static const uint8_t compiled[] = { URTC_TRUST_TABLE_BYTES };
    TEST_ASSERT(sizeof compiled == URTC_TRUST_TABLE_SIZE && memcmp(URTC_TRUST_TABLE, compiled, sizeof compiled) == 0, "the compiled table is the release table of the generated header");
    TEST_ASSERT(memcmp(URTC_TRUST_TABLE, "TKEY", 4) == 0, "the table starts with its magic");

    const TrustKeys_t *trusted = Trust_Active();
    if (URTC_TRUST_TABLE_SIZE == 12u) {
        TEST_ASSERT(trusted->count == 0, "no image-signing key is provisioned: the official bootloader trusts nobody and installs nothing");
    } else {
        TEST_ASSERT(trusted->count >= 1, "the release table holds the image-signing key");
    }
    int dev_key_trusted = 0;
    for (uint8_t i = 0; i < trusted->count; i++) {
        if (memcmp(trusted->keys[i], BOOT_SIG_PUBLIC_DEV, 32) == 0) dev_key_trusted = 1;
    }
    TEST_ASSERT(!dev_key_trusted, "an official bootloader never trusts the public development key");
    TEST_ASSERT(Trust_MinCounter() == URTC_TRUST_MIN_COUNTER, "the compiled floor of the counter is the one of the public file");

    if (failures_count == 0) {
        printf("  OK   all release trust table host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures_count);
    return 1;
}
