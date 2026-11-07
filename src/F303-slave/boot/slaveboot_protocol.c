// =============================================================================
// URTC Expansion Slave Bootloader - I2C-link protocol handlers, application
// validation, and jump
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
//
// The security-critical logic here (CRC32/SHA-256/HardwareID verification,
// anti-rollback, the backup-then-copy A/B sequence, resuming an
// interrupted copy on the next boot) is deliberately the same algorithm
// as the main board's own bootloader_protocol.c - none of that reasoning
// changes just because the transport underneath it is I2C instead of CAN.
// What's genuinely different: this chip can't push status/progress/acks
// proactively the way CAN_SendStatus et al. do on the main board - as an
// I2C SLAVE, it can only answer when the link-bus master (main board)
// actually reads a register, so status/progress live in plain variables
// (current_status, update_progress_percent) that the read side of the
// I2C1 interrupt handler in slaveboot_main.c serves on request instead.
// =============================================================================
#include "stm32f3xx_hal.h"
#include <string.h>
#include "slaveboot_common.h"
#include "slaveboot_protocol.h"
#include "slaveboot_crypto.h"
#include "slaveboot_flash.h"
#include "urtc_version_wire.h"
#include "urtc_slave_confirm.h"

// Defined here, read by the I2C1 interrupt handler's REG_STATUS read path.
volatile uint8_t current_status = STATUS_LISTENING;
// Defined here, read by the I2C1 interrupt handler's REG_VERIFY_FAIL_REASON
// read path - see slaveboot_protocol.h's own note on when this is meaningful.
volatile uint8_t verify_fail_reason = 0;

void BuildVersionQueryResponse(uint8_t out[10]) {
    FirmwareMetadata_t meta;
    uint8_t have_meta = Metadata_Read(&meta);
    uint32_t hw_id = have_meta ? meta.hardware_id : 0;
    uint16_t ver_major = have_meta ? (uint16_t)meta.version_major : 0;
    // Only the minor number itself: the high byte of the stored field may hold
    // the patch number, which a legacy host would misread as part of the minor.
    uint16_t ver_minor = have_meta ? (uint16_t)urtc_minor_field_minor(meta.version_minor) : 0;

    out[0] = 0x01; // answering as the bootloader, same convention as the main board's own 0x7F9 byte0
    out[1] = (uint8_t)(hw_id >> 24);
    out[2] = (uint8_t)(hw_id >> 16);
    out[3] = (uint8_t)(hw_id >> 8);
    out[4] = (uint8_t)(hw_id);
    out[5] = (uint8_t)(ver_major >> 8);
    out[6] = (uint8_t)(ver_major);
    out[7] = (uint8_t)(ver_minor >> 8);
    out[8] = (uint8_t)(ver_minor);
    out[9] = BOOTLOADER_VERSION_MAJOR; // this bootloader's own version - the main board's equivalent sends this as a separate CAN frame (0x7FA) since 0x7F9's 8 bytes were already full; a register read has no such length ceiling, so it just rides along as one extra byte here
}

void BuildVersionExtResponse(uint8_t out[4]) {
    FirmwareMetadata_t meta;
    uint8_t have_meta = Metadata_Read(&meta);
    urtc_slave_version_ext_build(out,
        have_meta ? urtc_minor_field_patch(meta.version_minor) : URTC_PATCH_UNKNOWN,
        BOOTLOADER_VERSION_MINOR, BOOTLOADER_VERSION_PATCH);
}

// The rollback floor of this slave: the compiled floor or the counter of the installed image, whichever is higher. An image whose signed counter is
// below it is refused. REG_COUNTER_FLOOR serves it to the master, which reads it before it verifies a record.
uint32_t Counter_Floor(void) {
    FirmwareMetadata_t meta;
    uint32_t floor = URTC_SLAVE_COUNTER_FLOOR;
    if (Metadata_Read(&meta) && meta.counter > floor) floor = meta.counter;
    return floor;
}

void BuildCounterFloorResponse(uint8_t out[4]) {
    uint32_t floor = Counter_Floor();
    out[0] = (uint8_t)(floor >> 24); out[1] = (uint8_t)(floor >> 16); out[2] = (uint8_t)(floor >> 8); out[3] = (uint8_t)floor;
}

