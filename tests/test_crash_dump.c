// =============================================================================
// URTC Firmware - Host logic tests for the crash capture record (crash_dump.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The record that a HardFault leaves in RAM: its CRC-32 against the usual test value, the 48 bytes on the bus against bytes written by hand (from a model
// made with struct and zlib, not from this code), the capture of a stacked frame (with and without a readable stack), the start-up rules (kept after a
// watchdog reset, emptied after a power-on or when the RAM holds anything else) and the property that no single flipped bit of a valid record is believed.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crash_dump.h"
#include "test_runner.h"

// What the model gives for the capture of the frame below: counter 1, PC 0x08002468, LR 0x08001235, xPSR 0x61000000, CFSR 0x8200, HFSR 0x40000000,
// MMFAR 0xDEADBEEF, BFAR 0x40021000, frame at 0x20009F00, reset cause 0, kind 1, version 1, then the CRC-32.
static const uint8_t EXPECTED_CAPTURE[CRASH_DUMP_BYTES] = {
    0x43,0x52,0x53,0x48,0x01,0x00,0x00,0x00,0x68,0x24,0x00,0x08,0x35,0x12,0x00,0x08,0x00,0x00,0x00,0x61,0x00,0x82,0x00,0x00,0x00,0x00,0x00,0x40,0xEF,0xBE,0xAD,0xDE,
    0x00,0x10,0x02,0x40,0x00,0x9F,0x00,0x20,0x00,0x01,0x01,0x00,0x22,0x41,0xAB,0x97};
// The empty record: magic, zeros, version 1.
static const uint8_t EXPECTED_EMPTY[CRASH_DUMP_BYTES] = {
    0x43,0x52,0x53,0x48,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0xA6,0x68,0x36,0x75};

static const uint32_t FRAME[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x08001235u, 0x08002468u, 0x61000000u};

static void capture_example(CrashDumpRecord_t *rec)
{
    CrashDump_Capture(rec, FRAME, 0x20009F00u, 0x8200u, 0x40000000u, 0xDEADBEEFu, 0x40021000u);
}

static void bytes_of(const CrashDumpRecord_t *rec, uint8_t out[CRASH_DUMP_BYTES])
{
    CrashDump_Serialize(rec, out);
}

static void run_crc_and_layout(int *failures)
{
    TEST_ASSERT(sizeof(CrashDumpRecord_t) == 48 && CRASH_DUMP_BYTES == 48 && CRASH_DUMP_FRAMES * 8 == CRASH_DUMP_BYTES, "the record is 48 bytes, six frames of eight");
    TEST_ASSERT(CrashDump_Crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u, "the CRC-32 of the usual test string");
    TEST_ASSERT(CrashDump_Crc32((const uint8_t *)"", 0) == 0, "the CRC-32 of nothing is zero");
    uint8_t zeros[4] = {0, 0, 0, 0};
    TEST_ASSERT(CrashDump_Crc32(zeros, 4) == 0x2144DF1Cu, "the CRC-32 of four zero bytes");

    CrashDumpRecord_t rec;
    CrashDump_Clear(&rec);
    uint8_t out[CRASH_DUMP_BYTES];
    bytes_of(&rec, out);
    TEST_ASSERT(memcmp(out, EXPECTED_EMPTY, sizeof(out)) == 0, "the empty record is the bytes the model gives");
    TEST_ASSERT(CrashDump_IsValid(&rec) && rec.kind == CRASH_KIND_NONE && rec.seq == 0, "the empty record is valid and says no crash");

    capture_example(&rec);
    bytes_of(&rec, out);
    TEST_ASSERT(memcmp(out, EXPECTED_CAPTURE, sizeof(out)) == 0, "the capture of a frame is the bytes the model gives, little-endian, whatever the machine");
    TEST_ASSERT(CrashDump_IsValid(&rec), "a captured record is valid");
    TEST_ASSERT(rec.pc == 0x08002468u && rec.lr == 0x08001235u && rec.xpsr == 0x61000000u, "PC, LR and xPSR come from slots 6, 5 and 7 of the stacked frame");
    TEST_ASSERT(rec.cfsr == 0x8200u && (rec.cfsr & CRASH_CFSR_PRECISERR) && (rec.cfsr & CRASH_CFSR_BFARVALID), "the fault status registers are kept as read");
    TEST_ASSERT(rec.hfsr == 0x40000000u && (rec.hfsr & CRASH_HFSR_FORCED), "the hard fault status is kept as read");
    TEST_ASSERT(rec.mmfar == 0xDEADBEEFu && rec.bfar == 0x40021000u && rec.sp == 0x20009F00u, "the fault addresses and the frame address are kept");
}

