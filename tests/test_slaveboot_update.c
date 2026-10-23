// =============================================================================
// URTC Firmware - Host logic tests for the expansion slave bootloader update flow
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// boot/slaveboot_protocol.c, boot/slaveboot_flash.c and boot/slaveboot_crypto.c (the bootloader of the STM32F303CB expansion slave, which the
// master updates over the I2C link) are compiled unchanged against the fake HAL in tests/host_hal/. The fake flash is a 256 KiB mapping at
// 0x08000000 that only lets bits go from 1 to 0 until a page is erased. The tests drive the update the way the I2C registers do (start, expected
// HMAC, data in pieces of up to 32 bytes, end) and assert on the status register, the reason of a failure, the progress, the flash content and the
// metadata: size and hardware-id validation, CRC32, HMAC-SHA256 signature, anti-rollback, flash error paths, resuming an interrupted copy after
// power loss, the adoption of an image that has no metadata and the two version answers.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "stm32f3xx_hal.h"
#include "slaveboot_common.h"
#include "slaveboot_crypto.h"
#include "slaveboot_flash.h"
#include "slaveboot_protocol.h"
#include "test_runner.h"
#include "urtc_version_wire.h"

extern uint32_t update_total_size, update_bytes_received;  // the progress of the transfer, defined in slaveboot_protocol.c and not in its header

IWDG_HandleTypeDef hiwdg;
I2C_HandleTypeDef hi2c1;

// ---- helpers -------------------------------------------------------------------------------------------------------------------------------
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

static void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void start_update(uint32_t size, uint32_t hw)
{
    uint8_t f[8];
    put_be32(f, size);
    put_be32(f + 4, hw);
    HandleStartUpdate(f);
}

static void send_data(uint32_t size, uint32_t piece)
{
    for (uint32_t off = 0; off < size; off += piece) {
        uint8_t f[32];
        uint32_t n = size - off < piece ? size - off : piece;
        memset(f, 0, sizeof f);
        memcpy(f, image + off, n);
        HandleData(f, n);
    }
}

static void end_update(uint32_t crc, uint16_t vmaj, uint16_t vminfield)
{
    uint8_t f[8];
    put_be32(f, crc);
    f[4] = (uint8_t)(vmaj >> 8);
    f[5] = (uint8_t)vmaj;
    f[6] = (uint8_t)(vminfield >> 8);
    f[7] = (uint8_t)vminfield;
    HandleEndUpdate(f);
}

static void seed_metadata(uint32_t state, uint32_t vmaj, uint32_t vminfield, uint32_t size)
{
    FirmwareMetadata_t m;
    memset(&m, 0, sizeof m);
    m.magic = METADATA_MAGIC_VALID;
    m.state = state;
    m.hardware_id = THIS_HARDWARE_ID;
    m.version_major = vmaj;
    m.version_minor = vminfield;
    m.size = size;
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
    current_status = STATUS_LISTENING;
    verify_fail_reason = 0;
    update_progress_percent = 0xFF;
    fake_tick = 0;
    fake_tick_step = 0;
}

typedef struct { int bad_crc; int bad_hmac; int skip_hmac; uint32_t declared_hw; uint32_t size; uint16_t vmaj; uint16_t vmin; uint32_t piece; } Upd;

static void run_update(Upd u)
{
    uint8_t hmac[32];
    make_image(u.size);
    sign_image(u.size, hmac);
    if (u.bad_hmac) hmac[31] ^= 0x01;
    start_update(u.size, u.declared_hw);
    if (!u.skip_hmac) HandleHmacExpected(hmac);
    send_data(u.size, u.piece);
    uint32_t crc = ref_crc32(image, u.size);
    if (u.bad_crc) crc ^= 1;
    end_update(crc, u.vmaj, u.vmin);
}

static Upd good(uint32_t size, uint16_t vmaj, uint16_t vmin)
{
    Upd u = { 0, 0, 0, THIS_HARDWARE_ID, size, vmaj, vmin, 32 };
    return u;
}

