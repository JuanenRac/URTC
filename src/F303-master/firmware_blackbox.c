// =============================================================================
// URTC Firmware - "Black box" fault recorder
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Planned feature: "Implementar un sistema de Black Box en la F-RAM para
// registrar los últimos 10s antes de un error". A continuously-updated RAM
// ring buffer (BLACKBOX_SAMPLES x ~150ms =~ 10s, matching the idea's own
// number rather than a round one picked arbitrarily), flushed to F-RAM
// ONLY on the rising edge of system_error_flag - never every tick, for
// 2 real reasons:
//   1. The whole point is capturing what led UP TO a fault. A fault
//      condition that stays true for a while (system_error_flag doesn't
//      auto-clear anywhere in this firmware) must not let every
//      subsequent tick overwrite the pre-fault window with nothing-but-
//      already-faulted samples - one flush per fault occurrence, latched.
//   2. F-RAM endurance is effectively unlimited (same reasoning
//      SavedState_MaybeSave's own header comment gives for the FM24CL64B
//      chip this board already uses) so write-wear isn't the real
//      concern - but a flush is still a real, non-trivial I2C
//      transaction (roughly 680 bytes = 85 8-byte page-ish writes,
//      depending on the driver's own chunking), not something to do
//      every 150ms for no reason when nothing is actually wrong.
//
// READBACK: CAN readback (0x1A8 request / 0x1A9 response, wired into
// firmware_can_global_pre.c right after the existing 0x1A6/0x1A7 sensor-
// variant query) - see BlackBox_HandleReadbackRequest()'s own doc comment
// below for the chunked-transfer protocol. The capture mechanism above
// stays independently valuable and inspectable via ST-Link/a F-RAM dump
// tool even without this, and remains unchanged by it - the readback is a
// pure export path, it never mutates the ring buffer or the flushed
// record.
// =============================================================================
#include "firmware_common.h"
#include "firmware_blackbox.h"
#include "firmware_fram.h"

static BlackBoxSample_t ring[BLACKBOX_SAMPLES];
static uint8_t write_index = 0;
static uint8_t sample_count = 0;
static uint8_t already_flushed_for_this_fault = 0; // latched false->true on flush, reset back to 0 once system_error_flag itself clears (if it ever does) - see the edge-detection below

