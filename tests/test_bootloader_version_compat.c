// =============================================================================
// URTC Firmware - Host tests of the version rule kept in the metadata of the master bootloader and changed by a signed command
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] boot/bootloader_protocol.c and boot/bootloader_flash.c are compiled unchanged against the fake HAL, twice, both with the two metadata pages
// (-DURTC_BL_META_AB=1): with the command built in (-DURTC_BL_VERSION_COMPAT_COMMAND=1) and as it ships (off). The signed commands come from the shared
// vectors of URTC-COMMON (tests/vectors/version_compat_vectors.h, throwaway test keys). Off, nothing of it exists: the frames are not read, nothing is
// written or answered and the strict rule of the build refuses an image without a patch, as before. On, only a signed command with a higher counter changes the
// setting, and a power cut at any flash step leaves the old setting or the new one, never a lost rollback counter or serial.
// The helpers of the update suite are reused by including it (its own main is renamed).
#define main update_suite_main
#include "test_bootloader_update.c"
#undef main

#include "urtc_bl_select.h"
#include "urtc_bl_serial.h"
#include "urtc_meta_ab.h"
#include "urtc_vc_annex.h"
#include "version_compat_vectors.h"

#ifndef URTC_BL_VERSION_COMPAT_COMMAND
#define URTC_BL_VERSION_COMPAT_COMMAND 0   // the bootloader as it ships
#endif
#define MAYBE_UNUSED __attribute__((unused))

#define SAMPLE_SERIAL 0x11223344u

static const version_compat_command_vec_t *vector(const char *name)
{
    for (unsigned i = 0; i < VERSION_COMPAT_COMMAND_VECTOR_COUNT; i++)
        if (strcmp(VERSION_COMPAT_COMMAND_VECTORS[i].name, name) == 0) return &VERSION_COMPAT_COMMAND_VECTORS[i];
    printf("  MISSING version compat case %s\n", name);
    return &VERSION_COMPAT_COMMAND_VECTORS[0];
}

// The head of the vectors: its unique ID gives the serial the commands are addressed to.
static void head(void)
{
    const version_compat_command_vec_t *v = &VERSION_COMPAT_COMMAND_VECTORS[0];
    fake_uid[0] = v->uid[0]; fake_uid[1] = v->uid[1]; fake_uid[2] = v->uid[2];
    BlSelect_Init();
}

static void send_command_chunks(const uint8_t *command, int chunks)
{
    for (int i = 0; i < chunks; i++) {
        uint8_t f[8];
        f[0] = (uint8_t)(URTC_VC_CHUNK_BASE + (unsigned)i);
        memcpy(f + 1, command + 7 * i, 7);
        HandleSerialChunk(f);
    }
}

static void send_command(const char *name)
{
    fake_hal_reset_recording();
    send_command_chunks(vector(name)->command, (int)URTC_VC_CHUNKS);
}

static void ask(void)
{
    uint8_t f[8] = { URTC_VC_QUERY_INDEX, 0, 0, 0, 0, 0, 0, 0 };
    fake_hal_reset_recording();
    HandleSerialChunk(f);
}

MAYBE_UNUSED static int answered(uint8_t status, uint8_t mode, uint8_t flags, uint32_t counter)
{
    const FakeTxFrame *a = tx_find(URTC_CAN_ID_BOOTLOADER_SERIAL_RESULT, 0);
    uint8_t expected[URTC_VC_ANSWER_SIZE];
    urtc_vc_answer_build(expected, (urtc_vc_status_t)status, mode, flags, counter);
    return a && a->dlc == URTC_VC_ANSWER_SIZE && memcmp(a->data, expected, sizeof expected) == 0 && !tx_find(URTC_CAN_ID_BOOTLOADER_SERIAL_RESULT, 1);
}

// An installed 0.4.2 with the rollback counter `counter` and a programmed serial.
static void installed(uint32_t counter)
{
    fresh();
    head();
    seed_metadata_counter(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000, counter);
    Serial_Store(SAMPLE_SERIAL, 5u);
    BlSelect_Init();   // the serial in force stays the fold of the unique ID: the programmable serial is off in this build
}

// Sends the signed 0.4 image without a patch (the minor field of an old flasher) on top of what is installed. 1: installed; 0: refused as a rollback.
static int patchless_installs(void)
{
    update_in_progress = 0;
    update_failed = 0;
    fake_hal_reset_recording();
    run_update(good(1500, 0, 4));
    if (status_seen(STATUS_VERIFY_OK) && main_slot_equals_image(1500)) return 1;
    return fail_reason() == VERIFY_FAIL_REASON_ROLLBACK ? 0 : -1;
}

