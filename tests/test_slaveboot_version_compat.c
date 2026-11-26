// =============================================================================
// URTC Firmware - Host tests of the version rule that the master may relax for one update of the expansion slave
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] the slave bootloader is compiled unchanged against the fake HAL, twice: with the register built in (-DURTC_SLAVE_VERSION_COMPAT=1) and as it
// ships (off). Off, there is no register and an image without a patch is refused over the same minor with a patch, as before. On, only the one value of
// urtc_version_compat.h relaxes the rule, only for the session in which it was written, and nothing is stored: a reset or a new START is strict again.
// The helpers of the update suite are reused by including it (its own main is renamed).
#define main update_suite_main
#include "test_slaveboot_update.c"
#undef main

#include "urtc_version_compat.h"

#ifndef URTC_SLAVE_VERSION_COMPAT
#define URTC_SLAVE_VERSION_COMPAT 0   // the bootloader as it ships
#endif
#define MAYBE_UNUSED __attribute__((unused))

static void installed(void)
{
    fresh();
    seed_counter = 0;
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
}

// START, digest, data, optionally the register, END and the confirmation of the master. `value` below 0: the register is not written.
static void send_update(uint16_t vminfield, int value)
{
    uint8_t digest[32];
    make_image(1500);
    sign_image(1500, digest);
    start_update(1500, THIS_HARDWARE_ID);
    HandleDigestExpected(digest);
    send_data(1500, 32);
#if URTC_SLAVE_VERSION_COMPAT
    if (value >= 0) {
        uint8_t v = (uint8_t)value;
        HandleVersionCompat(&v);
    }
#else
    (void)value;
#endif
    end_update(ref_crc32(image, 1500), 0, vminfield);
    confirm_now(1500, 0, vminfield, 0);
}

static int refused_as_rollback(void) { return current_status == STATUS_VERIFY_FAIL && verify_fail_reason == VERIFY_FAIL_REASON_ROLLBACK && main_slot_blank(); }

int main(void)
{
    int fails = 0;
    int *failures = &fails;
    printf("URTC host logic tests - the version rule of the expansion slave for one session (register %s)\n", URTC_SLAVE_VERSION_COMPAT ? "built in" : "off");
    void *m = mmap(0, IMG_MAX, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (m == MAP_FAILED || !fake_flash_init()) { printf("  SKIP: cannot map the fake flash on this host\n"); return 0; }
    image = m;
    image_addr = (uint32_t)(uintptr_t)m;

    installed();
    send_update(4, -1);
    TEST_ASSERT(refused_as_rollback(), "without the register an image without a patch is a rollback over 0.4.2, as before");
    installed();
    send_update(urtc_minor_field_pack(4, 3), -1);
    TEST_ASSERT(current_status == STATUS_VERIFY_OK, "an ordinary update installs as before");

#if URTC_SLAVE_VERSION_COMPAT
    installed();
    send_update(4, URTC_VC_SLAVE_ACCEPT);
    TEST_ASSERT(current_status == STATUS_VERIFY_OK && main_slot_equals_image(1500), "with the accept value the image without a patch installs");

    static const uint8_t others[] = { 0x00, 0x01, 0x02, 0x5A, 0xFF };
    int all_strict = 1;
    for (unsigned i = 0; i < sizeof others; i++) {
        installed();
        send_update(4, others[i]);
        all_strict &= refused_as_rollback();
    }
    TEST_ASSERT(all_strict, "no other value relaxes the rule");

    installed();
    send_update(urtc_minor_field_pack(4, 1), URTC_VC_SLAVE_ACCEPT);
    TEST_ASSERT(refused_as_rollback(), "a lower patch that is declared stays a rollback");
    installed();
    send_update(urtc_minor_field_pack(3, 0), URTC_VC_SLAVE_ACCEPT);
    TEST_ASSERT(refused_as_rollback(), "a lower minor stays a rollback");

    // written outside a session, or before the START that follows: nothing is kept
    installed();
    { uint8_t v = URTC_VC_SLAVE_ACCEPT; HandleVersionCompat(&v); }
    TEST_ASSERT(!update_in_progress && current_status == STATUS_LISTENING, "outside an update the register is ignored");
    send_update(4, -1);
    TEST_ASSERT(refused_as_rollback(), "and the next session is strict");
    installed();
    start_update(1500, THIS_HARDWARE_ID);
    { uint8_t v = URTC_VC_SLAVE_ACCEPT; HandleVersionCompat(&v); }
    send_update(4, -1);                                              // a new START (the master began again)
    TEST_ASSERT(refused_as_rollback(), "a new START forgets what the earlier session was told");

    // the floor of the signed counter is not touched by the register
    fresh();
    seed_counter = 5;
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    g_counter = 4;
    send_update(4, URTC_VC_SLAVE_ACCEPT);
    TEST_ASSERT(refused_as_rollback() && Counter_Floor() == 5, "a record below the rollback floor is refused whatever the register says");

#endif

    if (fails == 0) {
        printf("  OK   all slave version rule host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", fails);
    return 1;
}
