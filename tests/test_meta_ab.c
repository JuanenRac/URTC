// =============================================================================
// URTC Firmware - Host logic tests for urtc_meta_ab.h (metadata in two alternating pages)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The flash is modelled with its rules: a programmed bit cannot go back to 1 except by erasing the page, programming is by half-words, and the power can
// be cut at any half-word or in the middle of an erase. The sweep cuts the power at EVERY step of a write, with every way the interrupted step can leave the
// flash, and checks that the state that was there (or the new one) can always be read and that the next write recovers. The last tests are the move from the
// metadata of one page (an append-only log and two serial slots) to the two pages: the serial and the rollback counter of an installed board must survive it,
// also when the power goes at any step of the first write.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "urtc_meta_ab.h"
#include "urtc_meta_migrate.h"

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

// ---- the move from the single-page layout (urtc_meta_migrate.h) ----------------------------------------------------------------------------------
#define LEGACY_COUNTER_OFFSET 28u // the rollback counter inside the 64 bytes of the metadata

static void legacy_record(uint8_t *page, uint32_t slot, uint32_t generation, uint32_t counter)
{
    uint8_t *r = page + slot * URTC_META_LEGACY_RECORD_SIZE;
    memset(r, 0, URTC_META_LEGACY_RECORD_SIZE);
    memcpy(r, &generation, 4);
    const uint32_t magic = URTC_META_LEGACY_MAGIC, state = 1u, hw = 0x0303CC01UL;
    memcpy(r + 4, &magic, 4);
    memcpy(r + 8, &state, 4);
    memcpy(r + 12, &hw, 4);
    memcpy(r + 4 + LEGACY_COUNTER_OFFSET, &counter, 4);
    r[4 + 36] = (uint8_t)(counter ^ 0x5Au); // a byte of the digest
    const uint32_t crc = urtc_meta_ab_crc(r, URTC_META_LEGACY_RECORD_SIZE - 4u);
    memcpy(r + URTC_META_LEGACY_RECORD_SIZE - 4u, &crc, 4);
}

static void legacy_serial(uint8_t *page, uint32_t slot, uint32_t serial, uint32_t counter)
{
    uint32_t words[4];
    urtc_serial_slot_pack(serial, counter, words);
    memcpy(page + (slot == 0 ? URTC_META_LEGACY_SERIAL_A : URTC_META_LEGACY_SERIAL_B), words, sizeof words);
}

static uint32_t state_counter(const uint8_t *state) { return urtc_meta_ab_load32(state + LEGACY_COUNTER_OFFSET); }

static uint32_t state_serial(const uint8_t *state, uint32_t *serial_counter)
{
    uint32_t words[4];
    for (uint32_t w = 0; w < 4; w++) words[w] = urtc_meta_ab_load32(state + URTC_META_LEGACY_META_BYTES + 4u * w);
    uint32_t serial = 0, counter = 0;
    if (!urtc_serial_pick(words, words, &serial, &counter)) return 0;
    *serial_counter = counter;
    return serial;
}

static void set_counter(uint8_t *state, uint32_t counter) { urtc_meta_state_put32(state + LEGACY_COUNTER_OFFSET, counter); }

// an installed board: three records in the log (rollback counters 100, 101, 102) and a programmed serial 0x11223344 with the counter 5
static void installed_board(Flash_t *f)
{
    flash_init(f);
    legacy_record(f->page[0], 0, 1, 100); legacy_record(f->page[0], 1, 2, 101); legacy_record(f->page[0], 2, 3, 102);
    legacy_serial(f->page[0], 0, 0x11223344u, 5u);
}