static void run_capture_rules(int *failures)
{
    CrashDumpRecord_t rec;
    CrashDump_Clear(&rec);
    capture_example(&rec);
    TEST_ASSERT(rec.seq == 1 && rec.kind == CRASH_KIND_FAULT, "the first crash is number 1, with its frame");

    // a second crash in a row (the board reset and faulted again) goes on counting
    CrashDump_Boot(&rec, 0x01);
    capture_example(&rec);
    TEST_ASSERT(rec.seq == 2 && CrashDump_IsValid(&rec), "a crash after a reset that kept the record is number 2");

    // a stack that was not inside RAM: only the registers of the fault are real
    CrashDump_Capture(&rec, NULL, 0x00000010u, 0x20000u, 0x40000000u, 0, 0);
    TEST_ASSERT(rec.kind == CRASH_KIND_FAULT_NO_FRAME && rec.pc == 0 && rec.lr == 0 && rec.xpsr == 0 && rec.cfsr == 0x20000u && rec.seq == 3, "without a readable stack the frame words are zero and the kind says so");
    TEST_ASSERT(CrashDump_IsValid(&rec), "and the record is still valid");

    // a capture over garbage starts at 1 and does not trust the garbage counter
    memset(&rec, 0xA5, sizeof(rec));
    capture_example(&rec);
    TEST_ASSERT(rec.seq == 1 && CrashDump_IsValid(&rec), "a capture over RAM that held anything starts the count at 1");

    // the counter does not wrap to zero
    CrashDump_Clear(&rec);
    rec.seq = 0xFFFFFFFFu;
    CrashDump_Seal(&rec);
    capture_example(&rec);
    TEST_ASSERT(rec.seq == 0xFFFFFFFFu, "the counter stops at its maximum instead of wrapping to 'no crash'");

    // the frame address check
    TEST_ASSERT(CrashDump_FrameInRam(0x20000000u) && CrashDump_FrameInRam(0x20009FE0u) && CrashDump_FrameInRam(0x10000000u) && CrashDump_FrameInRam(0x10001FE0u), "frames inside the SRAM and the CCM RAM are readable");
    TEST_ASSERT(!CrashDump_FrameInRam(0x20009FE4u) && !CrashDump_FrameInRam(0x10001FE4u), "a frame that would run past the end of a RAM is not");
    TEST_ASSERT(!CrashDump_FrameInRam(0) && !CrashDump_FrameInRam(0x08000000u) && !CrashDump_FrameInRam(0x1FFFFFFCu) && !CrashDump_FrameInRam(0x2000A000u) && !CrashDump_FrameInRam(0xFFFFFFFCu), "flash, peripherals and the gaps are not");
    TEST_ASSERT(!CrashDump_FrameInRam(0x20000002u) && !CrashDump_FrameInRam(0x20000001u), "an unaligned stack pointer is not");
}

static void run_boot_rules(int *failures)
{
    CrashDumpRecord_t rec;

    // a watchdog reset after a crash: the record is kept, the cause is written into it, the capture is still there
    CrashDump_Clear(&rec);
    capture_example(&rec);
    CrashDump_Boot(&rec, 0x01);
    TEST_ASSERT(CrashDump_IsValid(&rec) && rec.reset_cause == 0x01 && rec.seq == 1 && rec.kind == CRASH_KIND_FAULT && rec.pc == 0x08002468u, "after a watchdog reset the capture is kept and the cause is written into it");

    // a software or pin reset keeps it too
    CrashDump_Boot(&rec, 0x14);
    TEST_ASSERT(rec.seq == 1 && rec.reset_cause == 0x14 && CrashDump_IsValid(&rec), "after a software reset with the pin flag the capture is kept");

    // a power-on empties it, with or without the other flags
    CrashDump_Boot(&rec, CRASH_RESET_POWER_ON);
    TEST_ASSERT(CrashDump_IsValid(&rec) && rec.seq == 0 && rec.kind == CRASH_KIND_NONE && rec.pc == 0 && rec.reset_cause == CRASH_RESET_POWER_ON, "a power-on empties the record");
    capture_example(&rec);
    CrashDump_Boot(&rec, (uint8_t)(CRASH_RESET_POWER_ON | 0x10));
    TEST_ASSERT(rec.seq == 0 && rec.kind == CRASH_KIND_NONE, "a power-on with the pin flag empties it as well");

    // RAM that holds anything
    memset(&rec, 0xFF, sizeof(rec));
    CrashDump_Boot(&rec, 0x01);
    TEST_ASSERT(CrashDump_IsValid(&rec) && rec.seq == 0 && rec.kind == CRASH_KIND_NONE && rec.reset_cause == 0x01, "RAM full of ones is not believed: the record is emptied and still says this reset");
    memset(&rec, 0, sizeof(rec));
    CrashDump_Boot(&rec, 0x04);
    TEST_ASSERT(CrashDump_IsValid(&rec) && rec.seq == 0, "RAM full of zeros is not believed either");

    // the record of a firmware with another layout version is not believed
    CrashDump_Clear(&rec);
    capture_example(&rec);
    rec.version = 2;
    CrashDump_Seal(&rec);
    TEST_ASSERT(!CrashDump_IsValid(&rec), "a record of another version is not valid, even with a good CRC");
    CrashDump_Boot(&rec, 0x01);
    TEST_ASSERT(rec.seq == 0 && rec.version == CRASH_DUMP_VERSION, "and it is emptied at start-up");

    // a kind that does not exist
    CrashDump_Clear(&rec);
    rec.kind = 9;
    CrashDump_Seal(&rec);
    TEST_ASSERT(!CrashDump_IsValid(&rec), "a kind that is not defined is not valid");
}