static void pages(uint8_t out[4096])
{
    memcpy(out, (const void *)(uintptr_t)URTC_MASTER_METADATA_B_ADDR, 2048);
    memcpy(out + 2048, (const void *)(uintptr_t)METADATA_ADDR, 2048);
}

static int pages_are(const uint8_t before[4096])
{
    uint8_t now[4096];
    pages(now);
    return memcmp(now, before, sizeof now) == 0;
}

MAYBE_UNUSED static int state_kept(uint32_t rollback_counter)
{
    FirmwareMetadata_t meta;
    uint32_t serial = 0, counter = 0;
    return Metadata_Read(&meta) && meta.counter == rollback_counter && Serial_ReadNewest(&serial, &counter) && serial == SAMPLE_SERIAL && counter == 5u;
}

#if URTC_BL_VERSION_COMPAT_COMMAND

static void stored(uint32_t *mode, uint32_t *counter) { VersionCompat_Read(mode, counter); }

static void test_default_and_command(int *failures)
{
    uint32_t mode = 9, counter = 9;
    installed(0);
    stored(&mode, &counter);
    TEST_ASSERT(mode == URTC_VC_MODE_NONE && counter == 0u, "a board nobody configured stores nothing");
    ask();
    TEST_ASSERT(answered(URTC_VC_OK, URTC_VC_MODE_STRICT, 0, 0), "and answers the strict rule in force");
    TEST_ASSERT(patchless_installs() == 0 && main_slot_blank(), "the default refuses an image without a patch over 0.4.2, as before");

    installed(0);
    send_command("a valid command that accepts an image without a patch");
    TEST_ASSERT(answered(URTC_VC_OK, URTC_VC_MODE_ACCEPT_PATCHLESS, URTC_VC_FLAG_STORED, 5), "a signed command is accepted and answered");
    TEST_ASSERT(state_kept(0), "the metadata and the serial are the same after the command");
    TEST_ASSERT(patchless_installs() == 1, "the same image installs once the mode says so");
    stored(&mode, &counter);
    TEST_ASSERT(mode == URTC_VC_MODE_ACCEPT_PATCHLESS && counter == 5u && state_kept(1), "the setting is carried through the metadata writes of the update");
    TEST_ASSERT(Serial_Store(SAMPLE_SERIAL, 6u), "a serial write after it");
    for (uint32_t i = 0; i < 6u; i++) seed_metadata_counter(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000, 1u + i);
    stored(&mode, &counter);
    TEST_ASSERT(mode == URTC_VC_MODE_ACCEPT_PATCHLESS && counter == 5u, "and through any number of later writes of the state and the serial, on both pages");
}

static void test_repeated_and_older_commands(int *failures)
{
    uint32_t mode, counter;
    uint8_t before[4096];
    installed(0);
    send_command("a valid command that accepts an image without a patch");      // counter 5
    pages(before);
    send_command("a valid command that accepts an image without a patch");
    TEST_ASSERT(answered(URTC_VC_REPLAY, URTC_VC_MODE_ACCEPT_PATCHLESS, URTC_VC_FLAG_STORED, 5) && pages_are(before), "the same command again is refused and writes nothing");
    send_command("a valid command back to the strict rule");                    // counter 6
    TEST_ASSERT(answered(URTC_VC_OK, URTC_VC_MODE_STRICT, URTC_VC_FLAG_STORED, 6), "a newer command goes back to strict");
    pages(before);
    send_command("a valid command that accepts an image without a patch");
    TEST_ASSERT(answered(URTC_VC_REPLAY, URTC_VC_MODE_STRICT, URTC_VC_FLAG_STORED, 6) && pages_are(before), "an older command cannot undo it");
    stored(&mode, &counter);
    TEST_ASSERT(mode == URTC_VC_MODE_STRICT && counter == 6u && patchless_installs() == 0, "and the strict rule is in force again");
}

