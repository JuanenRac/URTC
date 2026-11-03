// =============================================================================
// URTC Bootloader - CAN protocol handlers, application validation, and jump
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "stm32f3xx_hal.h"
#include <string.h>
#include "bootloader_common.h"
#include "bootloader_protocol.h"
#include "bootloader_crypto.h"
#include "bootloader_flash.h"
#include "bootloader_oled.h"
#include "bootloader_trust.h"
#include "urtc_auth.h"
#include "urtc_version_wire.h"

static uint8_t CAN_WaitForFreeMailbox(void) {
    // 50 attempts x 1ms = 50ms max - a small fraction of the ~800ms IWDG
    // window (prescaler 32, reload 999, LSI ~40kHz), but 10x more room
    // than the previous 5ms for transient bus congestion to clear before
    // a status frame gets dropped. HAL_IWDG_Refresh every iteration keeps
    // this safely clear of the watchdog regardless of how many attempts
    // it takes.
    for (uint8_t attempt = 0; attempt < 50; attempt++) {
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) return 1;
        HAL_IWDG_Refresh(&hiwdg);
        HAL_Delay(1);
    }
    return HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0;
}

void CAN_SendStatus(uint8_t status) {
    if (!CAN_WaitForFreeMailbox()) return; // genuinely no mailbox freed up in time - drop rather than block indefinitely
    CAN_TxHeaderTypeDef txHeader;
    uint8_t txData[1] = {status};
    uint32_t mailbox;
    txHeader.StdId = CAN_ID_STATUS;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = 1;
    txHeader.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txHeader, txData, &mailbox);
}

static void CAN_SendVerifyFailReason(uint8_t reason) {
    // Same CAN_ID_STATUS as CAN_SendStatus, but DLC=2 instead of 1: byte[0]
    // is still STATUS_VERIFY_FAIL (0x05) so anything that only reads
    // byte[0] keeps working exactly as before, byte[1] adds the specific
    // VERIFY_FAIL_REASON_* this particular failure was.
    if (!CAN_WaitForFreeMailbox()) return;
    CAN_TxHeaderTypeDef txHeader;
    uint8_t txData[2] = {STATUS_VERIFY_FAIL, reason};
    uint32_t mailbox;
    txHeader.StdId = CAN_ID_STATUS;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = 2;
    txHeader.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txHeader, txData, &mailbox);
}

static void CAN_SendPageAck(uint32_t page_index) {
    if (!CAN_WaitForFreeMailbox()) return; // genuinely no mailbox freed up in time - drop rather than block indefinitely
    CAN_TxHeaderTypeDef txHeader;
    uint8_t txData[4];
    txData[0] = (uint8_t)((page_index >> 24) & 0xFFu);
    txData[1] = (uint8_t)((page_index >> 16) & 0xFFu);
    txData[2] = (uint8_t)((page_index >> 8) & 0xFFu);
    txData[3] = (uint8_t)(page_index & 0xFFu);
    uint32_t mailbox;
    txHeader.StdId = CAN_ID_PAGE_ACK;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = 4;
    txHeader.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txHeader, txData, &mailbox);
}

void CAN_SendHeartbeat(uint8_t status, uint8_t progress_percent) {
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) return;
    CAN_TxHeaderTypeDef txHeader;
    uint8_t txData[2] = {status, progress_percent};
    uint32_t mailbox;
    txHeader.StdId = CAN_ID_HEARTBEAT;
    txHeader.IDE = CAN_ID_STD;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.DLC = 2;
    txHeader.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txHeader, txData, &mailbox);
}