// -----------------------------------------------------------------------
// Validates the MAIN slot and, if a copy from backup was left unfinished
// by an interrupted previous session, resumes and completes it before
// re-checking. Returns 1 if the main slot ends up holding a verified,
// jumpable application; 0 otherwise (bootloader stays in listening mode).
//
// Same two real RAM regions and same reasoning as the main board's own
// StackPointerInValidRAM - this chip is the same xB/xC density line
// (STM32F303CB), confirmed against the same ST datasheet family (DS9118)
// that already covers the main board's own F303CC. The F303CB has 32KB of SRAM at 0x20000000 and 8KB of CCM (CPU-only, no DMA) at 0x10000000; the
// check below still accepts a stack top up to SRAM_BASE + 0xA000 because the applications linked before the script said 32KB end their stack there, and
// an installed application must not be refused for it.
// -----------------------------------------------------------------------
static uint8_t StackPointerInValidRAM(uint32_t sp) {
    uint8_t in_sram = (sp >= SRAM_BASE + 0x100UL && sp <= SRAM_BASE + 0xA000UL);
    uint8_t in_ccm  = (sp >= CCMDATARAM_BASE + 0x100UL && sp <= CCMDATARAM_BASE + 0x2000UL);
    return in_sram || in_ccm;
}

uint8_t ApplicationIsValid(void) {
    FirmwareMetadata_t meta;
    if (!Metadata_Read(&meta)) {
        // No valid metadata yet - same reasoning as the main board's own
        // bootloader: distinguish a genuinely blank chip (erased flash
        // reads 0xFFFFFFFF, not a valid RAM address) from one that was
        // JTAG-flashed directly, by checking whether the stack pointer at
        // the start of the main slot plausibly sits inside real RAM.
        uint32_t app_stack = *(volatile uint32_t*)MAIN_APP_ADDR;
        if (!StackPointerInValidRAM(app_stack)) return 0;

        FirmwareMetadata_t adopted = {0};
        adopted.magic = METADATA_MAGIC_VALID;
        adopted.state = META_STATE_APP_VALID;
        adopted.hardware_id = THIS_HARDWARE_ID;
        adopted.version_major = FIRMWARE_VERSION_MAJOR;
        adopted.version_minor = urtc_minor_field_pack(FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH);
        adopted.size = APP_MAX_SIZE;
        uint32_t crc = 0xFFFFFFFFUL;
        crc = CRC32_Update(crc, (const uint8_t*)MAIN_APP_ADDR, APP_MAX_SIZE);
        adopted.crc32 = CRC32_Finalize(crc);
    adopted.counter = URTC_SLAVE_COUNTER_FLOOR;
        sha256_flash_region(MAIN_APP_ADDR, APP_MAX_SIZE, adopted.sha256);
        return Metadata_EraseAndWrite(&adopted) ? 1 : 0;
    }

    // Validated before anything below reads meta.size or meta.hardware_id,
    // including the COPY_PENDING resume branch just below - same reasoning
    // as the main board's own bootloader_protocol.c: the metadata page's
    // fields are written in ascending address order, and only magic is
    // checked by Metadata_Read, so a power loss between the magic+state
    // halfwords committing and the hardware_id/size halfwords committing
    // leaves state==COPY_PENDING with hardware_id/size still reading as
    // erased flash (0xFFFFFFFF). Gating here means that torn-write case is
    // caught before Flash_CopyRegion ever sees an unbounded size.
    if (meta.hardware_id != THIS_HARDWARE_ID) return 0;
    if (meta.size == 0 || meta.size > APP_MAX_SIZE) return 0;

    if (meta.state == META_STATE_COPY_PENDING) {
        // The backup slot must still hold the image that was verified and confirmed before the cut: if it does not, the main slot is left alone.
        uint8_t backup_digest[32];
        sha256_flash_region(BACKUP_APP_ADDR, meta.size, backup_digest);
        if (!constant_time_equal(backup_digest, meta.sha256, 32)) return 0;
        current_status = STATUS_COPYING;
        if (!Flash_CopyRegion(MAIN_APP_ADDR, BACKUP_APP_ADDR, meta.size)) {
            return 0;
        }
        FirmwareMetadata_t done = meta;
        done.state = META_STATE_APP_VALID;
        if (!Metadata_EraseAndWrite(&done)) return 0;
        meta = done;
    }

    uint32_t crc = 0xFFFFFFFFUL;
    crc = CRC32_Update(crc, (const uint8_t*)MAIN_APP_ADDR, meta.size);
    crc = CRC32_Finalize(crc);
    if (crc != meta.crc32) return 0;

    // At every boot only corruption of the flash has to be caught: a SHA-256 with no key, kept in the metadata.
    uint8_t actual_digest[32];
    sha256_flash_region(MAIN_APP_ADDR, meta.size, actual_digest);
    if (!constant_time_equal(actual_digest, meta.sha256, 32)) return 0;

    return 1;
}

