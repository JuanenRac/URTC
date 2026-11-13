// =============================================================================
// URTC Firmware - Host logic tests for urtc_meta_ab.h (metadata in two alternating pages)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The flash is modelled with its rules: a programmed bit cannot go back to 1 except by erasing the page, programming is by half-words, and the power can
// be cut at any half-word or in the middle of an erase. The sweep cuts the power at EVERY step of a write, with every way the interrupted step can leave the
// flash, and checks that the state that was there (or the new one) can always be read and that the next write recovers.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "urtc_meta_ab.h"

#define PAGE_BYTES 2048u
#define PAYLOAD 80u // the metadata (64 bytes) and the programmed serial (16 bytes)

typedef struct {
    uint8_t page[2][PAGE_BYTES];
    int step;            // primitive steps done (every half-word and every erase)
    int cut_at;          // the step at which the power goes (-1 = never)
    int cut_mode;        // how the interrupted step leaves the flash: 0 untouched, 1 completed, 2 garbled
    int cut;             // the power is gone: nothing is written any more
    int erases;
    int fail_erase;      // an erase that reports failure
    uint32_t seed;
} Flash_t;

static uint32_t next_random(Flash_t *f) { f->seed = f->seed * 1664525u + 1013904223u; return f->seed >> 8; }

static void flash_init(Flash_t *f) { memset(f, 0xFF, sizeof f->page); f->step = 0; f->cut_at = -1; f->cut_mode = 0; f->cut = 0; f->erases = 0; f->fail_erase = 0; f->seed = 12345u; }

static const uint8_t *m_page_ptr(void *ctx, uint8_t page) { return ((Flash_t *)ctx)->page[page]; }

static uint8_t m_erase(void *ctx, uint8_t page)
{
    Flash_t *f = ctx;
    if (f->cut) return 0;
    if (f->fail_erase) return 0;
    if (f->cut_at >= 0 && f->step == f->cut_at) {
        f->cut = 1;
        if (f->cut_mode == 1) memset(f->page[page], 0xFF, PAGE_BYTES);
        else if (f->cut_mode == 2) for (uint32_t i = 0; i < PAGE_BYTES; i++) f->page[page][i] = (uint8_t)next_random(f);
        return 0;
    }
    f->step++;
    f->erases++;
    memset(f->page[page], 0xFF, PAGE_BYTES);
    return 1;
}

static uint8_t m_program(void *ctx, uint8_t page, uint32_t offset, const uint8_t *data, uint32_t len)
{
    Flash_t *f = ctx;
    for (uint32_t i = 0; i < len; i += 2) {
        if (f->cut) return 0;
        const uint8_t lo = data[i], hi = (i + 1 < len) ? data[i + 1] : 0xFF;
        if (f->cut_at >= 0 && f->step == f->cut_at) {
            f->cut = 1;
            if (f->cut_mode == 1) { f->page[page][offset + i] &= lo; f->page[page][offset + i + 1] &= hi; }
            else if (f->cut_mode == 2) { f->page[page][offset + i] &= (uint8_t)(lo | next_random(f)); f->page[page][offset + i + 1] &= (uint8_t)(hi | next_random(f)); }
            return 0;
        }
        f->step++;
        f->page[page][offset + i] &= lo; // a programmed bit does not go back to 1
        f->page[page][offset + i + 1] &= hi;
    }
    return 1;
}

static UrtcMetaAbOps ops_for(Flash_t *f) { UrtcMetaAbOps o = { m_page_ptr, m_erase, m_program, f }; return o; }

static void fill(uint8_t *payload, uint8_t seed) { for (uint32_t i = 0; i < PAYLOAD; i++) payload[i] = (uint8_t)(seed * 31u + i); }

static int steps_of_one_write(void)
{
    Flash_t f; flash_init(&f);
    UrtcMetaAbOps o = ops_for(&f);
    uint8_t p[PAYLOAD]; fill(p, 1);
    urtc_meta_ab_write(&o, PAYLOAD, p);
    return f.step;
}

static void test_basics(int *failures)
{
    Flash_t f; flash_init(&f);
    UrtcMetaAbOps o = ops_for(&f);
    uint8_t out[PAYLOAD], a[PAYLOAD], b[PAYLOAD];
    fill(a, 1); fill(b, 2);
    TEST_ASSERT(!urtc_meta_ab_read(&o, PAYLOAD, out), "a blank flash holds no record");
    TEST_ASSERT(urtc_meta_ab_write(&o, PAYLOAD, a) && urtc_meta_ab_read(&o, PAYLOAD, out) && memcmp(out, a, PAYLOAD) == 0, "the first write reads back");
    uint32_t gen = 0;
    TEST_ASSERT(urtc_meta_ab_newest(&o, PAYLOAD, &gen) == 0 && gen == 1, "the first record is in page A with generation 1");
    const int erases = f.erases;
    TEST_ASSERT(urtc_meta_ab_write(&o, PAYLOAD, a) && f.erases == erases, "the same state is not written again");
    TEST_ASSERT(urtc_meta_ab_write(&o, PAYLOAD, b) && urtc_meta_ab_newest(&o, PAYLOAD, &gen) == 1 && gen == 2, "a new state goes to the other page with the next generation");
    TEST_ASSERT(urtc_meta_ab_read(&o, PAYLOAD, out) && memcmp(out, b, PAYLOAD) == 0, "and is the one that is read");
    TEST_ASSERT(urtc_meta_ab_write(&o, PAYLOAD, a) && urtc_meta_ab_newest(&o, PAYLOAD, &gen) == 0 && gen == 3, "the pages alternate");
    // a flipped bit in the newest record: the older one is read
    f.page[0][URTC_META_AB_HEADER + 5] ^= 0x01;
    TEST_ASSERT(urtc_meta_ab_read(&o, PAYLOAD, out) && memcmp(out, b, PAYLOAD) == 0, "a record with a wrong CRC is ignored and the other page is read");
    TEST_ASSERT(!urtc_meta_ab_write(&o, 0, a) && !urtc_meta_ab_write(&o, URTC_META_AB_MAX_PAYLOAD + 1u, a), "a payload of no length or too long is refused");
    // a failed erase leaves the previous record in force
    flash_init(&f);
    o = ops_for(&f);
    urtc_meta_ab_write(&o, PAYLOAD, a);
    f.fail_erase = 1;
    TEST_ASSERT(!urtc_meta_ab_write(&o, PAYLOAD, b) && urtc_meta_ab_read(&o, PAYLOAD, out) && memcmp(out, a, PAYLOAD) == 0, "an erase that fails keeps the previous state");
}