// Version query (0x7F8) response, sent as 0x7F9 - shared between both
// listening loops in main() below. byte0=1 marks this as the bootloader
// answering (as opposed to the application answering directly, byte0=0 -
// see the application's own 0x7F8 handler), reporting whatever the
// metadata page currently says about the main slot: a real HardwareID and
// version if something valid is installed, or all-zero HardwareID/version
// if Metadata_Read finds nothing valid - the host can tell those two
// cases apart by the all-zero HardwareID, which THIS_HARDWARE_ID never is.
void HandleVersionQuery(void) {
    FirmwareMetadata_t meta;
    uint8_t have_meta = Metadata_Read(&meta);
    uint32_t hw_id = have_meta ? meta.hardware_id : 0;
    uint16_t ver_major = have_meta ? (uint16_t)meta.version_major : 0;
    uint8_t ver_minor = have_meta ? (uint8_t)meta.version_minor : 0;

    CAN_TxHeaderTypeDef txH;
    uint8_t txD[8];
    txD[0] = 0x01; // answering as the bootloader
    txD[1] = (uint8_t)(hw_id >> 24);
    txD[2] = (uint8_t)(hw_id >> 16);
    txD[3] = (uint8_t)(hw_id >> 8);
    txD[4] = (uint8_t)(hw_id);
    txD[5] = (uint8_t)(ver_major >> 8);
    txD[6] = (uint8_t)(ver_major);
    txD[7] = ver_minor;
    txH.StdId = CAN_ID_VERSION_RESPONSE;
    txH.IDE = CAN_ID_STD;
    txH.RTR = CAN_RTR_DATA;
    txH.DLC = 8;
    txH.TransmitGlobalTime = DISABLE;
    if (CAN_WaitForFreeMailbox()) {
        uint32_t mb;
        HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
    }

    // Companion frame with the installed application PATCH number and this
    // bootloader's capability flags. Additive: 0x7F9 above is unchanged, so a
    // host that does not know this ID simply ignores it, and a host that never
    // sees it knows the board predates the patch extension.
    if (CAN_WaitForFreeMailbox()) {
        CAN_TxHeaderTypeDef txH3;
        uint8_t txD3[URTC_VERSION_PATCH_FRAME_DLC];
        urtc_version_patch_frame_build(txD3, 0x01,
            have_meta ? urtc_minor_field_patch(meta.version_minor) : URTC_PATCH_UNKNOWN,
            URTC_PATCH_FLAG_END_UPDATE_PATCH);
        txH3.StdId = URTC_CAN_ID_VERSION_PATCH;
        txH3.IDE = CAN_ID_STD;
        txH3.RTR = CAN_RTR_DATA;
        txH3.DLC = URTC_VERSION_PATCH_FRAME_DLC;
        txH3.TransmitGlobalTime = DISABLE;
        uint32_t mb3;
        HAL_CAN_AddTxMessage(&hcan, &txH3, txD3, &mb3);
    }

    // This bootloader's own version, sent right alongside the app
    // metadata above - a separate frame since 0x7F9's 8 bytes were
    // already fully used by the app metadata, and conflating "what
    // version is this bootloader" with "what app is installed" would
    // have made 0x7F9 answer two different questions depending on
    // context. Sent unconditionally whenever this handler runs (i.e.
    // whenever the bootloader itself is the one answering) - never sent
    // by the application, which has no way to know a currently-flashed
    // bootloader's version other than this.
    if (CAN_WaitForFreeMailbox()) {
        CAN_TxHeaderTypeDef txH2;
        uint8_t txD2[3] = {BOOTLOADER_VERSION_MAJOR, BOOTLOADER_VERSION_MINOR, BOOTLOADER_VERSION_PATCH};
        txH2.StdId = CAN_ID_BOOTLOADER_VERSION_RESPONSE;
        txH2.IDE = CAN_ID_STD;
        txH2.RTR = CAN_RTR_DATA;
        txH2.DLC = 3;
        txH2.TransmitGlobalTime = DISABLE;
        uint32_t mb2;
        HAL_CAN_AddTxMessage(&hcan, &txH2, txD2, &mb2);
    }

    // The identifier of the key this bootloader trusts (the current one), so that a flashing tool can compare it with the key that signed the image before it
    // sends anything. A bootloader that trusts nobody sends nothing: the tool reads "no data", never a difference.
    const TrustKeys_t *trust = Trust_Active();
    if (trust->count > 0u && CAN_WaitForFreeMailbox()) {
        CAN_TxHeaderTypeDef txH4;
        txH4.StdId = CAN_ID_KEY_ID;
        txH4.IDE = CAN_ID_STD;
        txH4.RTR = CAN_RTR_DATA;
        txH4.DLC = TRUST_KEY_ID_SIZE;
        txH4.TransmitGlobalTime = DISABLE;
        uint8_t txD4[TRUST_KEY_ID_SIZE];
        memcpy(txD4, trust->key_id[0], sizeof(txD4));
        uint32_t mb4;
        HAL_CAN_AddTxMessage(&hcan, &txH4, txD4, &mb4);
    }
}

// Error counter query (0x7FB) response, sent as 0x7FC - shared between
// both listening loops below, same dual-answerable convention as
// HandleVersionQuery above. TEC/REC (Transmit/Receive Error Counter) are
// read directly from the CAN peripheral's own ESR register (REC in bits
// 31:24, TEC in bits 23:16 - RM0316) rather than tracked in software,
// since the peripheral already maintains these per the CAN bus protocol's
// own fault-confinement rules. Useful for telling "the bus itself has a
// wiring/termination/bitrate problem" (climbing TEC/REC) apart from "this
// bootloader's own logic is stuck" during a slow or failed update.
void HandleErrorCounterQuery(void) {
    uint32_t esr = hcan.Instance->ESR;
    uint8_t tec = (uint8_t)((esr >> 16) & 0xFF);
    uint8_t rec = (uint8_t)((esr >> 24) & 0xFF);
    if (!CAN_WaitForFreeMailbox()) return;
    CAN_TxHeaderTypeDef txH;
    uint8_t txD[2] = {tec, rec};
    uint32_t mb;
    txH.StdId = CAN_ID_ERROR_COUNTERS_RESPONSE;
    txH.IDE = CAN_ID_STD;
    txH.RTR = CAN_RTR_DATA;
    txH.DLC = 2;
    txH.TransmitGlobalTime = DISABLE;
    HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
}

