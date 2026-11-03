// =============================================================================
// URTC Firmware - Host logic tests for the bootloader update flow
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// boot/bootloader_protocol.c, boot/bootloader_flash.c, boot/bootloader_crypto.c,
// boot/bootloader_trust.c and the Ed25519 verifier are compiled unchanged against the fake HAL in tests/host_hal/. The fake
// flash is a 256 KiB mapping at 0x08000000 that only lets bits go from 1 to 0
// until a page is erased, so a write to a non-erased page fails like the real
// part. The tests drive the CAN update sequence frame by frame (0x7F1 start,
// 0x7EC chunks of the Ed25519 record, 0x7F2 data, 0x7F4 end) and assert on the
// status frames, the flash content and the metadata: size / hardware-id
// validation, CRC32, the Ed25519 record against the trust table (unknown key,
// revoked key, closed table, wrong hardware), the signed rollback counter, the
// version anti-rollback with the one-shot downgrade authorization, the
// append-only metadata log (every power-cut point), flash error paths,
// resuming an interrupted copy after power loss, and the 0x7F8 / 0x7FB / 0x7FE
// queries. The signed records come from tests/boot_sig_cases.h (throwaway test
// keys, written by tools/make_boot_sig_cases.py).
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "test_runner.h"
#include "stm32f3xx_hal.h"
#include "bootloader_common.h"
#include "bootloader_protocol.h"
#include "bootloader_crypto.h"
#include "bootloader_flash.h"
#include "bootloader_oled.h"
#include "bootloader_trust.h"
#include "urtc_ed25519.h"
#include "urtc_trust_dev.h"          // the table this suite is built with (-DURTC_TRUST_DEV), as the generator wrote it
#include "urtc_version_wire.h"
#include "boot_sig_cases.h"
#include "vectors/urtc_fw_vectors.h" // written by URTC-COMMON from its conformance vectors

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
IWDG_HandleTypeDef hiwdg;
I2C_HandleTypeDef hi2c2;

void OLED_Init_Boot(void) {}
void OLED_ClearAll_Boot(void) {}
void OLED_PrintStr2x_Boot(uint8_t page, uint8_t col, const char *str) { (void)page; (void)col; (void)str; }
void OLED_DrawProgressBar_Boot(uint8_t percent) { (void)percent; }

// ---- helpers ----------------------------------------------------------------
// Independent bit-by-bit CRC32 (zlib variant), not the table version under test.
static uint32_t ref_crc32(const uint8_t *d, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
    }
    return ~c;
}

static uint8_t *image;          // low-memory copy of the test image (the SHA-256 takes a 32-bit address)
static uint32_t image_addr;
#define IMG_MAX 4096

static void make_image(uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) image[i] = (uint8_t)((i * 13 + 5) & 0xFF);
}

// The signed case for an image of `size` bytes with this version, counter, signing key and hardware id; NULL when the tables of tests/boot_sig_cases.h have none.
static const BootSigCase *find_case_ex(uint32_t size, uint32_t vmaj, uint32_t vmin, uint32_t counter, uint8_t other_key, uint32_t hardware_id)
{
    for (unsigned i = 0; i < BOOT_SIG_CASE_COUNT; i++) {
        const BootSigCase *c = &BOOT_SIG_CASES[i];
        if (c->size == size && c->major == vmaj && c->minor_field == vmin && c->counter == counter && c->other_key == other_key && c->hardware_id == hardware_id) return c;
    }
    return 0;
}

static const BootSigCase *find_case(uint32_t size, uint32_t vmaj, uint32_t vmin, uint32_t counter)
{
    const BootSigCase *c = find_case_ex(size, vmaj, vmin, counter, 0, THIS_HARDWARE_ID);
    if (!c) printf("  MISSING signed case %u/%u/0x%04X/%u\n", (unsigned)size, (unsigned)vmaj, (unsigned)vmin, (unsigned)counter);
    return c;
}

static const FakeTxFrame *tx_find(uint32_t id, int nth)
{
    for (int i = 0; i < fake_tx_count; i++)
        if (fake_tx[i].id == id && nth-- == 0) return &fake_tx[i];
    return 0;
}

static int status_seen(uint8_t code)
{
    for (int i = 0; i < fake_tx_count; i++)
        if (fake_tx[i].id == CAN_ID_STATUS && fake_tx[i].data[0] == code) return 1;
    return 0;
}

static int fail_reason(void)
{
    for (int i = 0; i < fake_tx_count; i++)
        if (fake_tx[i].id == CAN_ID_STATUS && fake_tx[i].dlc == 2 && fake_tx[i].data[0] == STATUS_VERIFY_FAIL) return fake_tx[i].data[1];
    return -1;
}

static void put_be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void start_update(uint32_t size, uint32_t hw)
{
    uint8_t f[8];
    put_be32(f, size); put_be32(f + 4, hw);
    HandleStartUpdate(f);
}

// The first `chunks` of the 11 frames of the record: index, then 7 bytes (the last frame holds 6 and a pad byte).
static void send_record(const uint8_t record[76], int chunks)
{
    for (int i = 0; i < chunks; i++) {
        uint8_t f[8] = { (uint8_t)i, 0, 0, 0, 0, 0, 0, 0 };
        for (int k = 0; k < 7; k++) {
            int at = i * 7 + k;
            f[1 + k] = at < 76 ? record[at] : 0;
        }
        HandleSigChunk(f);
    }
}

static void send_data(uint32_t size)
{
    for (uint32_t off = 0; off < size; off += 8) {
        uint8_t f[8];
        uint32_t n = size - off < 8 ? size - off : 8;
        memset(f, 0, 8);
        memcpy(f, image + off, n);
        HandleData(f, n);
    }
}

static void end_update(uint32_t crc, uint16_t vmaj, uint16_t vminfield)
{
    uint8_t f[8];
    put_be32(f, crc);
    f[4] = vmaj >> 8; f[5] = vmaj; f[6] = vminfield >> 8; f[7] = vminfield;
    HandleEndUpdate(f);
}

