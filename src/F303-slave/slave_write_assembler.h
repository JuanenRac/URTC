// =============================================================================
// URTC Expansion Slave Application Firmware - reassembly of register writes on the link bus
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// A register write on the I2C1 link is one transaction: the register number followed by its payload. Whether a transaction that ends early (a STOP before
// the payload is complete) is delivered as one short write or as two pieces depends on how the HAL listens, so the slave must not rely on it. This
// module is pure logic (no HAL): it collects the pieces of a write in order, tells the caller when a write is complete, drops a stale half-written
// command after a timeout (recovery) and counts what it dropped. It is compiled into the link only with URTC_SLAVE_WRITE_REASSEMBLY=1 (default 0:
// the link then behaves exactly as before). A dangerous command (REG_ENTER_BOOTLOADER) is never executed from an incomplete payload.
// =============================================================================
#ifndef SLAVE_WRITE_ASSEMBLER_H
#define SLAVE_WRITE_ASSEMBLER_H

#include <stdint.h>
#include <string.h>

#define SWA_BUFFER_SIZE 8u
#define SWA_STALE_MS_DEFAULT 20u    // a half-written command older than this is dropped; the value can be changed per instance
#define SWA_STALE_MS_MAX 1000u

typedef enum {
    SWA_COMPLETE = 0,   // msg/len hold a complete write, ready to execute
    SWA_PARTIAL = 1,    // the write is shorter than its register needs; waiting for the rest
    SWA_DROPPED = 2     // the bytes could not make a write (overflow or a stale half command); the assembler is empty again
} swa_result_t;

typedef struct {
    uint8_t buf[SWA_BUFFER_SIZE];
    uint8_t fill;
    uint32_t last_tick;
    uint32_t stale_ms;
    uint32_t dropped_stale;
    uint32_t dropped_overflow;
    uint32_t completed;
} slave_write_asm_t;

// Total bytes (register included) of the write of a register, or 0 when the write has no fixed length (a read-pointer selection or a register with an
// optional payload). The numbers are those of slave_common.h: REG_ENTER_BOOTLOADER 5, REG_ADS_CONFIGURE 3, REG_PWM_CONFIGURE 4, REG_PWM_PULSE 4, REG_PWM_STOP 2.
static inline uint8_t swa_expected_len(uint8_t reg)
{
    switch (reg) {
        case 0x02: return 5;   // REG_ENTER_BOOTLOADER: register + the 4 bytes of the magic
        case 0x20: return 3;   // REG_ADS_CONFIGURE
        case 0x30: return 4;   // REG_PWM_CONFIGURE
        case 0x31: return 4;   // REG_PWM_PULSE
        case 0x32: return 2;   // REG_PWM_STOP
        default: return 0;
    }
}

static inline void swa_init(slave_write_asm_t *a, uint32_t stale_ms)
{
    memset(a, 0, sizeof *a);
    a->stale_ms = (stale_ms == 0 || stale_ms > SWA_STALE_MS_MAX) ? SWA_STALE_MS_DEFAULT : stale_ms;
}

static inline void swa_reset(slave_write_asm_t *a)
{
    a->fill = 0;
}

// Adds the bytes the HAL delivered. On SWA_COMPLETE, *out holds the write and *out_len its length; the assembler is empty again. A piece that arrives
// after the stale time restarts the assembly as a new write (the old half command is counted and forgotten, never completed with unrelated bytes).
static inline swa_result_t swa_feed(slave_write_asm_t *a, const uint8_t *data, uint8_t n, uint32_t now, const uint8_t **out, uint8_t *out_len)
{
    if (n == 0) return a->fill ? SWA_PARTIAL : SWA_DROPPED;
    if (a->fill > 0 && (uint32_t)(now - a->last_tick) > a->stale_ms) {
        a->dropped_stale++;
        a->fill = 0;
    }
    if ((uint32_t)a->fill + n > SWA_BUFFER_SIZE) {
        a->dropped_overflow++;
        a->fill = 0;
        return SWA_DROPPED;
    }
    memcpy(&a->buf[a->fill], data, n);
    a->fill = (uint8_t)(a->fill + n);
    a->last_tick = now;

    uint8_t want = swa_expected_len(a->buf[0]);
    if (want == 0 || a->fill == want) {
        *out = a->buf;
        *out_len = a->fill;
        a->fill = 0;     // the caller uses buf before the next feed
        a->completed++;
        return SWA_COMPLETE;
    }
    if (a->fill > want) {   // more bytes than the register takes: not a write that can be trusted
        a->dropped_overflow++;
        a->fill = 0;
        return SWA_DROPPED;
    }
    return SWA_PARTIAL;
}

// True when a half command has been waiting longer than the stale time (the main loop can call it to recover without waiting for the next write).
static inline int swa_expire(slave_write_asm_t *a, uint32_t now)
{
    if (a->fill > 0 && (uint32_t)(now - a->last_tick) > a->stale_ms) {
        a->dropped_stale++;
        a->fill = 0;
        return 1;
    }
    return 0;
}

#endif // SLAVE_WRITE_ASSEMBLER_H