void JumpToApplication(void) {
    typedef void (*pFunction)(void);
    uint32_t app_stack = *(volatile uint32_t*)MAIN_APP_ADDR;
    uint32_t app_reset_vector = *(volatile uint32_t*)(MAIN_APP_ADDR + 4);

    // Same four checks, same ordering, same reasoning as the main board's
    // own JumpToApplication - see its own comment for the full rationale
    // on each one. Sanity check still runs before any peripheral teardown,
    // so a failure here returns to the caller with I2C1 still alive to
    // keep listening on.
    if (!StackPointerInValidRAM(app_stack) ||
        (app_stack & 0x3UL) != 0 ||
        (app_reset_vector & 0x1UL) == 0 ||
        app_reset_vector < MAIN_APP_ADDR ||
        app_reset_vector >= MAIN_APP_ADDR + APP_MAX_SIZE) {
        return;
    }

    HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
    HAL_I2C_DeInit(&hi2c1);
    HAL_RCC_DeInit();
    HAL_DeInit();
    // Same safe-state pattern as the main board's own bootloader -
    // 0xEBFFFFFF preserves PA13/PA14 (SWD).
    GPIOA->MODER = 0xEBFFFFFF;
    GPIOB->MODER = 0xFFFFFFFF;
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;

    __disable_irq();
    SCB->VTOR = MAIN_APP_ADDR;
    __DSB();
    __ISB();

    pFunction app_entry = (pFunction)app_reset_vector;
    __set_MSP(app_stack);
    app_entry();
}

// -----------------------------------------------------------------------
// Update flow - all writes during an update go to the BACKUP slot only.
// Same state machine as the main board's own bootloader_protocol.c;
// REG_DATA moves up to 32 bytes per transaction rather than CAN's fixed
// 8-byte DLC ceiling, so HandleData's inner loop bound changes to match,
// but the page-buffer-then-flush logic underneath is unchanged.
// -----------------------------------------------------------------------
uint32_t update_total_size = 0;
static uint32_t update_declared_hw_id = 0;
uint32_t update_bytes_received = 0;
static uint32_t update_running_crc = 0xFFFFFFFFUL;
static uint8_t  update_expected_digest[32];   // the expected SHA-256 of the image
static uint8_t  update_hmac_received = 0;
// The confirmation by the master (urtc_slave_confirm.h). The nonce is made at START and read by the master from REG_CHALLENGE; what was verified at END is
// kept here until the confirmation arrives. All of it is cleared by a new START, by a failure and by the first REG_CONFIRM of the session.
static uint8_t  update_nonce[URTC_SLAVE_CONFIRM_NONCE_SIZE];
static uint8_t  update_nonce_valid = 0;
// The signed counter of the record the master verified, from REG_RECORD_COUNTER; the confirmation is bound to it and it is checked against the floor.
static uint32_t update_record_counter = 0;
static uint8_t  update_counter_received = 0;
static uint32_t update_session_counter = 0;
uint8_t  update_awaiting_confirm = 0;
static uint8_t  await_digest[32];
static uint16_t await_version_major = 0;
static uint16_t await_version_minor = 0;
static uint32_t await_crc = 0;
static uint8_t  page_buffer[FLASH_PAGE_SIZE];
static uint32_t page_buffer_fill = 0;
static uint32_t current_page_index = 0;
uint8_t  update_in_progress = 0;
uint8_t  update_failed = 0;
uint32_t update_last_activity_tick = 0;
// The steps that the interrupt hands to the main loop (QueueEndUpdate, QueueConfirm) and the reset that follows a confirmed update.
#define RESET_PAUSE_MS 200u
enum { DEFER_NONE = 0, DEFER_END = 1, DEFER_CONFIRM = 2 };
static volatile uint8_t deferred_op = DEFER_NONE;
static uint8_t deferred_data[32];
static volatile uint8_t reset_due = 0;
static volatile uint32_t reset_due_tick = 0;