static void seed_metadata_counter(uint32_t state, uint32_t vmaj, uint32_t vminfield, uint32_t size, uint32_t counter)
{
    FirmwareMetadata_t m;
    memset(&m, 0, sizeof m);
    m.magic = METADATA_MAGIC_VALID; m.state = state; m.hardware_id = THIS_HARDWARE_ID;
    m.version_major = vmaj; m.version_minor = vminfield; m.size = size; m.counter = counter;
    Metadata_EraseAndWrite(&m);
}

static void seed_metadata(uint32_t state, uint32_t vmaj, uint32_t vminfield, uint32_t size)
{
    seed_metadata_counter(state, vmaj, vminfield, size, 0);
}

static int main_slot_equals_image(uint32_t size) { return memcmp((const void *)MAIN_APP_ADDR, image, size) == 0; }
static int main_slot_blank(void)
{
    const uint8_t *p = (const uint8_t *)MAIN_APP_ADDR;
    for (uint32_t i = 0; i < 4096; i++) if (p[i] != 0xFF) return 0;
    return 1;
}

static void fresh(void)
{
    fake_flash_erase_all();
    fake_hal_reset_recording();
    update_in_progress = 0;
    update_failed = 0;
    Trust_UseTableForTest(0, 0);   // the table compiled into this bootloader (the development one)
}

// Full signed update with optional tampering. Returns nothing; inspect the frames.
typedef struct {
    int bad_crc; int bad_sig; int chunks; uint32_t declared_hw; uint32_t size; uint16_t vmaj; uint16_t vmin;
    uint32_t counter; uint8_t other_key; uint32_t signed_hw;
} Upd;

static void run_update(Upd u)
{
    make_image(u.size);
    const BootSigCase *c = find_case_ex(u.size, u.vmaj, u.vmin, u.counter, u.other_key, u.signed_hw);
    if (!c) { printf("  MISSING signed case %u/%u/0x%04X/%u\n", (unsigned)u.size, (unsigned)u.vmaj, (unsigned)u.vmin, (unsigned)u.counter); return; }
    uint8_t record[76];
    memcpy(record, c->record, 76);
    if (u.bad_sig) record[40] ^= 0x01;
    start_update(u.size, u.declared_hw);
    send_record(record, u.chunks);
    send_data(u.size);
    uint32_t crc = ref_crc32(image, u.size);
    if (u.bad_crc) crc ^= 1;
    end_update(crc, u.vmaj, u.vmin);
}

static Upd good(uint32_t size, uint16_t vmaj, uint16_t vmin)
{
    Upd u = { 0, 0, 11, THIS_HARDWARE_ID, size, vmaj, vmin, 1, 0, THIS_HARDWARE_ID };
    return u;
}

// ---- the trust table, the signed counter and the metadata log ----------------------------------------------------------------------------------
static void send_chunk_at(const uint8_t record[76], int i)
{
    uint8_t f[8] = { (uint8_t)i, 0, 0, 0, 0, 0, 0, 0 };
    for (int k = 0; k < 7; k++) {
        int at = i * 7 + k;
        f[1 + k] = at < 76 ? record[at] : 0;
    }
    HandleSigChunk(f);
}

static void refresh_table_crc(uint8_t *table, uint32_t size)
{
    uint32_t crc = ref_crc32(table, size - 4u);
    table[size - 4u] = (uint8_t)crc;
    table[size - 3u] = (uint8_t)(crc >> 8);
    table[size - 2u] = (uint8_t)(crc >> 16);
    table[size - 1u] = (uint8_t)(crc >> 24);
}

// A trust table of `count` keys as tools/image_trust.py writes it: the key ids are the first 8 bytes of the SHA-512 of the keys.
static uint32_t make_table(uint8_t out[100], uint8_t count, const uint8_t *key0, uint8_t flags0, const uint8_t *key1, uint8_t flags1)
{
    memset(out, 0, 100);
    memcpy(out, "TKEY", 4);
    out[4] = 1;
    out[5] = count;
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t *key = (i == 0) ? key0 : key1;
        uint8_t *entry = out + 8 + i * 44;
        uint8_t digest[URTC_SHA512_DIGEST_SIZE];
        urtc_sha512(digest, key, 32);
        memcpy(entry, digest, 8);
        memcpy(entry + 8, key, 32);
        entry[40] = (i == 0) ? flags0 : flags1;
    }
    uint32_t size = 8u + count * 44u + 4u;
    refresh_table_crc(out, size);
    return size;
}