// Same CRC-8/SMBUS algorithm as SavedState_Checksum (firmware_persistence.c)
// - kept as its own small copy rather than changing that function's
// signature to be generic, to avoid touching already-working, unrelated
// code for this feature's sake.
static uint8_t BlackBox_Checksum(const BlackBoxRecord_t *r) {
    const uint8_t *bytes = (const uint8_t *)r;
    uint8_t crc = 0x00;
    for (size_t i = 0; i < sizeof(BlackBoxRecord_t) - 1; i++) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static void BlackBox_Flush(void) {
    BlackBoxRecord_t r;
    r.magic = BLACKBOX_MAGIC;
    r.struct_version = BLACKBOX_VERSION;
    r.sample_count = sample_count;
    r.write_index = write_index;
    memcpy(r.samples, ring, sizeof(ring));
    r.checksum = BlackBox_Checksum(&r);
    FRAM_WriteBytes(BLACKBOX_FRAM_ADDR, (const uint8_t *)&r, sizeof(r));
    // No retry-on-failure the way SavedState_MaybeSave has (it can just
    // wait for its own next periodic call) - a black box flush is a
    // one-shot reaction to a fault that already happened, not something
    // with a "next call" guaranteed to come back around to the same
    // pre-fault window if this one attempt fails (F-RAM missing/
    // unresponsive). Best-effort is the honest ceiling here.
}

void BlackBox_Sample(void) {
    static uint8_t prev_error_flag = 0;

    BlackBoxSample_t *s = &ring[write_index];
    s->tick_ms = HAL_GetTick();
    s->active_tool = (uint8_t)active_tool;
    s->current_temperature = current_temperature;
    s->target_temperature = target_temperature;
    s->flags = (uint8_t)((endstop_triggered ? 0x01 : 0) | (system_error_flag ? 0x02 : 0));

    write_index = (write_index + 1) % BLACKBOX_SAMPLES;
    if (sample_count < BLACKBOX_SAMPLES) sample_count++;

    // Rising-edge detection - flush exactly once per fault occurrence,
    // not on every tick system_error_flag happens to still read true.
    if (system_error_flag && !prev_error_flag && !already_flushed_for_this_fault) {
        BlackBox_Flush();
        already_flushed_for_this_fault = 1;
    }
    if (!system_error_flag) {
        already_flushed_for_this_fault = 0; // re-armed for the next real fault, if system_error_flag is ever cleared (most set-sites in this firmware are one-way today, but this doesn't assume that stays true forever)
    }
    prev_error_flag = system_error_flag;
}

// 32-byte chunks answered as 4 consecutive 8-byte CAN frames on the
// response ID, concatenated in the order sent - the exact same convention
// firmware_can_thermalinspection.c's own SendChunkFrames() already
// established for its own oversized (32-byte) MLX9064x pixel chunks. Kept
// as its own small copy here rather than exporting that static function,
// same reasoning as this file's own BlackBox_Checksum() above.
#define BLACKBOX_CHUNK_BYTES 32
// Integer constant expression (sizeof is compile-time), so this is a real
// compile-time bound, not a runtime computation - ceil(678 / 32) = 22 for
// the current BlackBoxRecord_t.
enum { BLACKBOX_CHUNK_COUNT = (sizeof(BlackBoxRecord_t) + BLACKBOX_CHUNK_BYTES - 1) / BLACKBOX_CHUNK_BYTES };

static void BlackBox_SendChunkFrames(const uint8_t *data32) {
    for (uint8_t frame = 0; frame < 4; frame++) {
        if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) return; // stop rather than send an out-of-order remainder if mailboxes run out mid-transfer - a host seeing fewer than 4 frames for this chunk already knows to treat it as incomplete, same as it would a dropped CAN frame from any other cause
        CAN_TxHeaderTypeDef txH;
        txH.StdId = 0x1A9;
        txH.IDE = CAN_ID_STD;
        txH.RTR = CAN_RTR_DATA;
        txH.DLC = 8;
        txH.TransmitGlobalTime = DISABLE;
        uint32_t mb;
        HAL_CAN_AddTxMessage(&hcan, &txH, (uint8_t *)&data32[frame * 8], &mb);
    }
}

// Answers one 32-byte slice of the flushed BlackBoxRecord_t sitting in
// F-RAM at BLACKBOX_FRAM_ADDR - a raw, byte-for-byte export, not a
// validated read: this firmware does no magic/checksum/version check on
// the way out, exactly like reading the F-RAM chip directly with an
// external tool would give you. A host reconstructs BlackBoxRecord_t from
// the concatenated chunks itself and is free to validate `magic` and
// `checksum` (BlackBox_Checksum's own CRC-8/SMBUS, same algorithm) before
// trusting the samples.
//
// [chunk_index] out of BLACKBOX_CHUNK_COUNT range, or the F-RAM read
// itself failing (chip missing/unresponsive), both mean no response at
// all - a host can't tell "no fault ever flushed yet" apart from "F-RAM
// hardware fault" from silence alone, but a dedicated status query isn't
// worth adding for a diagnostic-only command already independently
// verifiable via a direct F-RAM dump (see this file's own header
// comment). The last chunk is short (678 bytes is not a multiple of 32);
// the unused tail of `chunk` is left zeroed rather than reading past the
// record into whatever F-RAM holds next.
void BlackBox_HandleReadbackRequest(uint8_t chunk_index) {
    if (chunk_index >= BLACKBOX_CHUNK_COUNT) return;
    uint8_t chunk[BLACKBOX_CHUNK_BYTES] = {0};
    uint16_t offset = (uint16_t)chunk_index * BLACKBOX_CHUNK_BYTES;
    uint16_t remaining = (uint16_t)sizeof(BlackBoxRecord_t) - offset;
    uint16_t real_bytes = remaining < BLACKBOX_CHUNK_BYTES ? remaining : BLACKBOX_CHUNK_BYTES;
    if (!FRAM_ReadBytes(BLACKBOX_FRAM_ADDR + offset, chunk, real_bytes)) return;
    BlackBox_SendChunkFrames(chunk);
}
