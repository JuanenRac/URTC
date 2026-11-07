// =============================================================================
// URTC Firmware - Host tests of the signing key rotation inside the bootloader
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] boot/bootloader_protocol.c is compiled unchanged against the fake HAL, twice: with the rotation command built in (-DURTC_BL_ROTATION_COMMAND=1) and
// as it ships (off). The signed commands come from tests/boot_sig_cases.h (throwaway test keys). With the command on, the bootloader trusts the current key of its
// table alone until a command signed by that key names the new current key and a window for the previous one; the window runs on the uptime shifted by the
// time of the host; a reset forgets it. Off, nothing of this exists: every key of the table verifies and 0x7EA operation 05 is not answered.
// The helpers of the update suite are reused by including it (its own main is renamed).
#define main update_suite_main
#include "test_bootloader_update.c"
#undef main

#include "urtc_key_rotation.h"
#include "urtc_bl_select.h"

#ifndef URTC_BL_ROTATION_COMMAND
#define URTC_BL_ROTATION_COMMAND 0   // the bootloader as it ships
#endif
#define MAYBE_UNUSED __attribute__((unused))

#define ROT_FRAMES 15

MAYBE_UNUSED static const BootRotationCase *rotation_case(const char *name)
{
    for (unsigned i = 0; i < BOOT_ROTATION_CASE_COUNT; i++)
        if (strcmp(BOOT_ROTATION_CASES[i].name, name) == 0) return &BOOT_ROTATION_CASES[i];
    printf("  MISSING rotation case %s\n", name);
    return 0;
}

// The first `chunks` of the 15 frames of a command on 0x7EC: 0x80 | index, then 7 bytes (the last holds 2 and zeros).
MAYBE_UNUSED static void send_rotation_chunks(const uint8_t record[100], int chunks)
{
    for (int i = 0; i < chunks; i++) {
        uint8_t f[8] = { (uint8_t)(0x80u | (unsigned)i), 0, 0, 0, 0, 0, 0, 0 };
        for (int k = 0; k < 7; k++) {
            int at = i * 7 + k;
            f[1 + k] = at < 100 ? record[at] : 0;
        }
        HandleSigChunk(f);
    }
}

MAYBE_UNUSED static void send_rotation(const char *name)
{
    const BootRotationCase *c = rotation_case(name);
    if (c) send_rotation_chunks(c->record, ROT_FRAMES);
}

MAYBE_UNUSED static void ask_status(void)
{
    uint8_t op = URTC_KR_STATUS_OP;
    fake_hal_reset_recording();
    BlSelect_OnFrame(&op, 1);
}

MAYBE_UNUSED static const FakeTxFrame *status_frame(void) { return tx_find(URTC_BL_ID_RESP, 0); }

MAYBE_UNUSED static uint32_t status_left(const FakeTxFrame *f)
{
    return ((uint32_t)f->data[3] << 24) | ((uint32_t)f->data[4] << 16) | ((uint32_t)f->data[5] << 8) | f->data[6];
}

// Board with a table that holds the development key as the current one and the other throwaway key as the next one; the uptime starts at `seconds`.
MAYBE_UNUSED static void begin(uint32_t seconds)
{
    uint8_t t[100];
    fresh();
    uint32_t size = make_table(t, 2, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, BOOT_SIG_PUBLIC_OTHER, TRUST_FLAG_NEXT);
    Trust_UseTableForTest(t, size);
    Rotation_ResetForTest();
    fake_tick = seconds * 1000u;
    fake_tick_step = 0;
}

MAYBE_UNUSED static int installs(uint8_t other_key)
{
    Upd u = good(1500, 0, urtc_minor_field_pack(2, 0));
    u.other_key = other_key;
    fake_hal_reset_recording();
    fake_flash_erase_all();
    run_update(u);
    return status_seen(STATUS_VERIFY_OK) && main_slot_equals_image(1500);
}

MAYBE_UNUSED static int refused_as_unsigned(uint8_t other_key)
{
    Upd u = good(1500, 0, urtc_minor_field_pack(2, 0));
    u.other_key = other_key;
    fake_hal_reset_recording();
    fake_flash_erase_all();
    run_update(u);
    return fail_reason() == VERIFY_FAIL_REASON_SIGNATURE && main_slot_blank();
}