static void run_every_bit(int *failures)
{
    CrashDumpRecord_t good;
    CrashDump_Clear(&good);
    capture_example(&good);
    uint8_t wire[CRASH_DUMP_BYTES];
    CrashDump_Serialize(&good, wire);

    // Flip each of the 384 bits of the 48 bytes in the RAM image: none of them may leave a record that is believed and different from the original.
    int believed = 0, same = 0;
    for (size_t byte = 0; byte < sizeof(good); byte++) {
        for (int bit = 0; bit < 8; bit++) {
            CrashDumpRecord_t rec = good;
            ((uint8_t *)&rec)[byte] ^= (uint8_t)(1u << bit);
            if (CrashDump_IsValid(&rec)) {
                uint8_t now[CRASH_DUMP_BYTES];
                CrashDump_Serialize(&rec, now);
                if (memcmp(now, wire, sizeof(wire)) == 0) same++; else believed++;
            }
        }
    }
    TEST_ASSERT(believed == 0, "no single flipped bit of a valid record gives a different record that is still believed");
    TEST_ASSERT(same == 0, "there is no padding in the layout: every bit that flips changes the record");
}

static void run_fuzz(int *failures)
{
    // fixed seed: a xorshift stream fills the record with anything; whatever is believed must serialise to a frame whose own CRC agrees
    uint32_t state = 0x1234ABCDu;
    int believed = 0;
    for (int round = 0; round < 20000; round++) {
        CrashDumpRecord_t rec;
        uint8_t *p = (uint8_t *)&rec;
        for (size_t i = 0; i < sizeof(rec); i++) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            p[i] = (uint8_t)state;
        }
        if (round % 3 == 0) { rec.magic = CRASH_DUMP_MAGIC; rec.version = CRASH_DUMP_VERSION; rec.kind &= 3u; if (rec.kind == 3u) rec.kind = 2u; }
        if (round % 6 == 0) CrashDump_Seal(&rec);
        if (CrashDump_IsValid(&rec)) {
            believed++;
            uint8_t wire[CRASH_DUMP_BYTES];
            CrashDump_Serialize(&rec, wire);
            uint32_t crc = (uint32_t)wire[44] | ((uint32_t)wire[45] << 8) | ((uint32_t)wire[46] << 16) | ((uint32_t)wire[47] << 24);
            if (CrashDump_Crc32(wire, 44) != crc) { TEST_ASSERT(0, "a believed record has a frame whose CRC agrees"); break; }
        }
        CrashDump_Boot(&rec, (uint8_t)state);
        if (!CrashDump_IsValid(&rec)) { TEST_ASSERT(0, "start-up always leaves a valid record"); break; }
    }
    TEST_ASSERT(believed > 1000 && believed < 20000, "the fuzz reaches both the believed and the refused records");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - crash capture record\n");
    run_crc_and_layout(&failures);
    run_capture_rules(&failures);
    run_boot_rules(&failures);
    run_every_bit(&failures);
    run_fuzz(&failures);
    if (failures == 0) {
        printf("  OK   all crash capture host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
