// =============================================================================
// URTC Expansion Slave Bootloader - I2C-link protocol handlers, application
// validation, and jump
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// The security-critical logic here (CRC32/HMAC/HardwareID verification,
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

// Defined here, read by the I2C1 interrupt handler's REG_STATUS read path.
volatile uint8_t current_status = STATUS_LISTENING;

void BuildVersionQueryResponse(uint8_t out[10]) {
    FirmwareMetadata_t meta;
    uint8_t have_meta = Metadata_Read(&meta);
    uint32_t hw_id = have_meta ? meta.hardware_id : 0;
    uint16_t ver_major = have_meta ? (uint16_t)meta.version_major : 0;
    uint16_t ver_minor = have_meta ? (uint16_t)meta.version_minor : 0;

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

// -----------------------------------------------------------------------
// Validates the MAIN slot and, if a copy from backup was left unfinished
// by an interrupted previous session, resumes and completes it before
// re-checking. Returns 1 if the main slot ends up holding a verified,
// jumpable application; 0 otherwise (bootloader stays in listening mode).
//
// Same two real RAM regions and same reasoning as the main board's own
// StackPointerInValidRAM - this chip is the same xB/xC density line
// (STM32F303CB), confirmed against the same ST datasheet family (DS9118)
// that already covers the main board's own F303CC: 40KB standard SRAM at
// 0x20000000, 8KB CCM (CPU-only, no DMA) at 0x10000000.
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
        adopted.version_minor = FIRMWARE_VERSION_MINOR;
        adopted.size = APP_MAX_SIZE;
        uint32_t crc = 0xFFFFFFFFUL;
        crc = CRC32_Update(crc, (const uint8_t*)MAIN_APP_ADDR, APP_MAX_SIZE);
        adopted.crc32 = CRC32_Finalize(crc);
        hmac_sha256_flash_region(HMAC_KEY, 32, MAIN_APP_ADDR, APP_MAX_SIZE, adopted.hmac);
        return Metadata_EraseAndWrite(&adopted) ? 1 : 0;
    }

    if (meta.state == META_STATE_COPY_PENDING) {
        current_status = STATUS_COPYING;
        if (!Flash_CopyRegion(MAIN_APP_ADDR, BACKUP_APP_ADDR, meta.size)) {
            return 0;
        }
        FirmwareMetadata_t done = meta;
        done.state = META_STATE_APP_VALID;
        if (!Metadata_EraseAndWrite(&done)) return 0;
        meta = done;
    }

    if (meta.hardware_id != THIS_HARDWARE_ID) return 0;
    if (meta.size == 0 || meta.size > APP_MAX_SIZE) return 0;

    uint32_t crc = 0xFFFFFFFFUL;
    crc = CRC32_Update(crc, (const uint8_t*)MAIN_APP_ADDR, meta.size);
    crc = CRC32_Finalize(crc);
    if (crc != meta.crc32) return 0;

    uint8_t actual_hmac[32];
    hmac_sha256_flash_region(HMAC_KEY, 32, MAIN_APP_ADDR, meta.size, actual_hmac);
    if (!hmac_constant_time_compare(actual_hmac, meta.hmac, 32)) return 0;

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
static uint8_t  update_expected_hmac[32];
static uint8_t  update_hmac_received = 0;
static uint8_t  page_buffer[FLASH_PAGE_SIZE];
static uint32_t page_buffer_fill = 0;
static uint32_t current_page_index = 0;
uint8_t  update_in_progress = 0;
uint8_t  update_failed = 0;

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

void HandleStartUpdate(uint8_t *data) {
    update_total_size = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                       | ((uint32_t)data[2] << 8) | data[3];
    update_declared_hw_id = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                           | ((uint32_t)data[6] << 8) | data[7];

    if (update_total_size == 0 || update_total_size > APP_MAX_SIZE) {
        current_status = STATUS_ERROR;
        return;
    }
    if (update_declared_hw_id != THIS_HARDWARE_ID) {
        current_status = STATUS_VERIFY_FAIL;
        return;
    }

    uint32_t pages_needed = (update_total_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    current_status = STATUS_ERASING;
    if (!Flash_ErasePages(BACKUP_APP_ADDR, pages_needed)) {
        current_status = STATUS_ERROR;
        update_failed = 1;
        update_in_progress = 0;
        return;
    }

    memset(page_buffer, 0xFF, sizeof(page_buffer));
    update_bytes_received = 0;
    update_running_crc = 0xFFFFFFFFUL;
    update_hmac_received = 0;
    page_buffer_fill = 0;
    current_page_index = 0;
    update_in_progress = 1;
    update_failed = 0;
    update_progress_percent = 0;
    current_status = STATUS_RECEIVING;
}

void HandleHmacExpected(uint8_t *data) {
    if (!update_in_progress || update_failed) return;
    memcpy(update_expected_hmac, data, 32); // REG_HMAC_EXPECTED is always exactly 32 bytes in one transaction - no chunking needed the way CAN's 8-byte frames required
    update_hmac_received = 1;
}

void HandleData(uint8_t *data, uint32_t len) {
    if (!update_in_progress || update_failed) return;
    if (len > 32) return; // REG_DATA's own documented ceiling - guards against a malformed/oversized transaction the same way the main board's own DLC>8 check does for CAN

    uint32_t i;
    for (i = 0; i < len; i++) {
        if (update_bytes_received >= update_total_size) break;
        page_buffer[page_buffer_fill++] = data[i];
        update_bytes_received++;

        if (page_buffer_fill == FLASH_PAGE_SIZE) {
            if (!FlushPageBuffer()) {
                update_running_crc = CRC32_Update(update_running_crc, data, i + 1);
                current_status = STATUS_ERROR;
                update_failed = 1;
                return;
            }
        }
    }
    update_running_crc = CRC32_Update(update_running_crc, data, i);
}

void HandleEndUpdate(uint8_t *data) {
    if (!update_in_progress || update_failed) return;

    if (page_buffer_fill > 0) {
        if (!FlushPageBuffer()) {
            current_status = STATUS_ERROR;
            update_failed = 1;
            return;
        }
    }

    if (update_bytes_received != update_total_size || !update_hmac_received) {
        current_status = STATUS_VERIFY_FAIL;
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
        update_in_progress = 0;
        update_failed = 1;
        return;
    }

    uint8_t actual_hmac[32];
    hmac_sha256_flash_region(HMAC_KEY, 32, BACKUP_APP_ADDR, update_total_size, actual_hmac);
    if (!hmac_constant_time_compare(actual_hmac, update_expected_hmac, 32)) {
        current_status = STATUS_VERIFY_FAIL;
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
        uint32_t current_version = (current_meta.version_major << 16) | current_meta.version_minor;
        uint32_t new_version = ((uint32_t)version_major << 16) | version_minor;
        if (new_version < current_version) {
            current_status = STATUS_VERIFY_FAIL;
            update_in_progress = 0;
            update_failed = 1;
            return;
        }
    }

    FirmwareMetadata_t pending = {0};
    pending.magic = METADATA_MAGIC_VALID;
    pending.state = META_STATE_COPY_PENDING;
    pending.hardware_id = update_declared_hw_id;
    pending.version_major = version_major;
    pending.version_minor = version_minor;
    pending.size = update_total_size;
    pending.crc32 = actual_crc;
    memcpy(pending.hmac, actual_hmac, 32);
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
    HAL_Delay(200); // brief pause so a master polling REG_STATUS has a real chance to observe STATUS_VERIFY_OK before the reset below
    NVIC_SystemReset();
}