static void test_power_cut_sweep(int *failures)
{
    const int total = steps_of_one_write();
    TEST_ASSERT(total == 1 + (8 + PAYLOAD) / 2 + 2, "a write is one erase, the record and the magic");
    int bad_state = 0, bad_recovery = 0, none_lost = 0, cases = 0;
    for (int before = 0; before <= 5; before++) {                    // how many records the flash holds before the cut write
        for (int mode = 0; mode < 3; mode++) {
            for (int cut = 0; cut < total; cut++) {
                Flash_t f; flash_init(&f);
                UrtcMetaAbOps o = ops_for(&f);
                uint8_t last[PAYLOAD], next[PAYLOAD], out[PAYLOAD], again[PAYLOAD];
                for (int i = 1; i <= before; i++) { fill(last, (uint8_t)i); urtc_meta_ab_write(&o, PAYLOAD, last); }
                fill(next, 100);
                f.step = 0; f.cut_at = cut; f.cut_mode = mode; f.seed = 7u + (uint32_t)(cut * 3 + mode);
                urtc_meta_ab_write(&o, PAYLOAD, next);
                cases++;
                // after the cut: the power is back
                f.cut_at = -1; f.cut = 0;
                const int has = urtc_meta_ab_read(&o, PAYLOAD, out);
                if (before > 0 && !has) none_lost++;
                if (has && !(memcmp(out, next, PAYLOAD) == 0 || (before > 0 && memcmp(out, last, PAYLOAD) == 0))) bad_state++;
                if (!has && before > 0) bad_state++;
                // the next write recovers whatever the cut left
                fill(again, 200);
                if (!urtc_meta_ab_write(&o, PAYLOAD, again) || !urtc_meta_ab_read(&o, PAYLOAD, out) || memcmp(out, again, PAYLOAD) != 0) bad_recovery++;
                fill(next, 201);
                if (!urtc_meta_ab_write(&o, PAYLOAD, next) || !urtc_meta_ab_read(&o, PAYLOAD, out) || memcmp(out, next, PAYLOAD) != 0) bad_recovery++;
            }
        }
    }
    TEST_ASSERT(cases == 6 * 3 * total, "the sweep ran every cut with every way of leaving the flash");
    TEST_ASSERT(none_lost == 0, "a cut at any step never leaves the flash without a state once one had been written");
    TEST_ASSERT(bad_state == 0, "and the state that is read is always the one before or the new one, never a mixture");
    TEST_ASSERT(bad_recovery == 0, "the next writes after a cut always succeed and read back");
}

static void put_record(Flash_t *f, uint8_t page, uint32_t generation, const uint8_t *payload)
{
    uint8_t *p = f->page[page];
    memset(p, 0xFF, PAGE_BYTES);
    const uint32_t magic = URTC_META_AB_MAGIC;
    memcpy(p, &magic, 4);
    memcpy(p + 4, &generation, 4);
    memcpy(p + 8, payload, PAYLOAD);
    const uint32_t crc = urtc_meta_ab_crc(p + 4, 4u + PAYLOAD);
    memcpy(p + 8 + PAYLOAD, &crc, 4);
}

static void test_generation_wrap(int *failures)
{
    Flash_t f; flash_init(&f);
    UrtcMetaAbOps o = ops_for(&f);
    uint8_t a[PAYLOAD], b[PAYLOAD], out[PAYLOAD];
    fill(a, 1); fill(b, 2);
    put_record(&f, 0, 0xFFFFFFFDUL, a);
    uint32_t gen = 0;
    int expected[] = { 1, 0, 1, 0 };
    uint8_t p[PAYLOAD];
    for (int i = 0; i < 4; i++) {
        fill(p, (uint8_t)(10 + i));
        TEST_ASSERT(urtc_meta_ab_write(&o, PAYLOAD, p), "a write across the wrap of the generation succeeds");
        TEST_ASSERT(urtc_meta_ab_newest(&o, PAYLOAD, &gen) == expected[i], "and the pages keep alternating");
        TEST_ASSERT(urtc_meta_ab_read(&o, PAYLOAD, out) && memcmp(out, p, PAYLOAD) == 0, "and the newest state is the one read");
    }
    TEST_ASSERT(gen == 3, "the generation skips the two values that mean no record (0 and all ones)");
    // two pages whose generations are one apart across the wrap: the one that comes after wins
    put_record(&f, 0, 0xFFFFFFFEUL, a);
    put_record(&f, 1, 1u, b);
    TEST_ASSERT(urtc_meta_ab_newest(&o, PAYLOAD, &gen) == 1 && gen == 1, "a generation after the wrap is newer than one just before it");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - urtc_meta_ab.h\n");
    test_basics(&failures);
    test_power_cut_sweep(&failures);
    test_generation_wrap(&failures);
    if (failures == 0) {
        printf("  OK   all A/B metadata host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