static void test_refused_commands(int *failures)
{
    static const struct { const char *name; uint8_t status; } cases[] = {
        { "a command made for another head fails", URTC_VC_BAD_SIGNATURE },
        { "signed by a key the bootloader does not trust", URTC_VC_UNKNOWN_KEY },
        { "a mode changed after signing", URTC_VC_BAD_SIGNATURE },
        { "a counter changed after signing", URTC_VC_BAD_SIGNATURE },
        { "a signature with a bit changed", URTC_VC_BAD_SIGNATURE },
        { "a key id with a bit changed", URTC_VC_UNKNOWN_KEY },
        { "the mode 0 is not a mode", URTC_VC_BAD_MODE },
        { "the mode 3 is not a mode", URTC_VC_BAD_MODE },
        { "the counter 0 is never accepted", URTC_VC_REPLAY },
        { "a signed serial command is not a command of this kind", URTC_VC_BAD_SIGNATURE },
    };
    uint8_t before[4096];
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        installed(0);
        pages(before);
        send_command(cases[i].name);
        TEST_ASSERT(answered(cases[i].status, URTC_VC_MODE_STRICT, 0, 0) && pages_are(before), cases[i].name);
    }
    TEST_ASSERT(patchless_installs() == 0, "after every refusal the rule is still strict");

    // a command addressed to another head: silence
    installed(0);
    pages(before);
    {
        uint8_t other[URTC_VC_COMMAND_SIZE];
        memcpy(other, vector("a valid command that accepts an image without a patch")->command, sizeof other);
        other[3] ^= 0x01;
        fake_hal_reset_recording();
        send_command_chunks(other, (int)URTC_VC_CHUNKS);
    }
    TEST_ASSERT(fake_tx_count == 0 && pages_are(before), "a command for another target is not answered and writes nothing");

    // eleven chunks of twelve are not a command; nothing is a command while an update runs
    installed(0);
    pages(before);
    fake_hal_reset_recording();
    send_command_chunks(vector("a valid command that accepts an image without a patch")->command, (int)URTC_VC_CHUNKS - 1);
    TEST_ASSERT(fake_tx_count == 0 && pages_are(before), "eleven chunks of twelve do nothing");
    update_in_progress = 1;
    send_command("a valid command that accepts an image without a patch");
    TEST_ASSERT(answered(URTC_VC_BUSY, URTC_VC_MODE_STRICT, 0, 0) && pages_are(before), "a command during an update is refused as busy");
    update_in_progress = 0;
}

static void test_what_stays_refused(int *failures)
{
    installed(0);
    send_command("a valid command that accepts an image without a patch");
    fake_hal_reset_recording();
    run_update(good(1500, 0, urtc_minor_field_pack(4, 1)));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK, "a lower patch that is declared is still a rollback");
    installed(0);
    send_command("a valid command that accepts an image without a patch");
    fake_hal_reset_recording();
    run_update(good(1500, 0, urtc_minor_field_pack(3, 0)));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK, "a lower minor is still a rollback");
    installed(7);
    send_command("a valid command that accepts an image without a patch");
    TEST_ASSERT(patchless_installs() == 0 && state_kept(7), "an image below the floor of the signed counter is still refused");
}

// A bootloader without the feature writes the pages with the same record and no annex.
static void test_other_bootloader_versions(int *failures)
{
    uint32_t mode, counter;
    installed(0);
    send_command("a valid command that accepts an image without a patch");
    {   // damaged annex: any doubt reads as nothing stored
        const uint8_t *a = (const uint8_t *)(uintptr_t)URTC_MASTER_METADATA_B_ADDR;
        const uint8_t *b = (const uint8_t *)(uintptr_t)METADATA_ADDR;
        uint32_t ga = 0, gb = 0;
        const int va = urtc_meta_ab_page_valid(b, URTC_VC_ANNEX_PAYLOAD, &ga);
        const int vb = urtc_meta_ab_page_valid(a, URTC_VC_ANNEX_PAYLOAD, &gb);
        const uint32_t newest = (vb && (!va || gb > ga)) ? URTC_MASTER_METADATA_B_ADDR : METADATA_ADDR;
        uint8_t *annex = (uint8_t *)(uintptr_t)(newest + URTC_VC_ANNEX_OFFSET);
        uint32_t m = 0, c = 0;
        TEST_ASSERT(urtc_vc_record_read(annex, &m, &c) && m == URTC_VC_MODE_ACCEPT_PATCHLESS, "the annex sits in the page of the newest record");
        annex[4] &= (uint8_t)~0x02u;
        stored(&mode, &counter);
        TEST_ASSERT(mode == URTC_VC_MODE_NONE && counter == 0u && state_kept(0), "a damaged annex reads as nothing stored and the state stays");
        TEST_ASSERT(patchless_installs() == 0, "which is the strict rule");
    }
    installed(0);
    send_command("a valid command that accepts an image without a patch");
    {   // an older bootloader rewrites the metadata: the record layout is the same, the annex is not written
        FirmwareMetadata_t meta;
        TEST_ASSERT(Metadata_Read(&meta), "the state reads");
        uint32_t ga = 0, gb = 0;
        const int va = urtc_meta_ab_page_valid((const uint8_t *)(uintptr_t)METADATA_ADDR, URTC_VC_ANNEX_PAYLOAD, &ga);
        const int vb = urtc_meta_ab_page_valid((const uint8_t *)(uintptr_t)URTC_MASTER_METADATA_B_ADDR, URTC_VC_ANNEX_PAYLOAD, &gb);
        const uint32_t newest = (vb && (!va || gb > ga)) ? URTC_MASTER_METADATA_B_ADDR : METADATA_ADDR;
        memset((void *)(uintptr_t)(newest + URTC_VC_ANNEX_OFFSET), 0xFF, URTC_VC_RECORD_SIZE);
        stored(&mode, &counter);
        TEST_ASSERT(mode == URTC_VC_MODE_NONE && Metadata_Read(&meta) && state_kept(0), "a record without an annex is read as before and holds no setting");
        TEST_ASSERT(patchless_installs() == 0, "so the rule is strict");
    }
}