static void test_trust_table_parsing(int *failures)
{
    uint8_t t[100];
    TrustKeys_t k;
    uint32_t size = make_table(t, 2, BOOT_SIG_PUBLIC_OTHER, TRUST_FLAG_NEXT, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT);
    TEST_ASSERT(size == 100 && Trust_Parse(t, size, &k) == 2 && k.count == 2, "a table with a current and a next key trusts both");
    TEST_ASSERT(memcmp(k.keys[0], BOOT_SIG_PUBLIC_DEV, 32) == 0 && memcmp(k.keys[1], BOOT_SIG_PUBLIC_OTHER, 32) == 0 && k.flags[0] == TRUST_FLAG_CURRENT,
                "the current key comes first whatever the order in the table");
    TEST_ASSERT(Trust_Parse(t, size - 1u, &k) == 0 && k.count == 0, "a table cut short trusts nobody");
    TEST_ASSERT(Trust_Parse(0, size, &k) == 0, "no table at all trusts nobody");

    size = make_table(t, 2, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, BOOT_SIG_PUBLIC_OTHER, TRUST_FLAG_REVOKED);
    TEST_ASSERT(Trust_Parse(t, size, &k) == 1 && memcmp(k.keys[0], BOOT_SIG_PUBLIC_DEV, 32) == 0, "a revoked key stays in the table and is never trusted");
    size = make_table(t, 1, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_REVOKED, 0, 0);
    TEST_ASSERT(size == 56 && Trust_Parse(t, size, &k) == 0 && k.count == 0, "a table whose only key is revoked trusts nobody");
    size = make_table(t, 0, 0, 0, 0, 0);
    TEST_ASSERT(size == 12 && Trust_Parse(t, size, &k) == 0, "the closed table (no key) is valid and trusts nobody");

    static const uint8_t bad_flags[] = { 0, 3, 5, 6, 7, 8, 0x81 };
    int all_refused = 1;
    for (unsigned i = 0; i < sizeof bad_flags; i++) {
        size = make_table(t, 1, BOOT_SIG_PUBLIC_DEV, bad_flags[i], 0, 0);
        if (Trust_Parse(t, size, &k) != 0) all_refused = 0;
    }
    TEST_ASSERT(all_refused, "a key with no state, two states or an unknown state closes the table");

    size = make_table(t, 1, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, 0, 0);
    t[8] ^= 0x01; refresh_table_crc(t, size);
    TEST_ASSERT(Trust_Parse(t, size, &k) == 0, "a key id that is not the hash of its key closes the table (even with a good CRC)");
    size = make_table(t, 1, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, 0, 0);
    t[41] = 1; refresh_table_crc(t, size);
    TEST_ASSERT(Trust_Parse(t, size, &k) == 0, "padding that is not zero closes the table");
    size = make_table(t, 1, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, 0, 0);
    t[4] = 2; refresh_table_crc(t, size);
    TEST_ASSERT(Trust_Parse(t, size, &k) == 0, "an unknown schema closes the table");
    size = make_table(t, 1, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, 0, 0);
    t[5] = 3; refresh_table_crc(t, size);
    TEST_ASSERT(Trust_Parse(t, 100, &k) == 0, "more than two keys closes the table");
    size = make_table(t, 1, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, 0, 0);
    t[6] = 1; refresh_table_crc(t, size);
    TEST_ASSERT(Trust_Parse(t, size, &k) == 0, "table flags that are not zero close the table");

    // any single flipped bit of a two-key table, in any field, closes it: the CRC covers every byte
    size = make_table(t, 2, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, BOOT_SIG_PUBLIC_OTHER, TRUST_FLAG_NEXT);
    int none_survives = 1;
    for (uint32_t byte = 0; byte < size; byte++) {
        for (uint8_t bit = 0; bit < 8u; bit++) {
            uint8_t copy[100];
            memcpy(copy, t, size);
            copy[byte] ^= (uint8_t)(1u << bit);
            if (Trust_Parse(copy, size, &k) != 0) none_survives = 0;
        }
    }
    TEST_ASSERT(none_survives, "no single flipped bit of a table leaves a trusted key");

    // the table compiled into this build (the development one, -DURTC_TRUST_DEV) is the one of the public file
    extern const uint8_t URTC_TRUST_TABLE[];
    static const uint8_t compiled[] = { URTC_TRUST_TABLE_BYTES };
    TEST_ASSERT(sizeof compiled == URTC_TRUST_TABLE_SIZE && memcmp(URTC_TRUST_TABLE, compiled, sizeof compiled) == 0, "the compiled table is the one of the generated header");
    Trust_UseTableForTest(0, 0);
    TEST_ASSERT(Trust_Active()->count == 1 && memcmp(Trust_Active()->keys[0], BOOT_SIG_PUBLIC_DEV, 32) == 0, "the development bootloader trusts the development key and no other");
    TEST_ASSERT(Trust_MinCounter() == URTC_TRUST_MIN_COUNTER, "the compiled floor of the counter is the one of the public file");
}

static void test_trust_in_the_update(int *failures)
{
    uint8_t t[100];
    uint32_t size;
    Upd other = good(1500, 0, urtc_minor_field_pack(2, 0));
    other.other_key = 1;

    fresh();
    run_update(other);
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_SIGNATURE && main_slot_blank(), "a record signed by a key the table does not hold is refused");

    fresh();
    size = make_table(t, 2, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, BOOT_SIG_PUBLIC_OTHER, TRUST_FLAG_NEXT);
    Trust_UseTableForTest(t, size);
    run_update(other);
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK) && main_slot_equals_image(1500), "the same record installs when the table holds that key as the next one");
    fresh();
    Trust_UseTableForTest(t, size);
    run_update(good(1500, 0, urtc_minor_field_pack(2, 0)));
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK), "with two keys in the table the current one still installs");

    fresh();
    size = make_table(t, 2, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, BOOT_SIG_PUBLIC_OTHER, TRUST_FLAG_REVOKED);
    Trust_UseTableForTest(t, size);
    run_update(other);
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_SIGNATURE && main_slot_blank(), "a revoked key is refused even though its record is valid");

    fresh();
    size = make_table(t, 0, 0, 0, 0, 0);
    Trust_UseTableForTest(t, size);
    run_update(good(1500, 0, urtc_minor_field_pack(2, 0)));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_SIGNATURE && main_slot_blank() && !status_seen(STATUS_COPYING), "a closed table installs nothing, not even a valid record");

    fresh();
    size = make_table(t, 1, BOOT_SIG_PUBLIC_DEV, TRUST_FLAG_CURRENT, 0, 0);
    t[20] ^= 0x04;
    Trust_UseTableForTest(t, size);
    run_update(good(1500, 0, urtc_minor_field_pack(2, 0)));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_SIGNATURE && main_slot_blank(), "a table with a bad CRC trusts nobody, so nothing installs");

    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.signed_hw = 0x0303CB01UL; run_update(u); }
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_SIGNATURE && main_slot_blank(), "a record signed for another board does not install here");

    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); run_update(u); }
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK), "control: the development record installs with the development table");
    fresh();
    make_image(1500);
    start_update(1500, THIS_HARDWARE_ID);
    send_record(find_case(1500, 0, urtc_minor_field_pack(2, 0), 1)->record, 11);
    send_data(1500);
    end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(3, 0));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_SIGNATURE && main_slot_blank(), "a version in END_UPDATE other than the signed one breaks the signature");
    fresh();
    make_image(1500);
    start_update(1500, THIS_HARDWARE_ID);
    send_record(find_case(1500, 0, urtc_minor_field_pack(2, 0), 1)->record, 11);
    image[700] ^= 0x01;
    send_data(1500);
    image[700] ^= 0x01;
    end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(2, 0));
    TEST_ASSERT(fail_reason() != -1 && main_slot_blank(), "an image that is not the signed one never reaches the main slot");

    // the record may arrive in any order, with repeats, but it is forgotten by a new START
    fresh();
    make_image(1500);
    {
        const BootSigCase *c = find_case(1500, 0, urtc_minor_field_pack(2, 0), 1);
        start_update(1500, THIS_HARDWARE_ID);
        for (int i = 10; i >= 0; i--) send_chunk_at(c->record, i);
        send_chunk_at(c->record, 3);
        send_data(1500);
        end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(2, 0));
        TEST_ASSERT(status_seen(STATUS_VERIFY_OK), "the 11 chunks in reverse order, one of them twice, make a valid record");
    }
    fresh();
    make_image(1500);
    {
        const BootSigCase *c = find_case(1500, 0, urtc_minor_field_pack(2, 0), 1);
        start_update(1500, THIS_HARDWARE_ID);
        send_record(c->record, 11);
        start_update(1500, THIS_HARDWARE_ID);
        send_record(c->record, 10);
        send_data(1500);
        end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(2, 0));
        TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_INCOMPLETE, "a new START forgets the chunks of the earlier attempt");
    }

    // the key id announced after a version query
    fresh();
    HandleVersionQuery();
    const FakeTxFrame *kid = tx_find(CAN_ID_KEY_ID, 0);
    TEST_ASSERT(kid && kid->dlc == 8 && memcmp(kid->data, find_case(1500, 0, urtc_minor_field_pack(2, 0), 1)->record, 8) == 0, "the bootloader announces the id of its current key on 0x7ED");
    fresh();
    size = make_table(t, 0, 0, 0, 0, 0);
    Trust_UseTableForTest(t, size);
    HandleVersionQuery();
    TEST_ASSERT(tx_find(CAN_ID_KEY_ID, 0) == 0, "a bootloader that trusts nobody announces no key");
    fresh();
}