#if URTC_BL_ROTATION_COMMAND
static void test_rotation_on(int *failures)
{
    const FakeTxFrame *st;

    // before any command: the current key of the table alone
    begin(5);
    ask_status();
    st = status_frame();
    TEST_ASSERT(st && st->dlc == 7 && st->data[0] == 5 && st->data[1] == 1 && st->data[2] == 0 && status_left(st) == URTC_KR_NO_WINDOW,
                "operation 05 answers one key, no flags and no window before any command");
    TEST_ASSERT(tx_find(CAN_ID_KEY_ID, 0) && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_DEV, 8) == 0 && !tx_find(CAN_ID_KEY_ID, 1),
                "the status is followed by the id of the current key on 0x7ED");
    TEST_ASSERT(installs(0), "an image signed by the current key installs");
    begin(5);
    TEST_ASSERT(refused_as_unsigned(1), "the next key of the table does not verify until a command makes it current");

    // a valid command: the next key becomes current, the old one stays valid for the window
    begin(5);
    send_rotation("dev_to_other_600");
    ask_status();
    st = status_frame();
    TEST_ASSERT(st && st->data[1] == 2 && st->data[2] == URTC_KR_FLAG_ROTATING && status_left(st) == 600u, "after the command: two keys, rotating, 600 s left");
    TEST_ASSERT(tx_find(CAN_ID_KEY_ID, 1) && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_OTHER, 8) == 0 &&
                memcmp(tx_find(CAN_ID_KEY_ID, 1)->data, BOOT_SIG_ID_DEV, 8) == 0, "the new current key is announced first, the previous one second");
    TEST_ASSERT(installs(1), "inside the window an image signed by the new key installs");
    begin(5);
    send_rotation("dev_to_other_600");
    fake_tick += 300000u;
    ask_status();
    TEST_ASSERT(status_frame() && status_left(status_frame()) == 300u, "the seconds left follow the uptime");
    TEST_ASSERT(installs(0), "inside the window an image signed by the previous key still installs");
    HandleVersionQuery();
    TEST_ASSERT(tx_find(CAN_ID_KEY_ID, 1) != 0, "a version query announces both keys while the window runs");

    // the window ends: only the new key
    begin(5);
    send_rotation("dev_to_other_600");
    fake_tick += 601000u;
    ask_status();
    st = status_frame();
    TEST_ASSERT(st && st->data[1] == 1 && st->data[2] == 0 && status_left(st) == URTC_KR_NO_WINDOW && !tx_find(CAN_ID_KEY_ID, 1) &&
                memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_OTHER, 8) == 0, "after the window the new key alone is announced");
    TEST_ASSERT(refused_as_unsigned(0), "after the window an image signed by the previous key is refused");
    begin(5);
    send_rotation("dev_to_other_600");
    fake_tick += 601000u;
    TEST_ASSERT(installs(1), "after the window an image signed by the new key installs");

    // refusals leave the state alone
    begin(5);
    send_rotation("dev_to_other_600");
    fake_tick += 400000u;
    send_rotation("dev_to_other_600");
    ask_status();
    TEST_ASSERT(status_frame() && status_left(status_frame()) == 200u, "the same command again (counter repeated) does not restart the window");
    begin(5);
    {
        const BootRotationCase *c = rotation_case("dev_to_other_600");
        uint8_t bad[100];
        memcpy(bad, c->record, 100);
        bad[40] ^= 0x01;
        send_rotation_chunks(bad, ROT_FRAMES);
    }
    ask_status();
    TEST_ASSERT(status_frame() && status_frame()->data[1] == 1 && tx_find(CAN_ID_KEY_ID, 0) && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_DEV, 8) == 0,
                "a command with a damaged signature changes nothing");
    begin(5);
    send_rotation("other_signs_first");
    ask_status();
    TEST_ASSERT(tx_find(CAN_ID_KEY_ID, 0) && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_DEV, 8) == 0, "a command signed by a key that is not the current one changes nothing");
    begin(5);
    send_rotation("dev_to_other_long");
    ask_status();
    TEST_ASSERT(status_frame()->data[1] == 1 && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_DEV, 8) == 0, "a window longer than the limit is refused");
    begin(5);
    send_rotation("dev_to_third");
    ask_status();
    TEST_ASSERT(status_frame()->data[1] == 1 && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_DEV, 8) == 0, "a new key that the table does not hold is refused");
    begin(5);
    {
        const BootRotationCase *c = rotation_case("dev_to_other_600");
        send_rotation_chunks(c->record, ROT_FRAMES - 1);
    }
    ask_status();
    TEST_ASSERT(memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_DEV, 8) == 0, "fourteen chunks of fifteen are not a command");
    begin(5);
    update_in_progress = 1;
    send_rotation("dev_to_other_600");
    update_in_progress = 0;
    ask_status();
    TEST_ASSERT(status_frame()->data[1] == 1 && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_DEV, 8) == 0, "a command is ignored while an update is in progress");
    begin(5);
    {
        uint8_t f[8] = { 0x8F, 1, 2, 3, 4, 5, 6, 7 };
        HandleSigChunk(f);
        f[0] = 0xFF;
        HandleSigChunk(f);
    }
    ask_status();
    TEST_ASSERT(status_frame() && status_frame()->data[1] == 1, "a chunk index beyond the fifteenth is ignored");

    // no window: the new key replaces the old one at once
    begin(5);
    send_rotation("dev_to_other_none");
    ask_status();
    st = status_frame();
    TEST_ASSERT(st && st->data[1] == 1 && st->data[2] == 0 && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_OTHER, 8) == 0, "a command with no window makes the new key the only one");
    TEST_ASSERT(refused_as_unsigned(0) && installs(1), "with no window the old key is refused at once and the new one installs");

    // the new current key can end the overlap early
    begin(5);
    send_rotation("dev_to_other_600");
    send_rotation("other_ends_overlap");
    ask_status();
    TEST_ASSERT(status_frame()->data[1] == 1 && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_OTHER, 8) == 0, "the new current key can close the window early");

    // a reset forgets the command and goes back to the current key of the table
    begin(5);
    send_rotation("dev_to_other_none");
    Rotation_ResetForTest();
    ask_status();
    TEST_ASSERT(status_frame()->data[1] == 1 && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_DEV, 8) == 0, "after a reset the bootloader trusts the current key of the table alone");

    // a bootloader whose table is closed answers nothing
    {
        uint8_t t[100];
        fresh();
        uint32_t size = make_table(t, 0, 0, 0, 0, 0);
        Trust_UseTableForTest(t, size);
        Rotation_ResetForTest();
        ask_status();
        TEST_ASSERT(fake_tx_count == 0, "a bootloader that trusts nobody does not answer operation 05");
        send_rotation("dev_to_other_600");
        ask_status();
        TEST_ASSERT(fake_tx_count == 0, "and a command cannot give it a key");
    }

    // an addressed bootloader that is not selected stays quiet
    begin(5);
    {
        uint8_t select_other[6] = { URTC_BL_OP_SELECT, 0x12, 0x34, 0x56, 0x78, 1 };
        fake_uid[0] = 0x11223344u; fake_uid[1] = 0x55667788u; fake_uid[2] = 0x99AABBCCu;
        BlSelect_Init();
        BlSelect_OnFrame(select_other, 6);   // addresses the heads: none has that serial
        ask_status();
        TEST_ASSERT(status_frame() == 0, "an addressed bootloader that is not the selected one does not answer operation 05");
    }
    fake_tick_step = 0;
}
#else
static void test_rotation_off(int *failures)
{
    begin(5);
    ask_status();
    TEST_ASSERT(fake_tx_count == 0, "as it ships, operation 05 is not answered");
    TEST_ASSERT(installs(1) && (begin(5), installs(0)), "as it ships, every key of the table verifies");
    begin(5);
    send_rotation("dev_to_other_none");
    ask_status();
    TEST_ASSERT(fake_tx_count == 0, "as it ships, a rotation command is not read");
    TEST_ASSERT(installs(0) && (begin(5), installs(1)), "as it ships, a rotation command changes no key");
    begin(5);
    HandleVersionQuery();
    TEST_ASSERT(tx_find(CAN_ID_KEY_ID, 0) && memcmp(tx_find(CAN_ID_KEY_ID, 0)->data, BOOT_SIG_ID_DEV, 8) == 0 && !tx_find(CAN_ID_KEY_ID, 1), "as it ships, one key id is announced");

    // rotation chunks in the middle of an update do not touch the record of the image
    begin(5);
    make_image(1500);
    {
        const BootSigCase *c = find_case(1500, 0, urtc_minor_field_pack(2, 0), 1);
        start_update(1500, THIS_HARDWARE_ID);
        send_record(c->record, 6);
        send_rotation("dev_to_other_600");
        send_record(c->record, 11);
        send_data(1500);
        end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(2, 0));
        TEST_ASSERT(status_seen(STATUS_VERIFY_OK), "chunks of a rotation command do not disturb the record of an image");
    }
    fake_tick_step = 0;
}
#endif

int main(void)
{
    int failures = 0;
    void *m = mmap(0, IMG_MAX, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    printf("URTC host logic tests - key rotation in the bootloader (command %s)\n", URTC_BL_ROTATION_COMMAND ? "built in" : "off");
    if (m == MAP_FAILED || !fake_flash_init()) { printf("  SKIP: cannot map the fake flash on this host\n"); return 0; }
    image = m;
    image_addr = (uint32_t)(uintptr_t)m;
#if URTC_BL_ROTATION_COMMAND
    test_rotation_on(&failures);
#else
    test_rotation_off(&failures);
#endif
    if (failures == 0) {
        printf("  OK   all bootloader rotation host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