static void legacy_record(uint32_t slot, uint32_t generation, const FirmwareMetadata_t *meta)
{
    MetaRecord_t r;
    memset(&r, 0, sizeof r);
    r.generation = generation;
    r.meta = *meta;
    r.crc = CRC32_Finalize(CRC32_Update(0xFFFFFFFFUL, (const uint8_t *)&r, META_RECORD_SIZE - 4u));
    memcpy((void *)(uintptr_t)(METADATA_ADDR + slot * META_RECORD_SIZE), &r, sizeof r);
}

static void test_board_with_the_log_of_one_page(int *failures)
{
    uint32_t mode, counter;
    FirmwareMetadata_t meta, read;
    fresh();
    head();
    memset(&meta, 0, sizeof meta);
    meta.magic = METADATA_MAGIC_VALID; meta.state = META_STATE_APP_VALID; meta.hardware_id = THIS_HARDWARE_ID;
    meta.version_minor = urtc_minor_field_pack(4, 2); meta.size = 3000; meta.counter = 12;
    legacy_record(0, 1u, &meta);
    uint8_t old_page[2048];
    memcpy(old_page, (const void *)(uintptr_t)METADATA_ADDR, sizeof old_page);
    send_command("a valid command that accepts an image without a patch");
    stored(&mode, &counter);
    TEST_ASSERT(mode == URTC_VC_MODE_ACCEPT_PATCHLESS && Metadata_Read(&read) && read.counter == 12u, "a board with the log of one page keeps its rollback counter");
    TEST_ASSERT(memcmp(old_page, (const void *)(uintptr_t)METADATA_ADDR, sizeof old_page) == 0, "and the old log is untouched by the first record");
}

// The power goes at every flash step: of a command that stores a new setting, and of a metadata write that has to carry the setting.
static void test_power_cuts(int *failures)
{
    int lost = 0, wrong = 0, retry_failed = 0, cut_command = 0, cut_write = 0;
    for (int kind = 0; kind < 2; kind++) {                       // 0: the cut is in a program, 1: in an erase
        for (int nth = 1; nth <= 70; nth++) {
            uint32_t mode = 9, counter = 9;
            installed(3);
            send_command("a valid command that accepts an image without a patch");        // the old setting: accept, 5
            if (kind == 0) fake_flash_fail_program_nth = fake_flash_program_calls() + nth; else fake_flash_fail_erase_nth = fake_flash_erase_calls() + nth;
            send_command("a valid command back to the strict rule");                       // the new one: strict, 6
            const FakeTxFrame *a = tx_find(URTC_CAN_ID_BOOTLOADER_SERIAL_RESULT, 0);
            const int failed = a && a->data[1] == URTC_VC_FLASH_FAILED;
            fake_flash_fail_program_nth = 0; fake_flash_fail_erase_nth = 0;
            stored(&mode, &counter);
            if (failed) cut_command++;
            if (!state_kept(3)) lost++;
            if (failed ? !(mode == URTC_VC_MODE_ACCEPT_PATCHLESS && counter == 5u) : !(mode == URTC_VC_MODE_STRICT && counter == 6u)) wrong++;
            if (failed) {
                send_command("a valid command back to the strict rule");
                stored(&mode, &counter);
                if (!(mode == URTC_VC_MODE_STRICT && counter == 6u && state_kept(3))) retry_failed++;
            }

            installed(3);
            send_command("a valid command that accepts an image without a patch");
            if (kind == 0) fake_flash_fail_program_nth = fake_flash_program_calls() + nth; else fake_flash_fail_erase_nth = fake_flash_erase_calls() + nth;
            FirmwareMetadata_t next;
            Metadata_Read(&next);
            next.counter = 4u;
            const uint8_t done = Metadata_EraseAndWrite(&next);
            fake_flash_fail_program_nth = 0; fake_flash_fail_erase_nth = 0;
            if (!done) cut_write++;
            stored(&mode, &counter);
            if (!(mode == URTC_VC_MODE_ACCEPT_PATCHLESS && counter == 5u)) wrong++;
            if (!state_kept(done ? 4u : 3u)) lost++;
        }
    }
    TEST_ASSERT(cut_command > 20 && cut_write > 20, "the cuts really fell inside both kinds of write");
    TEST_ASSERT(lost == 0, "no cut loses the rollback counter or the serial");
    TEST_ASSERT(wrong == 0, "after a cut the setting is the old one, after a completed write the new one; a metadata write never changes it");
    TEST_ASSERT(retry_failed == 0, "a command that was cut can be sent again");
}

