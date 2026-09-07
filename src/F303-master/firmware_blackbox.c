// =============================================================================
// URTC Firmware - "Black box" fault recorder
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Audit idea: "Implementar un sistema de Black Box en la F-RAM para
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
// READBACK: not implemented in this pass. A real CAN readback command
// (paged reads, since ~680 bytes is far more than one 8-byte frame -
// the natural next free global-command pair is 0x1A8/0x1A9, right after
// the existing 0x1A6/0x1A7 in firmware_can_global_pre.c) is a real,
// well-defined follow-up, deliberately left for a separate pass rather
// than rushed alongside this capture mechanism - the capture itself is
// independently valuable and inspectable via ST-Link/a F-RAM dump tool
// even without a CAN command yet, and a chunked-transfer protocol
// deserves its own focused review, not to ride along here unverified.
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