static void test_migration_reads(int *failures)
{
    Flash_t f; installed_board(&f);
    UrtcMetaAbOps o = ops_for(&f);
    uint8_t state[URTC_META_STATE_BYTES];
    uint8_t legacy = 0;
    uint32_t serial_counter = 0;
    TEST_ASSERT(urtc_meta_ab_load_state(&o, state, &legacy) && legacy == 1, "a board with the log of one page is read as it is");
    TEST_ASSERT(state_counter(state) == 102u, "the rollback counter is the one of the newest record of the log");
    TEST_ASSERT(state_serial(state, &serial_counter) == 0x11223344u && serial_counter == 5u, "and the programmed serial and its counter are kept");

    // the newest record cut by a power loss (a wrong CRC): the one before it
    f.page[0][2 * URTC_META_LEGACY_RECORD_SIZE + 20] ^= 0x01;
    TEST_ASSERT(urtc_meta_ab_load_state(&o, state, &legacy) && state_counter(state) == 101u, "a record of the log with a wrong CRC is ignored");

    // the newer serial slot wins, a slot without its complement is not a serial
    legacy_serial(f.page[0], 1, 0x55667788u, 9u);
    TEST_ASSERT(urtc_meta_ab_load_state(&o, state, &legacy) && state_serial(state, &serial_counter) == 0x55667788u && serial_counter == 9u, "the newest serial slot is the serial");
    f.page[0][URTC_META_LEGACY_SERIAL_B + 8] ^= 0x01;
    TEST_ASSERT(urtc_meta_ab_load_state(&o, state, &legacy) && state_serial(state, &serial_counter) == 0x11223344u, "a serial slot that is not complete is ignored");

    // an A/B record beats whatever the old log still holds
    uint8_t ab[URTC_META_STATE_BYTES], out[URTC_META_STATE_BYTES];
    fill(ab, 3);
    put_record(&f, 1, 7u, ab);
    TEST_ASSERT(urtc_meta_ab_load_state(&o, out, &legacy) && legacy == 0 && memcmp(out, ab, URTC_META_STATE_BYTES) == 0, "an A/B record is the state, the old log is no longer read");

    // a blank flash is a first start
    flash_init(&f); o = ops_for(&f);
    TEST_ASSERT(!urtc_meta_ab_load_state(&o, out, &legacy) && legacy == 0 && state_counter(out) == 0 && state_serial(out, &serial_counter) == 0, "a blank flash holds no state");

    // a log with its 28 slots full still reads
    flash_init(&f); o = ops_for(&f);
    for (uint32_t i = 0; i < URTC_META_LEGACY_SLOTS; i++) legacy_record(f.page[0], i, i + 1u, 200u + i);
    TEST_ASSERT(urtc_meta_ab_load_state(&o, out, &legacy) && state_counter(out) == 200u + URTC_META_LEGACY_SLOTS - 1u, "a full log reads its newest record");
}

static void test_migration_first_write(int *failures)
{
    Flash_t f; installed_board(&f);
    UrtcMetaAbOps o = ops_for(&f);
    uint8_t state[URTC_META_STATE_BYTES], out[URTC_META_STATE_BYTES];
    uint8_t legacy = 0;
    uint8_t old_page[PAGE_BYTES];
    memcpy(old_page, f.page[0], PAGE_BYTES);
    urtc_meta_ab_load_state(&o, state, &legacy);
    set_counter(state, 103u); // the rollback counter goes up
    TEST_ASSERT(urtc_meta_ab_store_state(&o, state), "the first write after the move succeeds");
    uint32_t gen = 0;
    TEST_ASSERT(urtc_meta_ab_newest(&o, URTC_META_STATE_BYTES, &gen) == 1, "the first A/B record goes to page 1");
    TEST_ASSERT(memcmp(f.page[0], old_page, PAGE_BYTES) == 0, "and the old log in page 0 is untouched");
    uint32_t serial_counter = 0;
    TEST_ASSERT(urtc_meta_ab_load_state(&o, out, &legacy) && legacy == 0 && state_counter(out) == 103u && state_serial(out, &serial_counter) == 0x11223344u && serial_counter == 5u,
                "the new state keeps the serial and has the new rollback counter");
    set_counter(state, 104u);
    TEST_ASSERT(urtc_meta_ab_store_state(&o, state) && urtc_meta_ab_newest(&o, URTC_META_STATE_BYTES, &gen) == 0,
                "the next write goes to page 0, where the old log was, because page 1 already holds a record");
    TEST_ASSERT(urtc_meta_ab_load_state(&o, out, &legacy) && state_counter(out) == 104u && state_serial(out, &serial_counter) == 0x11223344u, "and the serial is still there");
    // a flash that fails the first erase: the board keeps the old state
    installed_board(&f); o = ops_for(&f);
    urtc_meta_ab_load_state(&o, state, &legacy);
    set_counter(state, 103u);
    f.fail_erase = 1;
    TEST_ASSERT(!urtc_meta_ab_store_state(&o, state) && urtc_meta_ab_load_state(&o, out, &legacy) && legacy == 1 && state_counter(out) == 102u, "an erase that fails keeps the old log in force");
}