#else // as the bootloader ships

static void test_off(int *failures)
{
    uint8_t before[4096];
    installed(0);
    pages(before);
    send_command("a valid command that accepts an image without a patch");
    TEST_ASSERT(fake_tx_count == 0 && pages_are(before), "off: the signed command is not read, answered or stored");
    ask();
    TEST_ASSERT(fake_tx_count == 0 && pages_are(before), "off: the query is not answered");
    TEST_ASSERT(patchless_installs() == 0 && main_slot_blank(), "off: an image without a patch is refused over 0.4.2, as before");
    installed(0);
    run_update(good(1500, 0, urtc_minor_field_pack(4, 3)));
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK) && main_slot_equals_image(1500) && state_kept(1), "off: an ordinary update installs as before");
    {
        const uint8_t *annex_a = (const uint8_t *)(uintptr_t)(METADATA_ADDR + URTC_VC_ANNEX_OFFSET);
        const uint8_t *annex_b = (const uint8_t *)(uintptr_t)(URTC_MASTER_METADATA_B_ADDR + URTC_VC_ANNEX_OFFSET);
        int blank = 1;
        for (unsigned i = 0; i < URTC_VC_RECORD_SIZE; i++) blank &= (annex_a[i] == 0xFF && annex_b[i] == 0xFF);
        TEST_ASSERT(blank, "off: nothing is written where the annex would be");
    }
    // the serial command on the same identifier is read as before (this build answers that it has no programmable serial)
    installed(0);
    fake_hal_reset_recording();
    {
        uint8_t command[URTC_SERIAL_COMMAND_SIZE] = {0};
        const uint32_t target = urtc_bl_serial_from_uid(fake_uid[0], fake_uid[1], fake_uid[2]);
        urtc_serial_put32(command, target);
        for (unsigned i = 0; i < URTC_SERIAL_CHUNKS; i++) {
            uint8_t f[8] = { (uint8_t)i, 0, 0, 0, 0, 0, 0, 0 };
            memcpy(f + 1, command + 7u * i, 7);
            HandleSerialChunk(f);
        }
    }
    {
        const FakeTxFrame *a = tx_find(URTC_CAN_ID_BOOTLOADER_SERIAL_RESULT, 0);
        TEST_ASSERT(a && a->dlc == 5 && a->data[0] == URTC_SERIAL_DISABLED, "off: the serial command answers as before");
    }
}
#endif

int main(void)
{
    int failures = 0;
    void *m = mmap(0, IMG_MAX, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    printf("URTC host logic tests - the stored version rule of the master bootloader (command %s)\n", URTC_BL_VERSION_COMPAT_COMMAND ? "built in" : "off");
    if (m == MAP_FAILED || !fake_flash_init()) { printf("  SKIP: cannot map the fake flash on this host\n"); return 0; }
    image = m;
    image_addr = (uint32_t)(uintptr_t)m;
#if URTC_BL_VERSION_COMPAT_COMMAND
    test_default_and_command(&failures);
    test_repeated_and_older_commands(&failures);
    test_refused_commands(&failures);
    test_what_stays_refused(&failures);
    test_other_bootloader_versions(&failures);
    test_board_with_the_log_of_one_page(&failures);
    test_power_cuts(&failures);
#else
    test_off(&failures);
#endif
    if (failures == 0) {
        printf("  OK   all stored version rule bootloader host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
