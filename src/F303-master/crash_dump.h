// =============================================================================
// URTC Firmware - crash capture (the black box of the HardFault): the record, its checksum and the rules of the capture (pure logic, no HAL)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// When the CPU takes a HardFault the handler first cuts every output (firmware_interrupts_fault.c) and then waits for the independent watchdog to reset
// the chip. Nothing of what the processor knew survived that reset: where it was (PC), who called (LR), why it faulted (CFSR, HFSR, MMFAR, BFAR). With
// URTC_CRASH_DUMP=1 the handler also copies those registers into a record in RAM that is not cleared at start-up (section .noinit), the application
// checks the record after the reset, and the host reads it over CAN (0x1BC asks, six frames of 0x1BD answer).
//
// This file holds only the arithmetic, so the host tests run the same code as the chip: the layout of the record, its CRC-32, the capture of a stacked
// frame, and the start-up check. The record is in RAM, so it is believed only when its magic, its version and its CRC agree; after a power-on it is
// emptied (RAM then holds anything); after any other reset it is kept, with the cause of that reset written into it.
// =============================================================================
#ifndef URTC_CRASH_DUMP_H
#define URTC_CRASH_DUMP_H

#include <stddef.h>
#include <stdint.h>

// The switch. Off: the handler is the one the firmware has always had (cut the outputs, wait for the watchdog), nothing is placed in .noinit and 0x1BC is ignored.
#ifndef URTC_CRASH_DUMP
#define URTC_CRASH_DUMP 0
#endif

#define CRASH_DUMP_MAGIC 0x48535243UL // "CRSH" as bytes in memory
#define CRASH_DUMP_VERSION 1u
#define CRASH_DUMP_BYTES 48u
#define CRASH_DUMP_FRAMES 6u          // frames of 8 bytes on 0x1BD

// What the record says happened.
#define CRASH_KIND_NONE 0u            // no crash since the last power-up
#define CRASH_KIND_FAULT 1u           // a HardFault, with its stacked frame
#define CRASH_KIND_FAULT_NO_FRAME 2u  // a HardFault whose stack pointer was not inside RAM: only the fault registers are real

// Reset-cause bits, the same as byte 5 of 0x111 and byte 1 of 0x1BB.
#define CRASH_RESET_POWER_ON 0x08u

typedef struct {
    uint32_t magic;
    uint32_t seq;      // crashes captured since the last power-up
    uint32_t pc;       // stacked frame: where the faulting instruction was
    uint32_t lr;       // stacked frame: the link register
    uint32_t xpsr;     // stacked frame: the program status register
    uint32_t cfsr;     // SCB->CFSR (memory, bus and usage fault status)
    uint32_t hfsr;     // SCB->HFSR (hard fault status)
    uint32_t mmfar;    // SCB->MMFAR (valid when CFSR bit 7 is set)
    uint32_t bfar;     // SCB->BFAR (valid when CFSR bit 15 is set)
    uint32_t sp;       // address of the stacked frame
    uint8_t reset_cause;
    uint8_t kind;
    uint8_t version;
    uint8_t reserved;
    uint32_t crc;      // CRC-32 of the 44 bytes above
} CrashDumpRecord_t;
_Static_assert(sizeof(CrashDumpRecord_t) == CRASH_DUMP_BYTES, "the crash record is exported over CAN as 48 bytes; a change of layout needs a new CRASH_DUMP_VERSION and the specification");

// CRC-32 (IEEE 802.3, reflected, initial 0xFFFFFFFF, final xor), the usual one: the host computes it with zlib.crc32.
uint32_t CrashDump_Crc32(const uint8_t *data, size_t len);

// The 48 bytes of the record as they go on the bus: every word little-endian, whatever the byte order of the machine.
void CrashDump_Serialize(const CrashDumpRecord_t *rec, uint8_t out[CRASH_DUMP_BYTES]);

// Writes the CRC of the record.
void CrashDump_Seal(CrashDumpRecord_t *rec);

// True when magic, version and CRC agree.
int CrashDump_IsValid(const CrashDumpRecord_t *rec);

// An empty, sealed record: no crash.
void CrashDump_Clear(CrashDumpRecord_t *rec);

// Start-up. The RAM holds either the record of a crash that ended in a reset, or garbage. After a power-on, or when the record fails its checks, it is
// emptied; either way the cause of this reset is written into it and it is sealed again.
void CrashDump_Boot(CrashDumpRecord_t *rec, uint8_t reset_cause);

// Capture, called from the fault handler after the outputs are cut. `frame` points at the eight words the processor stacked (r0, r1, r2, r3, r12, lr, pc,
// xpsr) or is NULL when the stack pointer was not inside RAM (the fault registers are still recorded). `sp` is the address of that frame. Touches only
// the record, calls nothing, allocates nothing, so it is safe in a fault.
void CrashDump_Capture(CrashDumpRecord_t *rec, const uint32_t *frame, uint32_t sp, uint32_t cfsr, uint32_t hfsr, uint32_t mmfar, uint32_t bfar);

// True when `sp` can be the address of a stacked frame: word aligned, with all eight words inside the 40 KB of SRAM or the 8 KB of CCM RAM.
int CrashDump_FrameInRam(uint32_t sp);

// Bits of the status registers that name a cause, for the host and for the tests.
#define CRASH_CFSR_IACCVIOL (1UL << 0)
#define CRASH_CFSR_DACCVIOL (1UL << 1)
#define CRASH_CFSR_MMARVALID (1UL << 7)
#define CRASH_CFSR_IBUSERR (1UL << 8)
#define CRASH_CFSR_PRECISERR (1UL << 9)
#define CRASH_CFSR_BFARVALID (1UL << 15)
#define CRASH_CFSR_UNDEFINSTR (1UL << 16)
#define CRASH_CFSR_INVSTATE (1UL << 17)
#define CRASH_CFSR_DIVBYZERO (1UL << 25)
#define CRASH_HFSR_FORCED (1UL << 30)

// The record kept across resets (section .noinit on the chip). One for the whole firmware.
extern CrashDumpRecord_t crash_dump_store;

#endif // URTC_CRASH_DUMP_H
