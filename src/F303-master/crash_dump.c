// =============================================================================
// URTC Firmware - crash capture: the record, its checksum and the rules of the capture (pure logic, no HAL)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include <string.h>

#include "crash_dump.h"

// On the chip the record sits in .noinit: the start-up code does not clear it, so it can outlive the watchdog reset that follows a HardFault. On the
// host (the tests) and with the switch off it is an ordinary zeroed variable.
#if defined(__arm__) && URTC_CRASH_DUMP
CrashDumpRecord_t crash_dump_store __attribute__((section(".noinit")));
#else
CrashDumpRecord_t crash_dump_store;
#endif

uint32_t CrashDump_Crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return ~crc;
}

static void put_u32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)v;
    out[1] = (uint8_t)(v >> 8);
    out[2] = (uint8_t)(v >> 16);
    out[3] = (uint8_t)(v >> 24);
}

// The bytes the CRC covers: the first 44, written word by word so that the result does not depend on the padding or the byte order of the machine.
static void body_bytes(const CrashDumpRecord_t *rec, uint8_t out[CRASH_DUMP_BYTES - 4u])
{
    put_u32(out + 0, rec->magic);
    put_u32(out + 4, rec->seq);
    put_u32(out + 8, rec->pc);
    put_u32(out + 12, rec->lr);
    put_u32(out + 16, rec->xpsr);
    put_u32(out + 20, rec->cfsr);
    put_u32(out + 24, rec->hfsr);
    put_u32(out + 28, rec->mmfar);
    put_u32(out + 32, rec->bfar);
    put_u32(out + 36, rec->sp);
    out[40] = rec->reset_cause;
    out[41] = rec->kind;
    out[42] = rec->version;
    out[43] = rec->reserved;
}

void CrashDump_Serialize(const CrashDumpRecord_t *rec, uint8_t out[CRASH_DUMP_BYTES])
{
    body_bytes(rec, out);
    put_u32(out + CRASH_DUMP_BYTES - 4u, rec->crc);
}

void CrashDump_Seal(CrashDumpRecord_t *rec)
{
    uint8_t body[CRASH_DUMP_BYTES - 4u];
    body_bytes(rec, body);
    rec->crc = CrashDump_Crc32(body, sizeof(body));
}

int CrashDump_IsValid(const CrashDumpRecord_t *rec)
{
    if (rec->magic != CRASH_DUMP_MAGIC || rec->version != CRASH_DUMP_VERSION) return 0;
    if (rec->kind > CRASH_KIND_FAULT_NO_FRAME) return 0;
    uint8_t body[CRASH_DUMP_BYTES - 4u];
    body_bytes(rec, body);
    return CrashDump_Crc32(body, sizeof(body)) == rec->crc;
}

void CrashDump_Clear(CrashDumpRecord_t *rec)
{
    memset(rec, 0, sizeof(*rec));
    rec->magic = CRASH_DUMP_MAGIC;
    rec->version = CRASH_DUMP_VERSION;
    CrashDump_Seal(rec);
}

void CrashDump_Boot(CrashDumpRecord_t *rec, uint8_t reset_cause)
{
    if ((reset_cause & CRASH_RESET_POWER_ON) || !CrashDump_IsValid(rec)) {
        CrashDump_Clear(rec);
    }
    rec->reset_cause = reset_cause;
    CrashDump_Seal(rec);
}

int CrashDump_FrameInRam(uint32_t sp)
{
    if (sp & 3u) return 0;
    if (sp >= 0x20000000u && sp <= 0x20000000u + 40u * 1024u - 32u) return 1;
    if (sp >= 0x10000000u && sp <= 0x10000000u + 8u * 1024u - 32u) return 1;
    return 0;
}

void CrashDump_Capture(CrashDumpRecord_t *rec, const uint32_t *frame, uint32_t sp, uint32_t cfsr, uint32_t hfsr, uint32_t mmfar, uint32_t bfar)
{
    // The counter goes on from the previous valid record (a fault in a loop counts up); anything else starts at 1.
    uint32_t seq = CrashDump_IsValid(rec) ? rec->seq : 0u;
    if (seq != 0xFFFFFFFFu) seq++;
    rec->magic = CRASH_DUMP_MAGIC;
    rec->version = CRASH_DUMP_VERSION;
    rec->reserved = 0;
    rec->seq = seq;
    rec->cfsr = cfsr;
    rec->hfsr = hfsr;
    rec->mmfar = mmfar;
    rec->bfar = bfar;
    rec->sp = sp;
    rec->reset_cause = 0; // written by CrashDump_Boot after the reset this fault ends in
    if (frame != NULL) {
        rec->lr = frame[5];
        rec->pc = frame[6];
        rec->xpsr = frame[7];
        rec->kind = (uint8_t)CRASH_KIND_FAULT;
    } else {
        rec->lr = rec->pc = rec->xpsr = 0;
        rec->kind = (uint8_t)CRASH_KIND_FAULT_NO_FRAME;
    }
    CrashDump_Seal(rec);
}