static void test_signed_counter(int *failures)
{
    FirmwareMetadata_t meta;

    fresh();
    seed_metadata_counter(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000, 5);
    { Upd u = good(1500, 0, urtc_minor_field_pack(4, 3)); u.counter = 3; run_update(u); }
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK && main_slot_blank(), "a signed counter below the floor is refused, though the version is newer");
    fresh();
    seed_metadata_counter(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000, 5);
    { Upd u = good(1500, 0, urtc_minor_field_pack(4, 3)); u.counter = 7; run_update(u); }
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK) && Metadata_Read(&meta) && meta.counter == 7, "a higher counter installs and becomes the new floor");
    fresh();
    seed_metadata_counter(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000, 1);
    run_update(good(1500, 0, urtc_minor_field_pack(4, 3)));
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK) && Metadata_Read(&meta) && meta.counter == 1, "a counter equal to the floor installs (the same release can be flashed again)");

    // the authorised downgrade lowers the version rule, never the counter
    fresh();
    seed_metadata_counter(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000, 5);
    make_image(1500);
    start_update(1500, THIS_HARDWARE_ID);
    {
        uint8_t magic[4] = { 0xD0, 0x9E, 0x12, 0xAD };
        HandleAuthorizeDowngrade(magic);
    }
    send_record(find_case(1500, 0, urtc_minor_field_pack(3, 0), 1)->record, 11);
    send_data(1500);
    end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(3, 0));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK && main_slot_blank(), "an authorised downgrade does not override the signed counter");
}

static const MetaRecord_t *log_slot(uint32_t index)
{
    return (const MetaRecord_t *)(METADATA_ADDR + index * META_RECORD_SIZE);
}

static uint32_t log_used_slots(void)
{
    uint32_t used = 0;
    for (uint32_t i = 0; i < META_RECORD_SLOTS; i++) {
        const uint8_t *p = (const uint8_t *)log_slot(i);
        for (uint32_t k = 0; k < META_RECORD_SIZE; k++) {
            if (p[k] != 0xFF) { used++; break; }
        }
    }
    return used;
}

static void commit_state(uint32_t minor_field, uint32_t size)
{
    seed_metadata(META_STATE_APP_VALID, 0, minor_field, size);
}

static void test_metadata_log(int *failures)
{
    FirmwareMetadata_t got;

    TEST_ASSERT(sizeof(FirmwareMetadata_t) == 64 && sizeof(MetaRecord_t) == META_RECORD_SIZE && META_RECORD_SLOTS == 28, "a record is 72 bytes and the page holds 28 of them");

    fresh();
    TEST_ASSERT(!Metadata_Read(&got) && got.magic == 0, "a blank page holds no state");
    for (uint32_t i = 1; i <= 5; i++) commit_state(i, 1000 + i);
    TEST_ASSERT(log_used_slots() == 5, "five commits append five records without erasing anything");
    TEST_ASSERT(Metadata_Read(&got) && got.version_minor == 5 && got.size == 1005, "the state is the last record");
    TEST_ASSERT(log_slot(0)->generation == 1 && log_slot(4)->generation == 5, "the generations count 1, 2, 3...");
    commit_state(5, 1005);
    TEST_ASSERT(log_used_slots() == 5, "committing the state that already is the last one writes nothing");

    // filling the page: the 29th commit erases it and starts a fresh log with the newest state first
    fresh();
    for (uint32_t i = 1; i <= META_RECORD_SLOTS; i++) commit_state(i, 2000 + i);
    TEST_ASSERT(log_used_slots() == META_RECORD_SLOTS, "28 commits fill the page");
    commit_state(29, 2029);
    TEST_ASSERT(log_used_slots() == 1 && Metadata_Read(&got) && got.version_minor == 29 && log_slot(0)->generation == 29, "the 29th commit erases the full page and keeps counting the generations");

    // a power cut at every halfword of a record leaves the previous state, never a damaged one
    int all_kept = 1, all_retried = 1;
    for (int cut = 1; cut <= 36; cut++) {
        fresh();
        commit_state(11, 1111);
        fake_flash_fail_program_nth = 36 + cut;   // each record takes 36 halfword programs
        FirmwareMetadata_t next;
        memset(&next, 0, sizeof next);
        next.magic = METADATA_MAGIC_VALID; next.state = META_STATE_APP_VALID; next.hardware_id = THIS_HARDWARE_ID;
        next.version_minor = 22; next.size = 2222; next.counter = 9;
        uint8_t ok = Metadata_EraseAndWrite(&next);
        fake_flash_fail_program_nth = 0;
        if (ok || !Metadata_Read(&got) || got.version_minor != 11 || got.size != 1111) all_kept = 0;
        if (!Metadata_EraseAndWrite(&next) || !Metadata_Read(&got) || got.version_minor != 22 || got.size != 2222 || got.counter != 9) all_retried = 0;
    }
    TEST_ASSERT(all_kept, "a power cut at any of the 36 halfwords of a record leaves the previous state in force");
    TEST_ASSERT(all_retried, "after the cut the same commit succeeds on the next free slot");

    // an erase that fails leaves the full page as it was
    fresh();
    for (uint32_t i = 1; i <= META_RECORD_SLOTS; i++) commit_state(i, 3000 + i);
    fake_flash_fail_erase_nth = 1;
    commit_state(40, 3040);
    fake_flash_fail_erase_nth = 0;
    TEST_ASSERT(Metadata_Read(&got) && got.version_minor == META_RECORD_SLOTS && log_used_slots() == META_RECORD_SLOTS, "a failed erase of the full page keeps the whole log");

    // a cut after the erase that makes room loses the log: the board starts again as a first boot, with the compiled-in floor (documented residual risk)
    int always_nothing_or_new = 1;
    for (int cut = 1; cut <= 36; cut++) {
        fresh();
        for (uint32_t i = 1; i <= META_RECORD_SLOTS; i++) commit_state(i, 3000 + i);
        fake_flash_fail_program_nth = (int)(36u * META_RECORD_SLOTS) + cut;
        commit_state(40, 3040);
        fake_flash_fail_program_nth = 0;
        if (Metadata_Read(&got) && got.version_minor != 40) always_nothing_or_new = 0;
    }
    TEST_ASSERT(always_nothing_or_new, "a cut while the full page is compacted leaves no state or the new one, never a damaged one");

    // a damaged record in the middle of the log is skipped, the newest valid one wins
    fresh();
    for (uint32_t i = 1; i <= 4; i++) commit_state(i, 4000 + i);
    {
        uint8_t zero[2] = { 0, 0 };
        Flash_WriteVerified(METADATA_ADDR + 3 * META_RECORD_SIZE + 20, zero, 2);   // spoil the 4th record (a bit clears: the CRC no longer matches)
    }
    TEST_ASSERT(Metadata_Read(&got) && got.version_minor == 3, "a record with a wrong CRC is ignored and the previous one stays in force");
}