int main(void)
{
    int fails = 0;
    int *failures = &fails;
    printf("URTC host logic tests - the expansion slave bootloader update flow\n");
    void *m = mmap(0, IMG_MAX, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (m == MAP_FAILED || !fake_flash_init()) {
        printf("  SKIP: cannot map the fake flash on this host\n");
        return 0;
    }
    image = m;
    image_addr = (uint32_t)(uintptr_t)m;

    // CRC32: the firmware table version matches an independent bitwise one.
    make_image(1000);
    TEST_ASSERT(CRC32_Finalize(CRC32_Update(0xFFFFFFFFu, image, 1000)) == ref_crc32(image, 1000), "CRC32 matches the reference algorithm");
    TEST_ASSERT(CRC32_Finalize(CRC32_Update(0xFFFFFFFFu, (const uint8_t *)"123456789", 9)) == 0xCBF43926u, "CRC32 check value of '123456789'");
    uint32_t split = CRC32_Update(CRC32_Update(0xFFFFFFFFu, image, 333), image + 333, 667);
    TEST_ASSERT(CRC32_Finalize(split) == ref_crc32(image, 1000), "CRC32 is incremental across split calls");

    // ---- happy path on a blank board ------------------------------------------------------------------------------------------------
    fresh();
    run_update(good(3000, 0, urtc_minor_field_pack(5, 1)));
    TEST_ASSERT(current_status == STATUS_VERIFY_OK && verify_fail_reason == 0 && !update_failed, "the update ends in VERIFY_OK");
    TEST_ASSERT(main_slot_equals_image(3000), "main slot holds the image after the copy");
    TEST_ASSERT(memcmp((const void *)BACKUP_APP_ADDR, image, 3000) == 0, "backup slot holds the image (it is verified before the copy)");
    FirmwareMetadata_t meta;
    TEST_ASSERT(Metadata_Read(&meta) && meta.state == META_STATE_APP_VALID && meta.size == 3000 && meta.hardware_id == THIS_HARDWARE_ID, "metadata is APP_VALID with size and hardware id");
    TEST_ASSERT(meta.version_major == 0 && urtc_minor_field_minor(meta.version_minor) == 5 && urtc_minor_field_patch(meta.version_minor) == 1, "metadata keeps the version with its patch number");
    TEST_ASSERT(meta.crc32 == ref_crc32(image, 3000), "metadata stores the CRC32");
    TEST_ASSERT(fake_reset_count == 1, "a successful update resets into the new application");
    TEST_ASSERT(update_progress_percent == 100, "progress ends at 100");
    TEST_ASSERT(fake_iwdg_refreshes > 0, "watchdog refreshed during the update");

    // pieces of any size up to 32 bytes make the same update
    for (uint32_t piece = 1; piece <= 32; piece += 7) {
        fresh();
        Upd u = good(1234, 0, urtc_minor_field_pack(2, 0));
        u.piece = piece;
        run_update(u);
        TEST_ASSERT(current_status == STATUS_VERIFY_OK && main_slot_equals_image(1234), "the same update arrives whatever the size of the pieces");
    }

    // ---- start validation ------------------------------------------------------------------------------------------------------------------
    fresh();
    start_update(0, THIS_HARDWARE_ID);
    TEST_ASSERT(current_status == STATUS_ERROR && !update_in_progress, "size 0 is rejected with STATUS_ERROR");
    fresh();
    start_update(APP_MAX_SIZE + 1, THIS_HARDWARE_ID);
    TEST_ASSERT(current_status == STATUS_ERROR && !update_in_progress, "size above the 54 KiB slot is rejected");
    fresh();
    start_update(APP_MAX_SIZE, THIS_HARDWARE_ID);
    TEST_ASSERT(update_in_progress && current_status == STATUS_RECEIVING && update_progress_percent == 0, "exactly the slot size is accepted");
    fresh();
    start_update(1000, THIS_HARDWARE_ID ^ 0x100);
    TEST_ASSERT(current_status == STATUS_VERIFY_FAIL && verify_fail_reason == VERIFY_FAIL_REASON_HARDWARE_ID && !update_in_progress, "foreign hardware id: reason 0x04");
    TEST_ASSERT(update_progress_percent == 0xFF, "a foreign hardware id is rejected before anything is erased");

    // A rejected START in the middle of a transfer is validated before it touches the shared state, as in the main board's bootloader
    // (tests/test_bootloader_update.c): the size of the transfer underway is kept and the transfer still completes.
    fresh();
    make_image(1000);
    uint8_t hm[32];
    sign_image(1000, hm);
    start_update(1000, THIS_HARDWARE_ID);
    HandleHmacExpected(hm);
    send_data(384, 32);
    start_update(0, THIS_HARDWARE_ID);
    TEST_ASSERT(current_status == STATUS_ERROR && update_total_size == 1000 && update_bytes_received == 384 && update_in_progress, "a rejected START mid-transfer keeps the size and the progress of the transfer underway");
    start_update(1000, THIS_HARDWARE_ID ^ 0x100);
    TEST_ASSERT(update_total_size == 1000 && update_in_progress, "and so does one for foreign hardware");
    for (uint32_t off = 384; off < 1000; off += 32) {
        uint8_t f[32] = {0};
        uint32_t n = 1000 - off < 32 ? 1000 - off : 32;
        memcpy(f, image + off, n);
        HandleData(f, n);
    }
    end_update(ref_crc32(image, 1000), 0, urtc_minor_field_pack(1, 0));
    TEST_ASSERT(current_status == STATUS_VERIFY_OK && main_slot_equals_image(1000), "the transfer still completes after the rejected STARTs");

    // ---- frames outside a transfer / out of bounds -----------------------------------------------------------------------------------------
    fresh();
    uint8_t any[40];
    for (int i = 0; i < 40; i++) any[i] = (uint8_t)(i + 1);
    uint32_t received_before = update_bytes_received;
    HandleData(any, 8);
    HandleHmacExpected(any);
    end_update(0, 0, 0);
    TEST_ASSERT(update_bytes_received == received_before && current_status == STATUS_LISTENING && fake_reset_count == 0, "data, signature or END before START are ignored");
    fresh();
    make_image(64);
    start_update(64, THIS_HARDWARE_ID);
    send_data(64, 32);
    uint32_t before = update_bytes_received;
    HandleData(any, 8);
    TEST_ASSERT(update_bytes_received == before && before == 64, "data beyond the declared size is dropped");
    fresh();
    start_update(500, THIS_HARDWARE_ID);
    HandleData(any, 33);
    TEST_ASSERT(update_bytes_received == 0, "a transaction of more than 32 bytes is ignored");

    // ---- verification failures leave the main slot untouched ---------------------------------------------------------------------------------
    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.skip_hmac = 1; run_update(u); }
    TEST_ASSERT(verify_fail_reason == VERIFY_FAIL_REASON_INCOMPLETE && update_failed && current_status == STATUS_VERIFY_FAIL, "no expected signature received: INCOMPLETE");
    TEST_ASSERT(main_slot_blank(), "an incomplete update never touches the main slot");
    { uint8_t before_status = current_status; HandleData(any, 8); HandleHmacExpected(any); end_update(0, 0, 0); TEST_ASSERT(current_status == before_status, "after a failure further transactions are ignored until a new START"); }

    fresh();
    make_image(1500);
    sign_image(1500, hm);
    start_update(1500, THIS_HARDWARE_ID);
    HandleHmacExpected(hm);
    send_data(1000, 32);
    end_update(0, 0, 0);
    TEST_ASSERT(verify_fail_reason == VERIFY_FAIL_REASON_INCOMPLETE && main_slot_blank(), "fewer bytes than declared: INCOMPLETE");

    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.bad_crc = 1; run_update(u); }
    TEST_ASSERT(verify_fail_reason == VERIFY_FAIL_REASON_CRC32 && main_slot_blank(), "wrong CRC32: reason 0x02, main slot blank");

    fresh();
    { Upd u = good(1500, 0, urtc_minor_field_pack(2, 0)); u.bad_hmac = 1; run_update(u); }
    TEST_ASSERT(verify_fail_reason == VERIFY_FAIL_REASON_HMAC && main_slot_blank(), "wrong signature: reason 0x03, main slot blank");
    TEST_ASSERT(current_status == STATUS_VERIFY_FAIL && fake_reset_count == 0, "a bad signature never starts the copy or resets");

    // ---- anti-rollback -----------------------------------------------------------------------------------------------------------------------
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(3, 0)));
    TEST_ASSERT(verify_fail_reason == VERIFY_FAIL_REASON_ROLLBACK && main_slot_blank(), "older minor is rejected as a rollback");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(4, 1)));
    TEST_ASSERT(verify_fail_reason == VERIFY_FAIL_REASON_ROLLBACK, "lower patch of the same minor is a rollback");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(4, 2)));
    TEST_ASSERT(current_status == STATUS_VERIFY_OK, "re-flashing the same version is allowed");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(4, 3)));
    TEST_ASSERT(current_status == STATUS_VERIFY_OK, "a higher patch is installed");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(4, 2), 3000);
    run_update(good(1500, 0, 4));
    TEST_ASSERT(current_status == STATUS_VERIFY_OK, "an image from before the patch extension (legacy minor) is accepted over the same minor");

    fresh();
    seed_metadata(META_STATE_APP_VALID, 1, 0, 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(9, 9)));
    TEST_ASSERT(verify_fail_reason == VERIFY_FAIL_REASON_ROLLBACK, "lower major is a rollback whatever the minor");

    fresh();
    seed_metadata(META_STATE_COPY_PENDING, 0, urtc_minor_field_pack(9, 9), 3000);
    run_update(good(1500, 0, urtc_minor_field_pack(1, 0)));
    TEST_ASSERT(current_status == STATUS_VERIFY_OK, "the version check only looks at an installed (APP_VALID) application");

    // ---- flash error paths -----------------------------------------------------------------------------------------------------------------------
    fresh();
    fake_flash_fail_erase_nth = 1;
    start_update(1500, THIS_HARDWARE_ID);
    TEST_ASSERT(current_status == STATUS_ERROR && update_failed && !update_in_progress && update_progress_percent == 0xFF, "erase failure at START: STATUS_ERROR and the update is aborted");

    fresh();
    make_image(4000);
    sign_image(4000, hm);
    start_update(4000, THIS_HARDWARE_ID);
    HandleHmacExpected(hm);
    fake_flash_fail_program_nth = 5; // inside the first page write
    send_data(4000, 32);
    TEST_ASSERT(current_status == STATUS_ERROR && update_failed && !update_in_progress, "flash program failure while receiving: STATUS_ERROR, update aborted");
    TEST_ASSERT(main_slot_blank(), "a backup-slot write failure leaves the main slot alone");

    // Copy failure after verification: metadata stays COPY_PENDING so the next boot resumes.
    fresh();
    make_image(3000);
    sign_image(3000, hm);
    start_update(3000, THIS_HARDWARE_ID);
    HandleHmacExpected(hm);
    send_data(3000, 32);
    fake_flash_fail_erase_nth = 4; // erases so far: 2 backup pages at START, 1 metadata page; the 4th is the first main-slot page
    end_update(ref_crc32(image, 3000), 0, urtc_minor_field_pack(6, 0));
    TEST_ASSERT(current_status == STATUS_ERROR && update_failed, "copy failure is reported and does not claim success");
    Metadata_Read(&meta);
    TEST_ASSERT(meta.state == META_STATE_COPY_PENDING, "interrupted copy leaves the metadata at COPY_PENDING");
    // "Reboot": the bootloader finishes the copy from the intact backup slot.
    fake_flash_fail_erase_nth = 0;
    fake_hal_reset_recording();
    TEST_ASSERT(ApplicationIsValid() == 1, "next boot resumes the pending copy and validates the application");
    Metadata_Read(&meta);
    TEST_ASSERT(meta.state == META_STATE_APP_VALID && main_slot_equals_image(3000), "after the resume the main slot is complete and APP_VALID");

    // the metadata cannot be written twice when it is already what it should be
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(1, 0), 100);
    Metadata_Read(&meta);
    fake_flash_fail_erase_nth = 1;
    TEST_ASSERT(Metadata_EraseAndWrite(&meta) == 1, "an unchanged metadata record is not erased and rewritten");
    fake_flash_fail_erase_nth = 0;

    // ---- ApplicationIsValid ------------------------------------------------------------------------------------------------------------------------
    fresh();
    TEST_ASSERT(ApplicationIsValid() == 0, "blank board: no application");
    fresh();
    { uint8_t sp_bad[4] = { 0, 0, 0, 0 }; Flash_WriteVerified(MAIN_APP_ADDR, sp_bad, 4); }
    TEST_ASSERT(ApplicationIsValid() == 0, "no metadata and an implausible stack pointer: not adopted");
    fresh();
    { uint8_t sp_ok[4] = { 0x00, 0x80, 0x00, 0x20 }; Flash_WriteVerified(MAIN_APP_ADDR, sp_ok, 4); } // 0x20008000
    TEST_ASSERT(ApplicationIsValid() == 1, "no metadata and a plausible stack pointer: the existing image is adopted");
    Metadata_Read(&meta);
    TEST_ASSERT(meta.state == META_STATE_APP_VALID && meta.hardware_id == THIS_HARDWARE_ID && meta.size == APP_MAX_SIZE, "the adopted image gets full metadata");
    TEST_ASSERT(urtc_minor_field_patch(meta.version_minor) == FIRMWARE_VERSION_PATCH, "adopted metadata carries the current patch number");
    TEST_ASSERT(ApplicationIsValid() == 1, "the adopted image validates again on the next boot");
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
    bad = meta;
    bad.hardware_id ^= 1;
    Metadata_EraseAndWrite(&bad);
    TEST_ASSERT(ApplicationIsValid() == 0, "metadata for another hardware id: rejected");
    bad = meta;
    bad.size = 0;
    Metadata_EraseAndWrite(&bad);
    TEST_ASSERT(ApplicationIsValid() == 0, "metadata with size 0: rejected");
    bad = meta;
    bad.size = APP_MAX_SIZE + 1;
    Metadata_EraseAndWrite(&bad);
    TEST_ASSERT(ApplicationIsValid() == 0, "metadata with an oversized image: rejected");

    // ---- the two version answers ---------------------------------------------------------------------------------------------------------------
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, urtc_minor_field_pack(3, 5), 100);
    uint8_t v[10];
    BuildVersionQueryResponse(v);
    uint32_t hw = ((uint32_t)v[1] << 24) | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 8) | v[4];
    TEST_ASSERT(v[0] == 0x01 && hw == THIS_HARDWARE_ID && v[5] == 0 && v[6] == 0 && v[7] == 0 && v[8] == 3 && v[9] == BOOTLOADER_VERSION_MAJOR, "REG_QUERY_VERSION: bootloader byte, hardware id, major, minor and the bootloader major");
    uint8_t e[4];
    BuildVersionExtResponse(e);
    TEST_ASSERT(e[0] == URTC_SLAVE_VERSION_EXT_MARKER && e[1] == 5 && e[2] == BOOTLOADER_VERSION_MINOR && e[3] == BOOTLOADER_VERSION_PATCH, "REG_QUERY_VERSION_EXT: marker, installed patch 5 and the bootloader's own minor and patch");

    fresh();
    BuildVersionQueryResponse(v);
    BuildVersionExtResponse(e);
    TEST_ASSERT(v[1] == 0 && v[2] == 0 && v[3] == 0 && v[4] == 0 && v[7] == 0 && v[8] == 0, "no metadata: hardware id and version read all zero");
    TEST_ASSERT(e[1] == URTC_PATCH_UNKNOWN, "no metadata: the patch is reported unknown (0xFF)");
    fresh();
    seed_metadata(META_STATE_APP_VALID, 0, 3, 100);   // legacy field, no patch
    BuildVersionExtResponse(e);
    TEST_ASSERT(e[1] == URTC_PATCH_UNKNOWN, "legacy metadata (no patch flag): the patch is unknown, never 0");

    if (fails == 0) {
        printf("  OK   all expansion slave bootloader update host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", fails);
    return 1;
}