// -----------------------------------------------------------------------
// Validates the MAIN slot and, if a copy from backup was left unfinished
// by an interrupted previous session, resumes and completes it before
// re-checking. Returns 1 if the main slot ends up holding a verified,
// jumpable application; 0 otherwise (bootloader stays in listening mode).
// -----------------------------------------------------------------------
// This chip has two distinct RAM regions a valid stack pointer could
// legitimately sit in - not just the 40KB standard SRAM at 0x20000000,
// but also an 8KB CCM (Core-Coupled Memory) region at 0x10000000,
// confirmed against ST's own datasheet (DS9118) for the STM32F303xB/xC
// family. CCM is CPU-only (no DMA access) and mapped on both instruction
// and data bus - a firmware built to run its most timing-critical code
// from there (or simply place its stack there) is using this chip
// exactly as ST documents it supporting, not doing anything unusual.
//
// Start addresses come from CMSIS (SRAM_BASE/CCMDATARAM_BASE) rather
// than hardcoded literals. The sizes stay as literals - verified neither
// SRAM_SIZE nor CCMDATARAM_SIZE exists in this chip family's CMSIS
// headers, so there's no macro to derive them from.
//
// The inclusive upper bound (<=, not <) on each range is intentional,
// not an off-by-one: this project's own linker script defines
// _estack = ORIGIN(RAM) + LENGTH(RAM), i.e. exactly SRAM_BASE + 40K -
// the standard ARM Cortex-M "empty descending stack" convention, where
// the reset vector table's initial MSP value legitimately points one
// address past the last usable byte (nothing is stored there; the first
// push decrements before writing). A stricter "<" here would reject the
// exact initial SP value of any correctly-linked application using this
// project's own linker script.
static uint8_t StackPointerInValidRAM(uint32_t sp) {
    // Lower bound leaves headroom for at least a few PUSHes before the
    // very base of RAM - a stack pointer sitting exactly at the base
    // would fault on the very first PUSH (which decrements before
    // writing), so this rejects that edge case rather than treating it
    // as a plausible, valid application.
    uint8_t in_sram = (sp >= SRAM_BASE + 0x100UL && sp <= SRAM_BASE + 0xA000UL);
    uint8_t in_ccm  = (sp >= CCMDATARAM_BASE + 0x100UL && sp <= CCMDATARAM_BASE + 0x2000UL);
    return in_sram || in_ccm;
}