void run_bootloader_update_tests(int *failures)
{
    void *m = mmap(0, IMG_MAX, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (m == MAP_FAILED || !fake_flash_init()) { printf("  SKIP: cannot map the fake flash on this host\n"); return; }
    image = m; image_addr = (uint32_t)(uintptr_t)m;

    // CRC32: the firmware table version matches an independent bitwise one.
    make_image(1000);
    TEST_ASSERT(CRC32_Finalize(CRC32_Update(0xFFFFFFFFu, image, 1000)) == ref_crc32(image, 1000), "CRC32 matches the reference algorithm");
    TEST_ASSERT(CRC32_Finalize(CRC32_Update(0xFFFFFFFFu, (const uint8_t *)"123456789", 9)) == 0xCBF43926u, "CRC32 check value of '123456789'");
    uint32_t split = CRC32_Update(CRC32_Update(0xFFFFFFFFu, image, 333), image + 333, 667);
    TEST_ASSERT(CRC32_Finalize(split) == ref_crc32(image, 1000), "CRC32 is incremental across split calls");

    // ---- happy path on a blank board ------------------------------------------
    fresh();
    { Upd u = good(3000, 0, urtc_minor_field_pack(5, 1)); u.counter = 5; run_update(u); }
    TEST_ASSERT(status_seen(STATUS_ERASING) && status_seen(STATUS_RECEIVING), "start reports ERASING then RECEIVING");
    TEST_ASSERT(status_seen(STATUS_VERIFYING) && status_seen(STATUS_COPYING) && status_seen(STATUS_VERIFY_OK), "end reports VERIFYING, COPYING, VERIFY_OK");
    TEST_ASSERT(!status_seen(STATUS_ERROR) && fail_reason() < 0, "no error or verify-fail frame on the happy path");
    TEST_ASSERT(main_slot_equals_image(3000), "main slot holds the image after the copy");
    TEST_ASSERT(memcmp((const void *)BACKUP_APP_ADDR, image, 3000) == 0, "backup slot holds the image (it is verified before the copy)");
    const FakeTxFrame *a0 = tx_find(CAN_ID_PAGE_ACK, 0), *a1 = tx_find(CAN_ID_PAGE_ACK, 1);
    TEST_ASSERT(a0 && a0->dlc == 4 && a0->data[3] == 0 && a1 && a1->data[3] == 1 && !tx_find(CAN_ID_PAGE_ACK, 2), "two page acks (0 and 1, big-endian) for a 3000-byte image");
    FirmwareMetadata_t meta;
    TEST_ASSERT(Metadata_Read(&meta) && meta.state == META_STATE_APP_VALID && meta.size == 3000 && meta.hardware_id == THIS_HARDWARE_ID, "metadata is APP_VALID with size and hardware id");
    TEST_ASSERT(meta.version_major == 0 && urtc_minor_field_minor(meta.version_minor) == 5 && urtc_minor_field_patch(meta.version_minor) == 1, "metadata keeps the version with its patch number");
    TEST_ASSERT(meta.crc32 == ref_crc32(image, 3000), "metadata stores the CRC32");
    TEST_ASSERT(meta.counter == 5, "metadata keeps the signed rollback counter as the new floor");
    const BootSigCase *happy = find_case(3000, 0, urtc_minor_field_pack(5, 1), 5);
    TEST_ASSERT(happy && memcmp(meta.sha256, happy->sha256, 32) == 0, "metadata keeps the SHA-256 of the installed application (equal to the independent one)");
    TEST_ASSERT(fake_reset_count == 1, "a successful update resets into the new application");
    TEST_ASSERT(fake_iwdg_refreshes > 0, "watchdog refreshed during the update");

    // ---- start validation ------------------------------------------------------
    fresh();
    start_update(0, THIS_HARDWARE_ID);
    TEST_ASSERT(status_seen(STATUS_ERROR) && !update_in_progress, "size 0 is rejected with STATUS_ERROR");
    fresh();
    start_update(APP_MAX_SIZE + 1, THIS_HARDWARE_ID);
    TEST_ASSERT(status_seen(STATUS_ERROR) && !update_in_progress, "size above the 112 KiB slot is rejected");
    fresh();
    start_update(APP_MAX_SIZE, THIS_HARDWARE_ID);
    TEST_ASSERT(update_in_progress && status_seen(STATUS_RECEIVING), "exactly the slot size is accepted");
    fresh();
    start_update(1000, THIS_HARDWARE_ID ^ 0x100);
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_HARDWARE_ID && !update_in_progress, "foreign hardware id: reason 0x04");
    TEST_ASSERT(!status_seen(STATUS_ERASING), "foreign hardware id is rejected before anything is erased");

    // A malformed START mid-transfer must not corrupt the transfer underway.
    fresh();
    make_image(1000);
    const BootSigCase *mid = find_case(1000, 0, urtc_minor_field_pack(1, 0), 1);
    start_update(1000, THIS_HARDWARE_ID);
    send_record(mid->record, 11);
    for (uint32_t off = 0; off < 400; off += 8) { uint8_t f[8]; memcpy(f, image + off, 8); HandleData(f, 8); }
    start_update(0, THIS_HARDWARE_ID);
    TEST_ASSERT(update_total_size == 1000 && update_bytes_received == 400 && update_in_progress, "rejected START keeps the transfer in progress untouched");
    for (uint32_t off = 400; off < 1000; off += 8) { uint8_t f[8] = {0}; uint32_t n = 1000 - off < 8 ? 1000 - off : 8; memcpy(f, image + off, n); HandleData(f, n); }
    fake_hal_reset_recording();
    end_update(ref_crc32(image, 1000), 0, urtc_minor_field_pack(1, 0));
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK) && main_slot_equals_image(1000), "the transfer still completes after the rejected START");

    // ---- frames outside a transfer / out of bounds ---------------------------
    fresh();
    send_data(64);
    TEST_ASSERT(update_bytes_received == 0 || !update_in_progress, "data before START is ignored");
    uint8_t any[8] = {1,2,3,4,5,6,7,8};
    HandleSigChunk(any);
    end_update(0, 0, 0);
    TEST_ASSERT(fake_tx_count == 0, "a record chunk or END before START produce no traffic");
    fresh();
    make_image(64);
    start_update(64, THIS_HARDWARE_ID);
    send_data(64);
    uint32_t before = update_bytes_received;
    HandleData(any, 8);
    TEST_ASSERT(update_bytes_received == before && before == 64, "data beyond the declared size is dropped");
    HandleData(any, 9);
    TEST_ASSERT(update_bytes_received == 64, "DLC above 8 is ignored");
    for (int idx = 11; idx < 256; idx += 41) {
        uint8_t far[8] = { (uint8_t)idx, 1, 2, 3, 4, 5, 6, 7 };
        HandleSigChunk(far);
    }
    TEST_ASSERT(1, "a chunk index beyond the 11 frames does not overflow the record buffer (covered by the sanitizers)");

    // ---- verification failures leave the main slot untouched ----------------
    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.chunks = 10; run_update(u); }
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_INCOMPLETE && update_failed, "a missing record chunk: INCOMPLETE");
    TEST_ASSERT(main_slot_blank(), "incomplete update never touches the main slot");
    { fake_hal_reset_recording(); send_data(8); HandleData(any, 8); end_update(0, 0, 0); }
    TEST_ASSERT(fake_tx_count == 0, "after a failure further frames are ignored until a new START");

    fresh();
    make_image(1500);
    start_update(1500, THIS_HARDWARE_ID);
    send_record(find_case(1500, 0, urtc_minor_field_pack(2, 0), 1)->record, 11);
    send_data(1000);
    end_update(0, 0, 0);
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_INCOMPLETE && main_slot_blank(), "fewer bytes than declared: INCOMPLETE");

    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.bad_crc = 1; run_update(u); }
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_CRC32 && main_slot_blank(), "wrong CRC32: reason 0x02, main slot blank");

    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.bad_sig = 1; run_update(u); }
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_SIGNATURE && main_slot_blank(), "a flipped bit in the signature: reason 0x03, main slot blank");
    TEST_ASSERT(!status_seen(STATUS_COPYING) && fake_reset_count == 0, "a bad signature never starts the copy or resets");

    // ---- anti-rollback ---------------------------------------------------------
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(3, 0)));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK && main_slot_blank(), "older minor is rejected as a rollback");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(4, 1)));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK, "lower patch of the same minor is a rollback");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(4, 2)));
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK), "re-flashing the same version is allowed");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(4, 3)));
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK), "a higher patch is installed");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, 4));
    TEST_ASSERT(status_seen(STATUS_VERIFY_OK), "an image from before the patch extension (legacy minor) is accepted over the same minor");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 1, 0, 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(9, 9)));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK, "lower major is a rollback whatever the minor");

    // Authorized downgrade: magic payload only, one-shot per attempt.
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    {
        make_image(1500);
        start_update(1500, THIS_HARDWARE_ID);
        uint8_t wrong_magic[4] = { 0xD0, 0x9E, 0x12, 0xAC };
        HandleAuthorizeDowngrade(wrong_magic);
        send_record(find_case(1500, 0, urtc_minor_field_pack(3, 0), 1)->record, 11); send_data(1500);
        end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(3, 0));
        TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK, "a wrong downgrade magic does not authorize");
    }
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    {
        make_image(1500);
        start_update(1500, THIS_HARDWARE_ID);
        uint8_t magic[4] = { 0xD0, 0x9E, 0x12, 0xAD };
        HandleAuthorizeDowngrade(magic);
        send_record(find_case(1500, 0, urtc_minor_field_pack(3, 0), 1)->record, 11); send_data(1500);
        end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(3, 0));
        TEST_ASSERT(status_seen(STATUS_VERIFY_OK) && main_slot_equals_image(1500), "the downgrade magic authorizes this attempt");
        Metadata_Read(&meta);
        TEST_ASSERT(urtc_minor_field_minor(meta.version_minor) == 3, "downgraded version is recorded");
    }
    // The payload URTC-COMMON writes down for 0x7FD is the one that authorizes.
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    {
        const uint8_t *shared = 0;
        for (int i = 0; i < URTC_VEC_COUNT(URTC_VEC_MAGIC); i++) {
            if (URTC_VEC_MAGIC[i].id == 0x7FD) shared = URTC_VEC_MAGIC[i].data;
        }
        TEST_ASSERT(shared != 0, "the shared vectors have a payload for 0x7FD");
        if (shared != 0) {
            make_image(1500);
            start_update(1500, THIS_HARDWARE_ID);
            uint8_t payload[4];
            memcpy(payload, shared, 4);
            HandleAuthorizeDowngrade(payload);
            send_record(find_case(1500, 0, urtc_minor_field_pack(3, 0), 1)->record, 11); send_data(1500);
            end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(3, 0));
            TEST_ASSERT(status_seen(STATUS_VERIFY_OK) && main_slot_equals_image(1500), "the 0x7FD payload of the shared vectors authorizes the downgrade");
        }
    }
    // The authorization from the previous attempt does not carry over.
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(3, 0)));
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK, "an earlier authorization is not reused by a new attempt");
    // Authorization given before START is cleared by START.
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    {
        uint8_t magic[4] = { 0xD0, 0x9E, 0x12, 0xAD };
        HandleAuthorizeDowngrade(magic);
        run_update(good(1500, 0, urtc_minor_field_pack(3, 0)));
        TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK, "START clears a stale downgrade authorization");
    }

    // ---- flash error paths -----------------------------------------------------
    fresh();
    fake_flash_fail_erase_nth = 1;
    start_update(1500, THIS_HARDWARE_ID);
    TEST_ASSERT(status_seen(STATUS_ERROR) && update_failed && !update_in_progress, "erase failure at START: STATUS_ERROR and the update is aborted");

    fresh();
    make_image(4000);
    start_update(4000, THIS_HARDWARE_ID);
    send_record(find_case(1500, 0, urtc_minor_field_pack(2, 0), 1)->record, 11);
    fake_flash_fail_program_nth = 5; // inside the first page write
    send_data(4000);
    TEST_ASSERT(status_seen(STATUS_ERROR) && update_failed && !update_in_progress, "flash program failure while receiving: STATUS_ERROR, update aborted");
    TEST_ASSERT(main_slot_blank(), "a backup-slot write failure leaves the main slot alone");

    // Copy failure after verification: metadata stays COPY_PENDING so the next boot resumes.
    fresh();
    make_image(3000);
    start_update(3000, THIS_HARDWARE_ID);
    send_record(find_case(3000, 0, urtc_minor_field_pack(6, 0), 1)->record, 11);
    send_data(3000);
    fake_flash_fail_erase_nth = 3; // erases so far: 2 backup pages at START (the metadata log only appends); the 3rd is the first main-slot page
    end_update(ref_crc32(image, 3000), 0, urtc_minor_field_pack(6, 0));
    TEST_ASSERT(status_seen(STATUS_COPYING) && status_seen(STATUS_ERROR) && !status_seen(STATUS_VERIFY_OK), "copy failure is reported and does not claim success");
    Metadata_Read(&meta);
    TEST_ASSERT(meta.state == META_STATE_COPY_PENDING, "interrupted copy leaves the metadata at COPY_PENDING");
    // "Reboot": the bootloader finishes the copy from the intact backup slot.
    fake_flash_fail_erase_nth = 0;
    fake_hal_reset_recording();
    TEST_ASSERT(ApplicationIsValid() == 1, "next boot resumes the pending copy and validates the application");
    Metadata_Read(&meta);
    TEST_ASSERT(meta.state == META_STATE_APP_VALID && main_slot_equals_image(3000), "after the resume the main slot is complete and APP_VALID");

    // ---- ApplicationIsValid ----------------------------------------------------
    fresh();
    TEST_ASSERT(ApplicationIsValid() == 0, "blank board: no application");
    fresh();
    { uint8_t sp_bad[4] = { 0, 0, 0, 0 }; Flash_WriteVerified(MAIN_APP_ADDR, sp_bad, 4); }
    TEST_ASSERT(ApplicationIsValid() == 0, "no metadata and an implausible stack pointer: not adopted");
    fresh();
    { uint8_t sp_ok[4] = { 0x00, 0x80, 0x00, 0x20 }; Flash_WriteVerified(MAIN_APP_ADDR, sp_ok, 4); } // 0x20008000
    TEST_ASSERT(ApplicationIsValid() == 1, "no metadata and a plausible stack pointer: existing image is adopted");
    Metadata_Read(&meta);
    TEST_ASSERT(meta.state == META_STATE_APP_VALID && meta.hardware_id == THIS_HARDWARE_ID && meta.size == APP_MAX_SIZE, "adopted image gets full metadata");
    TEST_ASSERT(urtc_minor_field_patch(meta.version_minor) == FIRMWARE_VERSION_PATCH, "adopted metadata carries the current patch number");
    TEST_ASSERT(ApplicationIsValid() == 1, "adopted image validates again on the next boot");
    // Tamper with one byte of the application: CRC no longer matches.
    { uint8_t z[2] = { 0, 0 }; Flash_WriteVerified(MAIN_APP_ADDR + 0x1000, z, 2); }
    TEST_ASSERT(ApplicationIsValid() == 0, "a modified application fails the CRC32 check");

    fresh();
    run_update(good(1500, 0, urtc_minor_field_pack(2, 0)));
    fake_hal_reset_recording();
    TEST_ASSERT(ApplicationIsValid() == 1, "a freshly flashed application validates");
    Metadata_Read(&meta);
    FirmwareMetadata_t bad = meta;
    bad.sha256[0] ^= 0xFF;
    Metadata_EraseAndWrite(&bad);
    TEST_ASSERT(ApplicationIsValid() == 0, "valid CRC but a wrong stored SHA-256: rejected");
    bad = meta; bad.hardware_id ^= 1;
    Metadata_EraseAndWrite(&bad);
    TEST_ASSERT(ApplicationIsValid() == 0, "metadata for another hardware id: rejected");
    bad = meta; bad.size = 0;
    Metadata_EraseAndWrite(&bad);
    TEST_ASSERT(ApplicationIsValid() == 0, "metadata with size 0: rejected");
    bad = meta; bad.size = APP_MAX_SIZE + 1;
    Metadata_EraseAndWrite(&bad);
    TEST_ASSERT(ApplicationIsValid() == 0, "metadata with an oversized image: rejected");

    // ---- version query 0x7F8 answered by the bootloader ---------------------------
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(3, 5), 100);
    HandleVersionQuery();
    const FakeTxFrame *v = tx_find(CAN_ID_VERSION_RESPONSE, 0);
    TEST_ASSERT(v && v->dlc == 8 && v->data[0] == 0x01, "bootloader answers 0x7F9 with byte0 = 1");
    if (v) {
        uint32_t hw = ((uint32_t)v->data[1] << 24) | ((uint32_t)v->data[2] << 16) | ((uint32_t)v->data[3] << 8) | v->data[4];
        TEST_ASSERT(hw == THIS_HARDWARE_ID && v->data[5] == 0 && v->data[6] == 0 && v->data[7] == 3, "0x7F9 reports the installed hardware id, major and minor");
    }
    const FakeTxFrame *p = tx_find(URTC_CAN_ID_VERSION_PATCH, 0);
    TEST_ASSERT(p && p->dlc == 3 && p->data[0] == 0x01 && p->data[1] == 5 && p->data[2] == URTC_PATCH_FLAG_END_UPDATE_PATCH, "0x7E9: bootloader side, installed patch 5, end-update-patch capability flag");
    const FakeTxFrame *b = tx_find(CAN_ID_BOOTLOADER_VERSION_RESPONSE, 0);
    TEST_ASSERT(b && b->dlc == 3 && b->data[0] == BOOTLOADER_VERSION_MAJOR && b->data[1] == BOOTLOADER_VERSION_MINOR && b->data[2] == BOOTLOADER_VERSION_PATCH, "0x7FA carries the bootloader's own version triplet");

    fresh();
    HandleVersionQuery();
    v = tx_find(CAN_ID_VERSION_RESPONSE, 0);
    TEST_ASSERT(v && v->data[1] == 0 && v->data[2] == 0 && v->data[3] == 0 && v->data[4] == 0, "no metadata: hardware id reads all zero");
    p = tx_find(URTC_CAN_ID_VERSION_PATCH, 0);
    TEST_ASSERT(p && p->data[1] == URTC_PATCH_UNKNOWN, "no metadata: patch is reported unknown (0xFF)");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, 3, 100); // legacy field, no patch
    HandleVersionQuery();
    p = tx_find(URTC_CAN_ID_VERSION_PATCH, 0);
    TEST_ASSERT(p && p->data[1] == URTC_PATCH_UNKNOWN, "legacy metadata (no patch flag): patch unknown, never 0");

    // ---- error counters ----------------------------------------------------------
    fresh();
    can_regs.ESR = (0x11u << 16) | (0x22u << 24);
    HandleErrorCounterQuery();
    const FakeTxFrame *ec = tx_find(CAN_ID_ERROR_COUNTERS_RESPONSE, 0);
    TEST_ASSERT(ec && ec->dlc == 2 && ec->data[0] == 0x11 && ec->data[1] == 0x22, "0x7FC = TEC then REC");

    // ---- readback 0x7FE -------------------------------------------------------------
    fresh();
    HandleReadbackStart();
    const FakeTxFrame *r0 = tx_find(CAN_ID_READBACK, 0);
    TEST_ASSERT(r0 && r0->dlc == 4 && r0->data[3] == 0 && !tx_find(CAN_ID_READBACK, 1), "readback with no valid application sends size 0 and nothing else");

    fresh();
    run_update(good(20, 0, urtc_minor_field_pack(1, 0)));
    update_in_progress = 0; // the reset after a successful update clears the RAM state
    fake_hal_reset_recording();
    fake_tick_step = 1;
    fake_rx_queue_count = 1;
    memset(&fake_rx_queue[0], 0, sizeof fake_rx_queue[0]);
    fake_rx_queue[0].hdr.StdId = CAN_ID_READBACK_PAGE_ACK; fake_rx_queue[0].hdr.DLC = 4; // page 0
    HandleReadbackStart();
    r0 = tx_find(CAN_ID_READBACK, 0);
    TEST_ASSERT(r0 && r0->dlc == 4 && r0->data[3] == 20, "readback announces the installed size");
    TEST_ASSERT(tx_find(CAN_ID_READBACK, 1) && tx_find(CAN_ID_READBACK, 1)->dlc == 8 && tx_find(CAN_ID_READBACK, 3) && tx_find(CAN_ID_READBACK, 3)->dlc == 4 && !tx_find(CAN_ID_READBACK, 4), "20 bytes are sent as 8 + 8 + 4");
    TEST_ASSERT(tx_find(CAN_ID_READBACK, 1) && memcmp(tx_find(CAN_ID_READBACK, 1)->data, image, 8) == 0, "readback data are the main-slot bytes");
    TEST_ASSERT(fake_rx_queue_count == 0, "the page ack was consumed");

    fake_hal_reset_recording();
    uint32_t t0 = fake_tick;
    fake_rx_queue_count = 0;
    HandleReadbackStart();
    TEST_ASSERT(fake_tick - t0 >= 3000, "without a page ack the readback gives up after about 3 s");

    update_in_progress = 1;
    fake_hal_reset_recording();
    HandleReadbackStart();
    TEST_ASSERT(fake_tx_count == 0, "readback is refused while an update is in progress");
    update_in_progress = 0;
    fake_tick_step = 0;

    // ---- no free mailbox: status is dropped, never blocks forever ---------------
    fresh();
    fake_tx_mailboxes_free = 0;
    CAN_SendStatus(STATUS_LISTENING);
    TEST_ASSERT(fake_tx_count == 0, "no mailbox: the status frame is dropped after the bounded wait");

    test_trust_table_parsing(failures);
    test_trust_in_the_update(failures);
    test_signed_counter(failures);
    test_metadata_log(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - bootloader update flow\n");
    run_bootloader_update_tests(&failures);
    if (failures == 0) {
        printf("  OK   all bootloader update host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
