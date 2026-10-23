// =============================================================================
// URTC Firmware - Host logic tests for the bootloader update flow
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// boot/bootloader_protocol.c, boot/bootloader_flash.c and boot/bootloader_crypto.c
// are compiled unchanged against the fake HAL in tests/host_hal/. The fake
// flash is a 256 KiB mapping at 0x08000000 that only lets bits go from 1 to 0
// until a page is erased, so a write to a non-erased page fails like the real
// part. The tests drive the CAN update sequence frame by frame (0x7F1 start,
// 0x7F7 signature chunks, 0x7F2 data, 0x7F4 end) and assert on the status
// frames, the flash content and the metadata: size / hardware-id validation,
// CRC32, HMAC-SHA256 signature, anti-rollback with the one-shot downgrade
// authorization, flash error paths, resuming an interrupted copy after power
// loss, and the 0x7F8 / 0x7FB / 0x7FE queries.
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
#include "urtc_version_wire.h"
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

static uint8_t *image;          // low-memory copy of the test image (HMAC takes a 32-bit address)
static uint32_t image_addr;
#define IMG_MAX 4096

static void make_image(uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) image[i] = (uint8_t)((i * 13 + 5) & 0xFF);
}

static void sign_image(uint32_t size, uint8_t hmac[32])
{
    hmac_sha256_flash_region(HMAC_KEY, 32, image_addr, size, hmac);
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

static void send_hmac(const uint8_t hmac[32], int chunks)
{
    for (int i = 0; i < chunks; i++) { uint8_t f[8]; memcpy(f, hmac + i * 8, 8); HandleHmacChunk(f); }
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

static void seed_metadata(uint32_t state, uint32_t vmaj, uint32_t vminfield, uint32_t size)
{
    FirmwareMetadata_t m;
    memset(&m, 0, sizeof m);
    m.magic = METADATA_MAGIC_VALID; m.state = state; m.hardware_id = THIS_HARDWARE_ID;
    m.version_major = vmaj; m.version_minor = vminfield; m.size = size;
    Metadata_EraseAndWrite(&m);
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
}

// Full signed update with optional tampering. Returns nothing; inspect the frames.
typedef struct { int bad_crc; int bad_hmac; int hmac_chunks; uint32_t declared_hw; uint32_t size; uint16_t vmaj; uint16_t vmin; } Upd;

static void run_update(Upd u)
{
    uint8_t hmac[32];
    make_image(u.size);
    sign_image(u.size, hmac);
    if (u.bad_hmac) hmac[31] ^= 0x01;
    start_update(u.size, u.declared_hw);
    send_hmac(hmac, u.hmac_chunks);
    send_data(u.size);
    uint32_t crc = ref_crc32(image, u.size);
    if (u.bad_crc) crc ^= 1;
    end_update(crc, u.vmaj, u.vmin);
}

static Upd good(uint32_t size, uint16_t vmaj, uint16_t vmin)
{
    Upd u = { 0, 0, 4, THIS_HARDWARE_ID, size, vmaj, vmin };
    return u;
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
    run_update(good(3000, 0, urtc_minor_field_pack(5, 1)));
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
    uint8_t hm[32];
    sign_image(1000, hm);
    start_update(1000, THIS_HARDWARE_ID);
    send_hmac(hm, 4);
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
    HandleHmacChunk(any);
    end_update(0, 0, 0);
    TEST_ASSERT(fake_tx_count == 0, "HMAC chunk or END before START produce no traffic");
    fresh();
    make_image(64);
    start_update(64, THIS_HARDWARE_ID);
    send_data(64);
    uint32_t before = update_bytes_received;
    HandleData(any, 8);
    TEST_ASSERT(update_bytes_received == before && before == 64, "data beyond the declared size is dropped");
    HandleData(any, 9);
    TEST_ASSERT(update_bytes_received == 64, "DLC above 8 is ignored");
    uint8_t h4[40] = {0};
    send_hmac(h4, 5);
    TEST_ASSERT(1, "a fifth HMAC chunk does not overflow the signature buffer (covered by the sanitizers)");

    // ---- verification failures leave the main slot untouched ----------------
    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.hmac_chunks = 3; run_update(u); }
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_INCOMPLETE && update_failed, "missing signature chunk: INCOMPLETE");
    TEST_ASSERT(main_slot_blank(), "incomplete update never touches the main slot");
    { fake_hal_reset_recording(); send_data(8); HandleData(any, 8); end_update(0, 0, 0); }
    TEST_ASSERT(fake_tx_count == 0, "after a failure further frames are ignored until a new START");

    fresh();
    make_image(1500);
    sign_image(1500, hm);
    start_update(1500, THIS_HARDWARE_ID);
    send_hmac(hm, 4);
    send_data(1000);
    end_update(0, 0, 0);
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_INCOMPLETE && main_slot_blank(), "fewer bytes than declared: INCOMPLETE");

    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.bad_crc = 1; run_update(u); }
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_CRC32 && main_slot_blank(), "wrong CRC32: reason 0x02, main slot blank");

    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.bad_hmac = 1; run_update(u); }
    TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_HMAC && main_slot_blank(), "wrong signature: reason 0x03, main slot blank");
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
        uint8_t hmac[32];
        make_image(1500); sign_image(1500, hmac);
        start_update(1500, THIS_HARDWARE_ID);
        uint8_t wrong_magic[4] = { 0xD0, 0x9E, 0x12, 0xAC };
        HandleAuthorizeDowngrade(wrong_magic);
        send_hmac(hmac, 4); send_data(1500);
        end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(3, 0));
        TEST_ASSERT(fail_reason() == VERIFY_FAIL_REASON_ROLLBACK, "a wrong downgrade magic does not authorize");
    }
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    {
        uint8_t hmac[32];
        make_image(1500); sign_image(1500, hmac);
        start_update(1500, THIS_HARDWARE_ID);
        uint8_t magic[4] = { 0xD0, 0x9E, 0x12, 0xAD };
        HandleAuthorizeDowngrade(magic);
        send_hmac(hmac, 4); send_data(1500);
        end_update(ref_crc32(image, 1500), 0, urtc_minor_field_pack(3, 0));
        TEST_ASSERT(status_seen(STATUS_VERIFY_OK) && main_slot_equals_image(1500), "the downgrade magic authorizes this attempt");
        Metadata_Read(&meta);
        TEST_ASSERT(urtc_minor_field_minor(meta.version_minor) == 3, "downgraded version is recorded");
    }
    // The payload URTC-COMMON writes down for 0x7FD is the one that authorizes.
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    {
        uint8_t hmac[32];
        const uint8_t *shared = 0;
        for (int i = 0; i < URTC_VEC_COUNT(URTC_VEC_MAGIC); i++) {
            if (URTC_VEC_MAGIC[i].id == 0x7FD) shared = URTC_VEC_MAGIC[i].data;
        }
        TEST_ASSERT(shared != 0, "the shared vectors have a payload for 0x7FD");
        if (shared != 0) {
            make_image(1500); sign_image(1500, hmac);
            start_update(1500, THIS_HARDWARE_ID);
            uint8_t payload[4];
            memcpy(payload, shared, 4);
            HandleAuthorizeDowngrade(payload);
            send_hmac(hmac, 4); send_data(1500);
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
    sign_image(4000, hm);
    start_update(4000, THIS_HARDWARE_ID);
    send_hmac(hm, 4);
    fake_flash_fail_program_nth = 5; // inside the first page write
    send_data(4000);
    TEST_ASSERT(status_seen(STATUS_ERROR) && update_failed && !update_in_progress, "flash program failure while receiving: STATUS_ERROR, update aborted");
    TEST_ASSERT(main_slot_blank(), "a backup-slot write failure leaves the main slot alone");

    // Copy failure after verification: metadata stays COPY_PENDING so the next boot resumes.
    fresh();
    make_image(3000);
    sign_image(3000, hm);
    start_update(3000, THIS_HARDWARE_ID);
    send_hmac(hm, 4);
    send_data(3000);
    fake_flash_fail_erase_nth = 4; // erases so far: 2 backup pages at START, 1 metadata page; the 4th is the first main-slot page
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
    bad.hmac[0] ^= 0xFF;
    Metadata_EraseAndWrite(&bad);
    TEST_ASSERT(ApplicationIsValid() == 0, "valid CRC but wrong stored signature: rejected");
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