static uint8_t FlushPageBuffer(void) {
    uint32_t page_addr = BACKUP_APP_ADDR + (current_page_index * FLASH_PAGE_SIZE);
    for (uint32_t i = page_buffer_fill; i < FLASH_PAGE_SIZE; i++) {
        page_buffer[i] = 0xFF;
    }
    uint8_t ok = Flash_WriteVerified(page_addr, page_buffer, FLASH_PAGE_SIZE);
    if (ok) {
        current_page_index++;
        page_buffer_fill = 0;
        HAL_IWDG_Refresh(&hiwdg);
        update_progress_percent = (update_total_size > 0) ?
            (uint8_t)(((uint64_t)update_bytes_received * 100) / update_total_size) : 0;
    }
    return ok;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

// A fresh nonce for every session: the hash of the unique id of the chip, the tick, a counter that only grows while the chip stays powered, the SysTick
// value and the previous nonce. It is a FRESHNESS value, not a secret and not a random number: the chip has no random source, and the master reads it
// from REG_CHALLENGE on the same bus. It makes a confirmation recorded in one session useless in the next one (a new START changes it).
static void MakeChallenge(void) {
    uint8_t seed[12 + 4 + 4 + 4 + URTC_SLAVE_CONFIRM_NONCE_SIZE];
    uint8_t hash[32];
    put32(seed, HAL_GetUIDw0());
    put32(seed + 4, HAL_GetUIDw1());
    put32(seed + 8, HAL_GetUIDw2());
    put32(seed + 12, HAL_GetTick());
    put32(seed + 16, ++update_session_counter);
    put32(seed + 20, SysTick->VAL);
    memcpy(seed + 24, update_nonce, URTC_SLAVE_CONFIRM_NONCE_SIZE);
    sha256_buffer(seed, sizeof seed, hash);
    memcpy(update_nonce, hash, URTC_SLAVE_CONFIRM_NONCE_SIZE);
    update_nonce_valid = 1;
}

void BuildChallengeResponse(uint8_t out[16]) {
    if (update_nonce_valid) memcpy(out, update_nonce, URTC_SLAVE_CONFIRM_NONCE_SIZE);
    else memset(out, 0, URTC_SLAVE_CONFIRM_NONCE_SIZE);
}

void HandleStartUpdate(uint8_t *data) {
    // Parsed into locals and validated before the shared update_total_size
    // and update_declared_hw_id are touched, as the main board's bootloader
    // does: a rejected START arriving in the middle of a transfer (a retried
    // frame, a bit-flip on the bus) must not leave the transfer underway
    // with a size that no longer matches what was erased for it.
    uint32_t new_total_size = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                             | ((uint32_t)data[2] << 8) | data[3];
    uint32_t new_hw_id = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                        | ((uint32_t)data[6] << 8) | data[7];

    if (new_total_size == 0 || new_total_size > APP_MAX_SIZE) {
        current_status = STATUS_ERROR;
        return;
    }
    if (new_hw_id != THIS_HARDWARE_ID) {
        current_status = STATUS_VERIFY_FAIL;
        verify_fail_reason = VERIFY_FAIL_REASON_HARDWARE_ID;
        return;
    }
    update_total_size = new_total_size;
    update_declared_hw_id = new_hw_id;

    uint32_t pages_needed = (update_total_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    current_status = STATUS_ERASING;
    if (!Flash_ErasePages(BACKUP_APP_ADDR, pages_needed)) {
        current_status = STATUS_ERROR;
        update_failed = 1;
        update_in_progress = 0;
        // Restores the documented "not currently updating" sentinel
        // (slaveboot_flash.h) rather than leaving whatever percentage a
        // PRIOR update left behind - REG_STATUS and REG_PROGRESS are read
        // independently, so without this a host could see STATUS_ERROR
        // paired with a stale, misleadingly high leftover percentage.
        update_progress_percent = 0xFF;
        return;
    }

    memset(page_buffer, 0xFF, sizeof(page_buffer));
    update_bytes_received = 0;
    update_running_crc = 0xFFFFFFFFUL;
    update_hmac_received = 0;
    update_awaiting_confirm = 0;
    update_counter_received = 0;
    update_record_counter = 0;
    MakeChallenge();
    page_buffer_fill = 0;
    current_page_index = 0;
    update_in_progress = 1;
    update_failed = 0;
    update_progress_percent = 0;
    update_last_activity_tick = HAL_GetTick();
    current_status = STATUS_RECEIVING;
}

void HandleDigestExpected(uint8_t *data) {
    if (!update_in_progress || update_failed || update_awaiting_confirm) return;
    memcpy(update_expected_digest, data, 32); // REG_DIGEST_EXPECTED is always exactly 32 bytes in one transaction - no chunking needed the way CAN's 8-byte frames required
    update_hmac_received = 1;
    update_last_activity_tick = HAL_GetTick();
}

// REG_RECORD_COUNTER, 4 bytes big-endian. Only before the END frame: after it the counter is already part of what was checked.
void HandleRecordCounter(uint8_t *data) {
    if (!update_in_progress || update_failed || update_awaiting_confirm) return;
    update_record_counter = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    update_counter_received = 1;
    update_last_activity_tick = HAL_GetTick();
}

void HandleData(uint8_t *data, uint32_t len) {
    if (!update_in_progress || update_failed || update_awaiting_confirm) return;
    if (len > 32) return; // REG_DATA's own documented ceiling - guards against a malformed/oversized transaction the same way the main board's own DLC>8 check does for CAN
    update_last_activity_tick = HAL_GetTick();

    uint32_t i;
    for (i = 0; i < len; i++) {
        if (update_bytes_received >= update_total_size) break;
        page_buffer[page_buffer_fill++] = data[i];
        update_bytes_received++;

        if (page_buffer_fill == FLASH_PAGE_SIZE) {
            if (!FlushPageBuffer()) {
                update_running_crc = CRC32_Update(update_running_crc, data, i + 1);
                current_status = STATUS_ERROR;
                update_in_progress = 0;
                update_failed = 1;
                return;
            }
        }
    }
    update_running_crc = CRC32_Update(update_running_crc, data, i);
}

void HandleEndUpdate(uint8_t *data) {
    if (!update_in_progress || update_failed || update_awaiting_confirm) return;
    update_last_activity_tick = HAL_GetTick();

    if (page_buffer_fill > 0) {
        if (!FlushPageBuffer()) {
            current_status = STATUS_ERROR;
            update_in_progress = 0;
            update_failed = 1;
            return;
        }
    }

    if (update_bytes_received != update_total_size || !update_hmac_received) {
        current_status = STATUS_VERIFY_FAIL;
        verify_fail_reason = VERIFY_FAIL_REASON_INCOMPLETE;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    uint32_t expected_crc = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                           | ((uint32_t)data[2] << 8) | data[3];
    uint16_t version_major = ((uint16_t)data[4] << 8) | data[5];
    uint16_t version_minor = ((uint16_t)data[6] << 8) | data[7];
    uint32_t actual_crc = CRC32_Finalize(update_running_crc);

    current_status = STATUS_VERIFYING;

    if (actual_crc != expected_crc) {
        current_status = STATUS_VERIFY_FAIL;
        verify_fail_reason = VERIFY_FAIL_REASON_CRC32;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    // Integrity of what arrived: the SHA-256 (no key) of the backup slot against the digest the master sent. WHO vouched for the image is the master's
    // business: it verifies the Ed25519 record and confirms below (HandleConfirm).
    uint8_t actual_digest[32];
    sha256_flash_region(BACKUP_APP_ADDR, update_total_size, actual_digest);
    if (!constant_time_equal(actual_digest, update_expected_digest, 32)) {
        current_status = STATUS_VERIFY_FAIL;
        verify_fail_reason = VERIFY_FAIL_REASON_HMAC;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    // Anti-rollback by counter: the record the master verified carries a signed counter, and an image below the floor this slave keeps is refused
    // however well it is signed. The master sends the counter before END; if it did not, the confirmation below cannot match and the update fails closed.
    if (update_counter_received && update_record_counter < Counter_Floor()) {
        current_status = STATUS_VERIFY_FAIL;
        verify_fail_reason = VERIFY_FAIL_REASON_ROLLBACK;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    // Anti-rollback - same reasoning as the main board's own bootloader:
    // a cryptographically valid image is still rejected if it declares a
    // version older than what's already running, so a validly-signed
    // image with a since-discovered vulnerability can't be replayed
    // indefinitely.
    FirmwareMetadata_t current_meta;
    if (Metadata_Read(&current_meta) && current_meta.state == META_STATE_APP_VALID) {
        // The minor field may carry the PATCH number in its high byte (see
        // urtc_version_wire.h); the patch only takes part when both sides know it.
        if (urtc_version_is_older(version_major, version_minor,
                                  current_meta.version_major, current_meta.version_minor)) {
            current_status = STATUS_VERIFY_FAIL;
            verify_fail_reason = VERIFY_FAIL_REASON_ROLLBACK;
            update_in_progress = 0;
            update_failed = 1;
            return;
        }
    }

    // Everything the slave can check alone has passed. Nothing is written to the metadata and the main slot is not touched until the master confirms that
    // it verified the signature record of THIS image in THIS session; without the confirmation the update fails closed (HandleConfirm, AbortUnconfirmed).
    memcpy(await_digest, actual_digest, 32);
    await_version_major = version_major;
    await_version_minor = version_minor;
    await_crc = actual_crc;
    update_awaiting_confirm = 1;
    update_last_activity_tick = HAL_GetTick();
    current_status = STATUS_AWAIT_CONFIRM;
}

static void FailUpdate(uint8_t reason) {
    current_status = STATUS_VERIFY_FAIL;
    verify_fail_reason = reason;
    update_awaiting_confirm = 0;
    update_nonce_valid = 0;
    update_in_progress = 0;
    update_failed = 1;
}

void AbortUnconfirmed(void) {
    if (update_awaiting_confirm) FailUpdate(VERIFY_FAIL_REASON_NOT_CONFIRMED);
}

void HandleConfirm(uint8_t *data) {
    if (!update_in_progress || update_failed || !update_awaiting_confirm) return;   // nothing asked for it: ignored
    // One use: whatever this confirmation turns out to be, the session cannot be confirmed again.
    update_awaiting_confirm = 0;
    update_nonce_valid = 0;
    update_last_activity_tick = HAL_GetTick();

    // The backup slot must still hold what was verified at END: it is read again now, so an image altered after the checks (or after the master's
    // confirmation was computed) is caught here, not trusted from the first pass.
    uint8_t now_digest[32];
    sha256_flash_region(BACKUP_APP_ADDR, update_total_size, now_digest);
    if (!constant_time_equal(now_digest, await_digest, 32)) {
        FailUpdate(VERIFY_FAIL_REASON_HMAC);
        return;
    }
    // The tag the master must have computed: the nonce of this session, this image, this hardware, this version, this size and the counter of the record,
    // all taken from the slave's own state and not from the master. A master that never sent the counter cannot have the tag accepted.
    if (!update_counter_received) {
        FailUpdate(VERIFY_FAIL_REASON_NOT_CONFIRMED);
        return;
    }
    uint8_t message[URTC_SLAVE_CONFIRM_MESSAGE_SIZE];
    uint8_t expected_tag[URTC_SLAVE_CONFIRM_TAG_SIZE];
    urtc_slave_confirm_message(message, update_nonce, now_digest, update_declared_hw_id, await_version_major, await_version_minor, update_total_size, update_record_counter);
    sha256_buffer(message, sizeof message, expected_tag);
    if (!constant_time_equal(expected_tag, data, URTC_SLAVE_CONFIRM_TAG_SIZE)) {
        FailUpdate(VERIFY_FAIL_REASON_NOT_CONFIRMED);
        return;
    }
    // Checked again here, at the last moment before anything is written: the floor is what is in the metadata NOW.
    if (update_record_counter < Counter_Floor()) {
        FailUpdate(VERIFY_FAIL_REASON_ROLLBACK);
        return;
    }

    FirmwareMetadata_t pending = {0};
    pending.magic = METADATA_MAGIC_VALID;
    pending.state = META_STATE_COPY_PENDING;
    pending.hardware_id = update_declared_hw_id;
    pending.version_major = await_version_major;
    pending.version_minor = await_version_minor;
    pending.size = update_total_size;
    pending.crc32 = await_crc;
    pending.counter = update_record_counter;   // the new floor travels in the same record that says the copy is pending: one append, no window without it
    memcpy(pending.sha256, await_digest, 32);
    if (!Metadata_EraseAndWrite(&pending)) {
        current_status = STATUS_ERROR;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    current_status = STATUS_COPYING;
    update_progress_percent = 0;
    if (!Flash_CopyRegion(MAIN_APP_ADDR, BACKUP_APP_ADDR, update_total_size)) {
        // Main slot may be partially written, but metadata still says
        // META_STATE_COPY_PENDING and backup is untouched - same recovery
        // path as the main board's own bootloader: next boot's
        // ApplicationIsValid() resumes the copy from backup.
        current_status = STATUS_ERROR;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    // The copy is read back against the digest that was confirmed; a main slot that differs stays COPY_PENDING, so the next boot repeats the copy.
    uint8_t main_digest[32];
    sha256_flash_region(MAIN_APP_ADDR, update_total_size, main_digest);
    if (!constant_time_equal(main_digest, await_digest, 32)) {
        current_status = STATUS_ERROR;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    FirmwareMetadata_t done = pending;
    done.state = META_STATE_APP_VALID;
    if (!Metadata_EraseAndWrite(&done)) {
        current_status = STATUS_ERROR;
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    current_status = STATUS_VERIFY_OK;
    update_progress_percent = 100;
    // A brief pause so a master polling REG_STATUS has a real chance to observe STATUS_VERIFY_OK before the reset: ServiceDeferred() waits it out in the main
    // loop (a HAL_Delay here, inside the interrupt, never ended when the SysTick could not preempt it) and then resets.
    reset_due_tick = HAL_GetTick();
    reset_due = 1;
}

// ---- the heavy steps out of the interrupt --------------------------------------------------------------------------------------------------------
void QueueEndUpdate(uint8_t *data) {
    if (deferred_op != DEFER_NONE || reset_due) return;
    if (!update_in_progress || update_failed || update_awaiting_confirm) return;   // the entry conditions of HandleEndUpdate
    memcpy(deferred_data, data, 8);
    update_last_activity_tick = HAL_GetTick();
    current_status = STATUS_VERIFYING;
    deferred_op = DEFER_END;
}

void QueueConfirm(uint8_t *data) {
    if (deferred_op != DEFER_NONE || reset_due) return;
    if (!update_in_progress || update_failed || !update_awaiting_confirm) return;  // the entry conditions of HandleConfirm
    memcpy(deferred_data, data, URTC_SLAVE_CONFIRM_TAG_SIZE);
    update_last_activity_tick = HAL_GetTick();
    deferred_op = DEFER_CONFIRM;
}

uint8_t DeferredBusy(void) {
    return (deferred_op != DEFER_NONE || reset_due) ? 1u : 0u;
}

void ServiceDeferred(void) {
    const uint8_t op = deferred_op;
    if (op != DEFER_NONE) {
        uint8_t copy[32];
        memcpy(copy, deferred_data, sizeof copy);
        if (op == DEFER_END) HandleEndUpdate(copy);
        else HandleConfirm(copy);
        deferred_op = DEFER_NONE;     // cleared last: the interrupt keeps ignoring writes while the step runs
    }
    if (reset_due && (uint32_t)(HAL_GetTick() - reset_due_tick) >= RESET_PAUSE_MS) {
        reset_due = 0;
        NVIC_SystemReset();
    }
}

uint8_t ListenWindowExpired(uint32_t start, uint32_t now, uint32_t window_ms) {
    if (update_in_progress || DeferredBusy()) return 0;
    return (uint32_t)(now - start) >= window_ms ? 1u : 0u;
}