// The power goes at every step of the first write, with every way of leaving the flash: the serial and the rollback counter of the board are never lost.
static void test_migration_power_cut_sweep(int *failures)
{
    int total;
    {
        Flash_t f; installed_board(&f);
        UrtcMetaAbOps o = ops_for(&f);
        uint8_t state[URTC_META_STATE_BYTES]; uint8_t legacy;
        urtc_meta_ab_load_state(&o, state, &legacy);
        set_counter(state, 103u);
        f.step = 0;
        urtc_meta_ab_store_state(&o, state);
        total = f.step;
    }
    int lost = 0, wrong = 0, bad_recovery = 0, cases = 0, touched_old = 0;
    for (int mode = 0; mode < 3; mode++) {
        for (int cut = 0; cut < total; cut++) {
            Flash_t f; installed_board(&f);
            UrtcMetaAbOps o = ops_for(&f);
            uint8_t state[URTC_META_STATE_BYTES], out[URTC_META_STATE_BYTES], old_page[PAGE_BYTES];
            uint8_t legacy;
            memcpy(old_page, f.page[0], PAGE_BYTES);
            urtc_meta_ab_load_state(&o, state, &legacy);
            set_counter(state, 103u);
            f.step = 0; f.cut_at = cut; f.cut_mode = mode; f.seed = 11u + (uint32_t)(cut * 5 + mode);
            urtc_meta_ab_store_state(&o, state);
            cases++;
            f.cut_at = -1; f.cut = 0;                                  // the power is back
            uint32_t serial_counter = 0;
            const uint8_t has = urtc_meta_ab_load_state(&o, out, &legacy);
            const uint32_t counter = state_counter(out);
            if (!has) lost++;
            else if (state_serial(out, &serial_counter) != 0x11223344u || serial_counter != 5u) lost++;   // the serial and its counter are always there
            else if (counter != 102u && counter != 103u) wrong++;                                         // the rollback counter: the old one or the new, never another
            if (legacy && memcmp(f.page[0], old_page, PAGE_BYTES) != 0) touched_old++;                   // while the old log is in force it is intact
            // the next writes recover whatever the cut left
            set_counter(state, 110u);
            if (!urtc_meta_ab_store_state(&o, state) || !urtc_meta_ab_load_state(&o, out, &legacy) || state_counter(out) != 110u || state_serial(out, &serial_counter) != 0x11223344u) bad_recovery++;
            set_counter(state, 111u);
            if (!urtc_meta_ab_store_state(&o, state) || !urtc_meta_ab_load_state(&o, out, &legacy) || state_counter(out) != 111u || state_serial(out, &serial_counter) != 0x11223344u) bad_recovery++;
        }
    }
    TEST_ASSERT(cases == 3 * total && total > 0, "the sweep ran every cut with every way of leaving the flash");
    TEST_ASSERT(lost == 0, "a cut at any step of the move never loses the serial or leaves the board without a state");
    TEST_ASSERT(wrong == 0, "the rollback counter read after a cut is the old one or the new one, never another");
    TEST_ASSERT(touched_old == 0, "the old log is never touched while it is the state in force");
    TEST_ASSERT(bad_recovery == 0, "the next writes after a cut always succeed and keep the serial");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - urtc_meta_ab.h\n");
    test_basics(&failures);
    test_power_cut_sweep(&failures);
    test_generation_wrap(&failures);
    test_migration_reads(&failures);
    test_migration_first_write(&failures);
    test_migration_power_cut_sweep(&failures);
    if (failures == 0) {
        printf("  OK   all A/B metadata host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