uint8_t ApplicationIsValid(void) {
    FirmwareMetadata_t meta;
    if (!Metadata_Read(&meta)) {
        // No valid metadata yet - either a genuinely blank chip, or the
        // main slot was just JTAG-flashed directly (which writes the app
        // region itself but has no way to also populate this separate
        // metadata page). Distinguish the two with a plausibility check on
        // the initial stack pointer: genuinely erased flash reads as
        // 0xFFFFFFFF, not a valid RAM address. A real vector table (from
        // JTAG, or adopted here before) has a real MSP inside this chip's
        // actual 48KB RAM. If it passes, compute its CRC/SHA-256 right now and
        // adopt it as the trusted baseline.
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
        adopted.counter = Trust_MinCounter();
        sha256_flash_region(MAIN_APP_ADDR, APP_MAX_SIZE, adopted.sha256);
        return Metadata_EraseAndWrite(&adopted) ? 1 : 0;
        // The adopted digest is only a baseline for the damage check at
        // every start; a NEW incoming image is judged by its signature.
    }

    // Validated before anything below reads meta.size or meta.hardware_id,
    // including the COPY_PENDING resume branch just below - the metadata
    // page's own fields are written in ascending address order
    // (Flash_WriteVerified), and only magic is checked by Metadata_Read, so
    // a power loss between the magic+state halfwords committing and the
    // hardware_id/size halfwords committing leaves state==COPY_PENDING with
    // hardware_id/size still reading as erased flash (0xFFFFFFFF). Gating
    // here means that torn-write case is caught before Flash_CopyRegion
    // ever sees an unbounded size, rather than after.
    if (meta.hardware_id != THIS_HARDWARE_ID) return 0;
    if (meta.size == 0 || meta.size > APP_MAX_SIZE) return 0;

    if (meta.state == META_STATE_COPY_PENDING) {
        // Previous session was interrupted mid-copy. Backup was never
        // touched during that copy, so it's still exactly what it was when
        // it passed full verification - just resume the copy from it.
        CAN_SendStatus(STATUS_COPYING);
        if (!Flash_CopyRegion(MAIN_APP_ADDR, BACKUP_APP_ADDR, meta.size)) {
            return 0; // stay in bootloader; can retry again next boot
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

    uint8_t actual_digest[32];
    sha256_flash_region(MAIN_APP_ADDR, meta.size, actual_digest);
    if (!digest_constant_time_compare(actual_digest, meta.sha256, 32)) return 0;

    return 1;
}

void JumpToApplication(void) {
    typedef void (*pFunction)(void);
    uint32_t app_stack = *(volatile uint32_t*)MAIN_APP_ADDR;
    uint32_t app_reset_vector = *(volatile uint32_t*)(MAIN_APP_ADDR + 4);

    // Sanity check runs before any peripheral teardown below - a failure
    // here returns to the caller with CAN still alive to fall back to
    // listening with, rather than having already disabled the CAN
    // interrupt, de-initialized CAN, reset the clock config, and
    // de-initialized HAL first. Four separate conditions, each catching a
    // different way a corrupted or wrong-slot image could otherwise still
    // pass a naive check: the stack pointer must sit in one of this
    // chip's two real RAM regions (40KB SRAM at 0x20000000, or 8KB CCM at
    // 0x10000000 - see StackPointerInValidRAM's own note on why both are
    // legitimate), the stack pointer must be
    // contiguous SRAM (0x20000000-0x2000A000), the stack pointer must be
    // 4-byte aligned per the AAPCS calling convention Cortex-M requires,
    // the reset vector's LSB must be 1 (Thumb state - Cortex-M cannot
    // execute ARM-mode instructions at all, so a 0 here means an
    // instant UsageFault on entry), and the reset vector itself must
    // actually point inside the application slot rather than anywhere
    // else a corrupted vector table might claim.
    if (!StackPointerInValidRAM(app_stack) ||
        (app_stack & 0x3UL) != 0 ||
        (app_reset_vector & 0x1UL) == 0 ||
        app_reset_vector < MAIN_APP_ADDR ||
        app_reset_vector >= MAIN_APP_ADDR + APP_MAX_SIZE) {
        return;
    }

    HAL_NVIC_DisableIRQ(CAN_RX0_IRQn);
    HAL_CAN_DeInit(&hcan);
    HAL_I2C_DeInit(&hi2c2);
    HAL_RCC_DeInit();
    HAL_DeInit();
    // Same safe-state pattern already used in HardFault_Handler - not
    // covering an actual collision in this project's current pin usage
    // (the application reconfigures I2C1/CAN on these same pins for the
    // same purpose regardless of what state they're in), but cheap
    // insurance if a future firmware revision ever repurposes them.
    // 0xEBFFFFFF preserves PA13/PA14 (SWD).
    GPIOA->MODER = 0xEBFFFFFF;
    GPIOB->MODER = 0xFFFFFFFF;
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    // If SysTick happened to reach 0 right before the disable above, the
    // resulting pending interrupt survives it - CTRL=0 stops the counter,
    // it doesn't clear an already-latched pending request in the NVIC.
    // Left uncleared, that request fires the instant the application
    // calls __enable_irq(), potentially before its own SysTick_Handler's
    // dependencies (HAL_Init(), etc.) are ready for it.
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;

    // Defensive: the app already relocates its own vector table as the
    // first line of main() (before HAL_Init()), and every interrupt
    // source used here (CAN RX, SysTick) is already disabled above, so
    // this line isn't covering an active gap - it's cheap insurance
    // against a future refactor reordering the app's own VTOR write.
    __disable_irq();
    SCB->VTOR = MAIN_APP_ADDR;
    __DSB(); // Data Synchronization Barrier - the VTOR write must complete before anything below relies on it
    __ISB(); // Instruction Synchronization Barrier - flushes the pipeline so nothing prefetched against the old vector table runs

    pFunction app_entry = (pFunction)app_reset_vector;
    __set_MSP(app_stack);
    app_entry();
}

// -----------------------------------------------------------------------
// Update flow - all writes during an update go to the BACKUP slot only.
// The main slot is never touched until backup has fully passed size, CRC32,
// Ed25519 signature, and HardwareID verification.
// -----------------------------------------------------------------------
uint32_t update_total_size = 0;
static uint32_t update_declared_hw_id = 0;
uint32_t update_bytes_received = 0;
static uint32_t update_running_crc = 0xFFFFFFFFUL;
// The 76-byte record (key id, counter, signature) arrives in 11 chunks of 7 bytes; the last chunk carries 6 and a pad byte.
#define SIG_RECORD_SIZE   76u
#define SIG_CHUNK_BYTES   7u
#define SIG_CHUNK_COUNT   11u
#define SIG_ALL_CHUNKS    0x7FFu
static uint8_t  update_record[SIG_CHUNK_COUNT * SIG_CHUNK_BYTES];
static uint16_t update_record_chunks = 0;
static uint8_t  page_buffer[FLASH_PAGE_SIZE];
static uint32_t page_buffer_fill = 0;
static uint32_t current_page_index = 0;
uint8_t  update_in_progress = 0;
uint32_t update_last_activity_tick = 0;
uint8_t  update_failed = 0;
// Set only by HandleAuthorizeDowngrade's own magic payload, reset to 0 at
// the start of every fresh HandleStartUpdate (0x7F1) and consumed
// unconditionally the moment HandleEndUpdate's own anti-rollback check
// runs - one-shot per update attempt, so an authorization can never
// silently carry over into a later, unrelated attempt. See
// HandleAuthorizeDowngrade's own comment for why this exists at all.
static uint8_t update_downgrade_authorized = 0;

static uint8_t FlushPageBuffer(void) {
    uint32_t page_addr = BACKUP_APP_ADDR + (current_page_index * FLASH_PAGE_SIZE);
    for (uint32_t i = page_buffer_fill; i < FLASH_PAGE_SIZE; i++) {
        page_buffer[i] = 0xFF;
    }
    uint8_t ok = Flash_WriteVerified(page_addr, page_buffer, FLASH_PAGE_SIZE);
    if (ok) {
        CAN_SendPageAck(current_page_index);
        current_page_index++;
        page_buffer_fill = 0;
        HAL_IWDG_Refresh(&hiwdg);
        // update_total_size > 0 is already guaranteed by this point (only
        // reachable after HandleStartUpdate validated it), but checking
        // explicitly here too costs nothing and keeps that safety visible
        // locally rather than relying on a precondition from elsewhere.
        uint8_t pct = (update_total_size > 0) ?
            (uint8_t)(((uint64_t)update_bytes_received * 100) / update_total_size) : 0;
        OLED_DrawProgressBar_Boot(pct);
    }
    return ok;
}

// The 28 bytes of image header that the signature covers, rebuilt from the frames of this update (src/urtc_common/image_header.py is the reference): the
// hardware id and the length of START_UPDATE, the CRC and the version of END_UPDATE. The patch is the one of the minor field, or 0 when the flasher
// did not send it; the fourth version component (the build) is 0 until the end frame can carry it.
static void BuildImageHeader(uint8_t out[URTC_AUTH_HEADER_BYTES], uint16_t major, uint16_t minor_field, uint32_t crc) {
    uint8_t patch = urtc_minor_field_patch(minor_field);
    memset(out, 0, URTC_AUTH_HEADER_BYTES);
    out[0] = 'U'; out[1] = 'R'; out[2] = 'I'; out[3] = 'H';
    out[4] = 1;   // header version
    out[5] = 32;  // header size
    out[8] = (uint8_t)(update_declared_hw_id >> 24); out[9] = (uint8_t)(update_declared_hw_id >> 16);
    out[10] = (uint8_t)(update_declared_hw_id >> 8); out[11] = (uint8_t)update_declared_hw_id;
    out[12] = (uint8_t)(major >> 8); out[13] = (uint8_t)major;
    out[14] = urtc_minor_field_minor(minor_field);
    out[15] = (uint8_t)((patch == URTC_PATCH_UNKNOWN) ? 0u : patch);
    out[20] = (uint8_t)(update_total_size >> 24); out[21] = (uint8_t)(update_total_size >> 16);
    out[22] = (uint8_t)(update_total_size >> 8); out[23] = (uint8_t)update_total_size;
    out[24] = (uint8_t)(crc >> 24); out[25] = (uint8_t)(crc >> 16); out[26] = (uint8_t)(crc >> 8); out[27] = (uint8_t)crc;
}

void HandleStartUpdate(uint8_t *data) {
    // Parsed into locals and validated BEFORE touching update_total_size /
    // update_declared_hw_id below - those two are the same shared globals
    // HandleData/CAN_SendHeartbeat's progress math read on every frame of
    // an update that might already be in progress. Writing them first and
    // validating after (the previous order here) meant a malformed or
    // out-of-order START_UPDATE arriving mid-transfer - a duplicate/retried
    // frame, a bit-flip on the bus, a confused host - would overwrite
    // update_total_size with a rejected value while leaving
    // update_in_progress, update_bytes_received, page_buffer_fill, and
    // current_page_index exactly as they were from the good update already
    // underway: an early return here left the state machine internally
    // inconsistent (an in-progress transfer with a size that no longer
    // matches what was actually erased/allocated for it) rather than either
    // cleanly continuing the original update or cleanly aborting it.
    uint32_t new_total_size = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                             | ((uint32_t)data[2] << 8) | data[3];
    uint32_t new_hw_id = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                        | ((uint32_t)data[6] << 8) | data[7];

    if (new_total_size == 0 || new_total_size > APP_MAX_SIZE) {
        CAN_SendStatus(STATUS_ERROR);
        return;
    }
    if (new_hw_id != THIS_HARDWARE_ID) {
        // Rejected before touching flash at all - an image built for
        // different hardware doesn't even get a chance to erase anything.
        // Uses the same reason-coded path as the other verification
        // failures below (not the generic STATUS_ERROR) so the flasher has
        // one consistent way to learn exactly why an update was rejected.
        CAN_SendVerifyFailReason(VERIFY_FAIL_REASON_HARDWARE_ID);
        return;
    }

    update_total_size = new_total_size;
    update_declared_hw_id = new_hw_id;

    OLED_ClearAll_Boot();
    OLED_PrintStr2x_Boot(0, 16, "UPDATING");
    OLED_DrawProgressBar_Boot(0);

    uint32_t pages_needed = (update_total_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    CAN_SendStatus(STATUS_ERASING);
    if (!Flash_ErasePages(BACKUP_APP_ADDR, pages_needed)) {
        CAN_SendStatus(STATUS_ERROR);
        update_failed = 1;
        update_in_progress = 0; // explicit for clarity - every consumer of
                                 // these two flags already checks
                                 // update_failed too, so this wasn't
                                 // actually reachable as a live bug
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    memset(page_buffer, 0xFF, sizeof(page_buffer)); // clears any residual
    // content from an earlier, aborted attempt - not reachable as a live
    // bug either (the fill logic always writes valid data up to
    // page_buffer_fill regardless), but keeps stale bytes out of RAM.
    update_bytes_received = 0;
    update_running_crc = 0xFFFFFFFFUL;
    memset(update_record, 0, sizeof(update_record));
    update_record_chunks = 0;
    page_buffer_fill = 0;
    current_page_index = 0;
    update_in_progress = 1;
    update_failed = 0;
    update_downgrade_authorized = 0; // every fresh update attempt starts unauthorized - see this flag's own declaration comment
    update_last_activity_tick = HAL_GetTick();
    CAN_SendStatus(STATUS_RECEIVING);
}

// Authorizes the CURRENT update attempt to install a version older than
// what's already running, bypassing HandleEndUpdate's own anti-rollback
// check just this once. Magic payload, same reasoning as
// CAN_ID_ENTER_BOOTLOADER/CAN_ID_ERASE_FRAM's own magic payloads - a
// deliberate downgrade is a real, occasionally-needed operation (reverting
// a bad release), but it must never happen by accident the way a bare
// "allow older version" flag with no confirmation could. Only meaningful
// between HandleStartUpdate and HandleEndUpdate for the SAME attempt - see
// update_downgrade_authorized's own declaration comment for the full
// one-shot reasoning.
uint8_t DowngradeIsAuthorized(void) {
    return update_downgrade_authorized;
}

void HandleAuthorizeDowngrade(uint8_t *data) {
    if (data[0] == 0xD0 && data[1] == 0x9E && data[2] == 0x12 && data[3] == 0xAD) {
        update_downgrade_authorized = 1;
    }
}

// Blocks waiting for the host's own page-ack (CAN_ID_READBACK_PAGE_ACK)
// confirming it safely received and stored the page just sent, refreshing
// the watchdog throughout - same 3s-per-page window and refresh-while-
// waiting reasoning as the write direction's own page-ACK wait. Returns 0
// on timeout (host stopped listening, or the bus dropped every copy of
// the ack) or a wrong page index (host desynced somehow) - either way the
// caller abandons the transfer rather than sending more data nobody
// confirmed wanting.
static uint8_t WaitForReadbackPageAck(uint32_t page_index) {
    uint32_t wait_start = HAL_GetTick();
    while ((HAL_GetTick() - wait_start) < 3000) { // same unsigned-subtraction wraparound safety as every other tick comparison in this file
        HAL_IWDG_Refresh(&hiwdg);
        if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0) {
            CAN_RxHeaderTypeDef rxH;
            uint8_t rxD[8];
            if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxH, rxD) == HAL_OK) {
                if (rxH.RTR == CAN_RTR_DATA && rxH.IDE == CAN_ID_STD &&
                    rxH.StdId == CAN_ID_READBACK_PAGE_ACK && rxH.DLC == 4) {
                    uint32_t acked = ((uint32_t)rxD[0] << 24) | ((uint32_t)rxD[1] << 16)
                                    | ((uint32_t)rxD[2] << 8) | rxD[3];
                    if (acked == page_index) return 1;
                }
            }
        }
    }
    return 0;
}

// Reads the current MAIN slot back over CAN, unmodified - the CAN
// equivalent of the Flasher's own "back up entire flash before erasing"
// SWD feature, for the same reason: a real copy of what's currently
// installed, worth having before deliberately overwriting it (a normal
// update, or a CAN_ID_AUTHORIZE_DOWNGRADE-authorized one). Read-only -
// never touches flash itself, so it's safe to call any time the
// bootloader is idle. Ignored while an update is already in progress,
// rather than interleaving with it.
void HandleReadbackStart(void) {
    if (update_in_progress) return;

    FirmwareMetadata_t meta;
    uint32_t total_size = 0;
    // Same guard as ApplicationIsValid() below - Metadata_Read() only
    // checks `magic`, so a torn-write metadata page (power loss between
    // the state halfword and the hardware_id/size halfwords committing,
    // same scenario documented there) could otherwise leave state reading
    // as META_STATE_APP_VALID with hardware_id/size still at erased-flash
    // 0xFFFFFFFF. Without this check, total_size below would come out
    // unbounded and the page loop further down would memcpy past the end
    // of real flash - reachable by any node on the bus, unauthenticated.
    if (Metadata_Read(&meta) && meta.state == META_STATE_APP_VALID &&
        meta.hardware_id == THIS_HARDWARE_ID &&
        meta.size > 0 && meta.size <= APP_MAX_SIZE) {
        total_size = meta.size;
    }

    // First reply is always DLC=4 (the total byte count, 0 if there's
    // nothing valid installed to back up) - every reply after this one is
    // DLC=8 raw data, so the host tells them apart by DLC alone, no
    // separate "here's how many bytes are coming" side-channel needed.
    if (!CAN_WaitForFreeMailbox()) return;
    {
        CAN_TxHeaderTypeDef txH;
        uint8_t txD[4] = {
            (uint8_t)(total_size >> 24), (uint8_t)(total_size >> 16),
            (uint8_t)(total_size >> 8), (uint8_t)(total_size)
        };
        uint32_t mb;
        txH.StdId = CAN_ID_READBACK;
        txH.IDE = CAN_ID_STD;
        txH.RTR = CAN_RTR_DATA;
        txH.DLC = 4;
        txH.TransmitGlobalTime = DISABLE;
        HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
    }
    if (total_size == 0) return;

    uint32_t num_pages = (total_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    uint32_t offset = 0;
    for (uint32_t p = 0; p < num_pages; p++) {
        uint32_t page_len = FLASH_PAGE_SIZE;
        if (offset + page_len > total_size) page_len = total_size - offset;

        for (uint32_t i = 0; i < page_len; i += 8) {
            uint8_t chunk_len = (uint8_t)((page_len - i < 8) ? (page_len - i) : 8);
            // CAN_WaitForFreeMailbox's own 50ms-of-retries-then-give-up
            // already provides real backpressure here if the host (or the
            // bus) can't keep up - no separate manual pacing delay needed
            // between frames the way the Flasher's own upload direction
            // adds one, since that pacing is about not overrunning the
            // BOOTLOADER's own receive/flash-write pace, which doesn't
            // apply to a read that never touches flash.
            if (!CAN_WaitForFreeMailbox()) return;
            CAN_TxHeaderTypeDef txH;
            uint8_t txD[8] = {0};
            memcpy(txD, (const void*)(MAIN_APP_ADDR + offset + i), chunk_len);
            uint32_t mb;
            txH.StdId = CAN_ID_READBACK;
            txH.IDE = CAN_ID_STD;
            txH.RTR = CAN_RTR_DATA;
            txH.DLC = chunk_len;
            txH.TransmitGlobalTime = DISABLE;
            HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
        }
        offset += page_len;

        if (!WaitForReadbackPageAck(p)) return;
        OLED_DrawProgressBar_Boot((uint8_t)(((uint64_t)(p + 1) * 100) / num_pages));
    }
}

void HandleSigChunk(uint8_t *data) {
    if (!update_in_progress || update_failed) return;
    uint8_t index = data[0];
    if (index >= SIG_CHUNK_COUNT) return;
    memcpy(&update_record[index * SIG_CHUNK_BYTES], data + 1, SIG_CHUNK_BYTES);
    update_record_chunks = (uint16_t)(update_record_chunks | (1u << index));
    update_last_activity_tick = HAL_GetTick();
}

void HandleData(uint8_t *data, uint32_t dlc) {
    if (!update_in_progress || update_failed) return;
    // Classic CAN hardware already caps DLC at 8, but this costs nothing
    // and closes off a theoretical out-of-bounds read of the caller's
    // 8-byte rxData buffer if that ever stops being true (a future
    // CAN-FD migration, or a corrupted header field).
    if (dlc > 8) return;
    if (update_bytes_received < update_total_size) {
        update_last_activity_tick = HAL_GetTick();
    }

    uint32_t i;
    for (i = 0; i < dlc; i++) {
        if (update_bytes_received >= update_total_size) break;
        page_buffer[page_buffer_fill++] = data[i];
        update_bytes_received++;

        if (page_buffer_fill == FLASH_PAGE_SIZE) {
            if (!FlushPageBuffer()) {
                update_running_crc = CRC32_Update(update_running_crc, data, i + 1);
                CAN_SendStatus(STATUS_ERROR);
                update_in_progress = 0;
                update_failed = 1;
                OLED_ClearAll_Boot();
                OLED_PrintStr2x_Boot(2, 4, "ERROR");
                return;
            }
        }
    }
    update_running_crc = CRC32_Update(update_running_crc, data, i);
}

void HandleEndUpdate(uint8_t *data) {
    if (!update_in_progress || update_failed) return;
    update_last_activity_tick = HAL_GetTick();

    if (page_buffer_fill > 0) {
        if (!FlushPageBuffer()) {
            CAN_SendStatus(STATUS_ERROR);
            update_in_progress = 0;
            update_failed = 1;
            OLED_ClearAll_Boot();
            OLED_PrintStr2x_Boot(2, 4, "ERROR");
            return;
        }
    }

    if (update_bytes_received != update_total_size || update_record_chunks != SIG_ALL_CHUNKS) {
        CAN_SendVerifyFailReason(VERIFY_FAIL_REASON_INCOMPLETE);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    uint32_t expected_crc = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16)
                           | ((uint32_t)data[2] << 8) | data[3];
    uint16_t version_major = ((uint16_t)data[4] << 8) | data[5];
    uint16_t version_minor = ((uint16_t)data[6] << 8) | data[7];
    uint32_t actual_crc = CRC32_Finalize(update_running_crc);

    CAN_SendStatus(STATUS_VERIFYING);
    OLED_ClearAll_Boot();
    OLED_PrintStr2x_Boot(0, 16, "UPDATING");

    if (actual_crc != expected_crc) {
        CAN_SendVerifyFailReason(VERIFY_FAIL_REASON_CRC32);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    // Authentication: the Ed25519 record must verify, under a key of the trust table, over the header that the frames of this update describe, the SHA-256
    // of the image in the backup slot and the rollback counter (urtc_auth.h). Nothing is believed before it does. A table with no key (the official build
    // until the image-signing key exists) trusts nobody, so nothing installs.
    uint8_t actual_digest[32];
    sha256_flash_region(BACKUP_APP_ADDR, update_total_size, actual_digest);
    uint8_t header28[URTC_AUTH_HEADER_BYTES];
    BuildImageHeader(header28, version_major, version_minor, actual_crc);

    FirmwareMetadata_t current_meta;
    uint8_t have_current = Metadata_Read(&current_meta);
    uint32_t counter_floor = Trust_MinCounter();
    if (have_current && current_meta.counter > counter_floor) counter_floor = current_meta.counter;

    const TrustKeys_t *trust = Trust_Active();
    urtc_auth_context_t auth;
    memset(&auth, 0, sizeof(auth));
    auth.policy = URTC_AUTH_DEFAULT_POLICY;
    auth.keys = (const uint8_t (*)[URTC_ED25519_PUBLIC_KEY_SIZE])trust->keys;
    auth.key_count = trust->count;
    auth.hardware_id = THIS_HARDWARE_ID;
    auth.installed_counter = counter_floor;
    // The version rule is applied below with the comparison that tolerates a legacy flasher (no patch number); urtc_auth_decide does the rest.
    auth.installed_version_known = 0;
    urtc_auth_image_t candidate;
    candidate.header28 = header28;
    candidate.payload_sha256 = actual_digest;
    candidate.record = update_record;
    candidate.hmac_ok = 0;
    uint32_t new_counter = counter_floor;
    urtc_auth_status_t verdict = urtc_auth_decide(&auth, &candidate, &new_counter);
    if (verdict != URTC_AUTH_OK) {
        uint8_t reason = VERIFY_FAIL_REASON_SIGNATURE;
        if (verdict == URTC_AUTH_HARDWARE_MISMATCH) reason = VERIFY_FAIL_REASON_HARDWARE_ID;
        if (verdict == URTC_AUTH_ROLLBACK_COUNTER) reason = VERIFY_FAIL_REASON_ROLLBACK;
        // CRC32 passed but the record did not: the image isn't corrupt, but nobody the bootloader trusts vouched for it. The main slot was never touched.
        CAN_SendVerifyFailReason(reason);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    // Anti-rollback by version: a validly signed image is still rejected if it declares a version older than what's already running, UNLESS this exact
    // attempt was explicitly authorized via HandleAuthorizeDowngrade (0x7FD's own magic payload). The signed counter above is never overridable. Only
    // enforced when there's an existing valid version to roll back from - a blank board or one recovering from corrupted metadata has nothing to compare
    // against. The version is part of what the record signs, so it cannot be changed without breaking the signature.
    if (have_current && current_meta.state == META_STATE_APP_VALID) {
        // The minor field may carry the PATCH number in its high byte (see
        // urtc_version_wire.h); the patch only takes part when both sides know it.
        if (urtc_version_is_older(version_major, version_minor,
                                  current_meta.version_major, current_meta.version_minor)
            && !update_downgrade_authorized) {
            CAN_SendVerifyFailReason(VERIFY_FAIL_REASON_ROLLBACK);
            update_in_progress = 0;
            update_failed = 1;
            OLED_ClearAll_Boot();
            OLED_PrintStr2x_Boot(2, 4, "ERROR");
            return;
        }
    }
    // Consumed here regardless of whether the block above actually needed
    // it (new_version could have been >= current_version, making the flag
    // moot for this attempt) - one-shot per attempt either way, per this
    // flag's own declaration comment.
    update_downgrade_authorized = 0;

    // Backup slot is now fully verified: size matched, CRC32 matched, the
    // signature verified, HardwareID was checked before a single byte was written.
    // Record the copy as pending BEFORE touching the main slot - if power
    // is lost partway through the copy below, the next boot's
    // ApplicationIsValid() finds this state and resumes from backup
    // (untouched by any of this) rather than treating a partially-copied
    // main slot as something to try to run.
    FirmwareMetadata_t pending = {0};
    pending.magic = METADATA_MAGIC_VALID;
    pending.state = META_STATE_COPY_PENDING;
    pending.hardware_id = update_declared_hw_id;
    pending.version_major = version_major;
    pending.version_minor = version_minor;
    pending.size = update_total_size;
    pending.crc32 = actual_crc;
    pending.counter = new_counter;
    memcpy(pending.sha256, actual_digest, 32);
    if (!Metadata_EraseAndWrite(&pending)) {
        CAN_SendStatus(STATUS_ERROR);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    CAN_SendStatus(STATUS_COPYING);
    OLED_ClearAll_Boot();
    OLED_PrintStr2x_Boot(0, 4, "UPDATING");
    OLED_DrawProgressBar_Boot(0);
    if (!Flash_CopyRegion(MAIN_APP_ADDR, BACKUP_APP_ADDR, update_total_size)) {
        // Main slot may be partially written at this point, but that's
        // fine - metadata still says META_STATE_COPY_PENDING, backup is
        // still fully intact, and the next boot (or another attempt right
        // now, if we retried in a loop) resumes the copy from it.
        CAN_SendStatus(STATUS_ERROR);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }
    OLED_DrawProgressBar_Boot(100);

    FirmwareMetadata_t done = pending;
    done.state = META_STATE_APP_VALID;
    if (!Metadata_EraseAndWrite(&done)) {
        CAN_SendStatus(STATUS_ERROR);
        update_in_progress = 0;
        update_failed = 1;
        OLED_ClearAll_Boot();
        OLED_PrintStr2x_Boot(2, 4, "ERROR");
        return;
    }

    OLED_ClearAll_Boot();
    OLED_PrintStr2x_Boot(2, 16, "FLASH OK");
    CAN_SendStatus(STATUS_VERIFY_OK);
    HAL_Delay(600); // let the success message actually be seen before resetting
    NVIC_SystemReset();
}
