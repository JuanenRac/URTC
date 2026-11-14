// =============================================================================
// URTC Firmware - Host tests for the metadata of the master bootloader in two alternating pages (bootloader_flash.c built with URTC_BL_META_AB=1)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The real flash code of the bootloader runs against the fake flash of the host HAL. What is held here: a blank board starts in page 0; the state and the
// programmed serial live in one record, so writing either never loses the other; and a board that was installed with the log of one page (the layout of the
// bootloaders before the two pages) moves over WITHOUT losing its serial or its rollback counter, also when a flash step fails at any point of the first write.
// The logic of the pages itself, with the power cut at every half-word, is in tests/test_meta_ab.c.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "test_runner.h"
#include "stm32f3xx_hal.h"
#include "bootloader_common.h"
#include "bootloader_protocol.h"
#include "bootloader_flash.h"
#include "bootloader_oled.h"
#include "urtc_bl_serial.h"
#include "urtc_meta_ab.h"

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
IWDG_HandleTypeDef hiwdg;
I2C_HandleTypeDef hi2c2;

void OLED_Init_Boot(void) {}
void OLED_ClearAll_Boot(void) {}
void OLED_PrintStr2x_Boot(uint8_t page, uint8_t col, const char *str) { (void)page; (void)col; (void)str; }
void OLED_DrawProgressBar_Boot(uint8_t percent) { (void)percent; }

#define SERIAL_A (METADATA_ADDR + META_RECORD_SLOTS * META_RECORD_SIZE)
#define SERIAL_B (SERIAL_A + URTC_SERIAL_SLOT_BYTES)
#define SAMPLE_SERIAL 0x11223344u

static const uint8_t *page_bytes(uint32_t address) { return (const uint8_t *)(uintptr_t)address; }

static uint32_t first_word(uint32_t address)
{
    uint32_t w;
    memcpy(&w, page_bytes(address), 4);
    return w;
}

static void fresh(void) { fake_flash_erase_all(); }

static FirmwareMetadata_t sample_meta(uint32_t counter)
{
    FirmwareMetadata_t m;
    memset(&m, 0, sizeof m);
    m.magic = METADATA_MAGIC_VALID;
    m.state = META_STATE_APP_VALID;
    m.hardware_id = THIS_HARDWARE_ID;
    m.version_major = 0;
    m.version_minor = 4;
    m.size = 4096;
    m.crc32 = 0xA5A5A5A5u ^ counter;
    m.counter = counter;
    memset(m.sha256, (int)(counter & 0xFFu), sizeof m.sha256);
    return m;
}

// The log of one page written by a bootloader of the earlier layout: records of generation, metadata and CRC, then the two serial slots.
static void legacy_record(uint32_t slot, uint32_t generation, const FirmwareMetadata_t *meta)
{
    MetaRecord_t r;
    memset(&r, 0, sizeof r);
    r.generation = generation;
    r.meta = *meta;
    r.crc = CRC32_Finalize(CRC32_Update(0xFFFFFFFFUL, (const uint8_t *)&r, META_RECORD_SIZE - 4u));
    memcpy((void *)(uintptr_t)(METADATA_ADDR + slot * META_RECORD_SIZE), &r, sizeof r);
}

static void legacy_serial(uint32_t address, uint32_t serial, uint32_t counter)
{
    uint32_t words[4];
    urtc_serial_slot_pack(serial, counter, words);
    memcpy((void *)(uintptr_t)address, words, sizeof words);
}

// an installed board: three records (rollback counters 100, 101, 102) and a programmed serial with the counter 5
static void installed_board(void)
{
    fresh();
    for (uint32_t i = 0; i < 3; i++) {
        const FirmwareMetadata_t m = sample_meta(100u + i);
        legacy_record(i, i + 1u, &m);
    }
    legacy_serial(SERIAL_A, SAMPLE_SERIAL, 5u);
}

static void test_blank_board(int *failures)
{
    fresh();
    FirmwareMetadata_t read;
    uint32_t serial = 0, counter = 0;
    TEST_ASSERT(!Metadata_Read(&read) && read.magic == 0, "a blank flash holds no metadata");
    TEST_ASSERT(!Serial_ReadNewest(&serial, &counter), "and no serial");
    const FirmwareMetadata_t m = sample_meta(7u);
    TEST_ASSERT(Metadata_EraseAndWrite(&m) && Metadata_Read(&read) && memcmp(&read, &m, sizeof m) == 0, "the metadata written reads back");
    TEST_ASSERT(first_word(METADATA_ADDR) == URTC_META_AB_MAGIC && first_word(URTC_MASTER_METADATA_B_ADDR) == 0xFFFFFFFFu, "the first record of a blank board goes to page 0");
    TEST_ASSERT(Serial_Store(0xCAFE0001u, 3u) && Serial_ReadNewest(&serial, &counter) && serial == 0xCAFE0001u && counter == 3u, "the serial is stored");
    TEST_ASSERT(Metadata_Read(&read) && memcmp(&read, &m, sizeof m) == 0, "and writing the serial does not touch the metadata");
    TEST_ASSERT(first_word(URTC_MASTER_METADATA_B_ADDR) == URTC_META_AB_MAGIC, "the next record went to page 1");
    const FirmwareMetadata_t n = sample_meta(8u);
    TEST_ASSERT(Metadata_EraseAndWrite(&n) && Serial_ReadNewest(&serial, &counter) && serial == 0xCAFE0001u && counter == 3u, "writing the metadata does not touch the serial");
    TEST_ASSERT(Metadata_EraseAndWrite(&n), "the same metadata written again is accepted");
    // many writes: the pages alternate and nothing is lost
    for (uint32_t i = 0; i < 100u; i++) {
        const FirmwareMetadata_t x = sample_meta(1000u + i);
        if (!Metadata_EraseAndWrite(&x)) { TEST_ASSERT(0, "a write in the middle of a long series succeeds"); break; }
    }
    TEST_ASSERT(Metadata_Read(&read) && read.counter == 1099u && Serial_ReadNewest(&serial, &counter) && serial == 0xCAFE0001u, "after a hundred writes the newest state and the serial are in force");
}

static void test_installed_board_moves_over(int *failures)
{
    installed_board();
    FirmwareMetadata_t read;
    uint32_t serial = 0, counter = 0;
    TEST_ASSERT(Metadata_Read(&read) && read.counter == 102u, "a board with the log of one page keeps its rollback counter");
    TEST_ASSERT(Serial_ReadNewest(&serial, &counter) && serial == SAMPLE_SERIAL && counter == 5u, "and its programmed serial and the counter of the serial command");
    uint8_t old_page[2048];
    memcpy(old_page, page_bytes(METADATA_ADDR), sizeof old_page);
    const FirmwareMetadata_t next = sample_meta(103u);
    TEST_ASSERT(Metadata_EraseAndWrite(&next), "the first write after the move succeeds");
    TEST_ASSERT(first_word(URTC_MASTER_METADATA_B_ADDR) == URTC_META_AB_MAGIC, "its record goes to page 1");
    TEST_ASSERT(memcmp(page_bytes(METADATA_ADDR), old_page, sizeof old_page) == 0, "and the old log in page 0 is untouched");
    TEST_ASSERT(Metadata_Read(&read) && read.counter == 103u && Serial_ReadNewest(&serial, &counter) && serial == SAMPLE_SERIAL && counter == 5u, "the new state has the new counter and the old serial");
    TEST_ASSERT(Serial_Store(SAMPLE_SERIAL + 1u, 6u) && Serial_ReadNewest(&serial, &counter) && serial == SAMPLE_SERIAL + 1u && counter == 6u && Metadata_Read(&read) && read.counter == 103u,
                "a new serial is stored without losing the rollback counter");
    TEST_ASSERT(first_word(METADATA_ADDR) == URTC_META_AB_MAGIC, "the pages alternate from there on");

    // a board that has only the serial (no application ever installed) and one that has only metadata
    fresh();
    legacy_serial(SERIAL_B, 0x0A0B0C0Du, 2u);
    TEST_ASSERT(!Metadata_Read(&read) && Serial_ReadNewest(&serial, &counter) && serial == 0x0A0B0C0Du && counter == 2u, "a board with only a serial keeps it");
    TEST_ASSERT(Metadata_EraseAndWrite(&next) && Serial_ReadNewest(&serial, &counter) && serial == 0x0A0B0C0Du && counter == 2u && Metadata_Read(&read) && read.counter == 103u,
                "and gets its first metadata without losing the serial");
    fresh();
    const FirmwareMetadata_t only = sample_meta(55u);
    legacy_record(0, 1u, &only);
    TEST_ASSERT(Metadata_Read(&read) && read.counter == 55u && !Serial_ReadNewest(&serial, &counter), "a board with only metadata keeps it and has no serial");
    TEST_ASSERT(Serial_Store(0x01020304u, 1u) && Metadata_Read(&read) && read.counter == 55u, "and storing the first serial keeps the metadata");

    // a log whose 28 slots are full
    fresh();
    for (uint32_t i = 0; i < META_RECORD_SLOTS; i++) {
        const FirmwareMetadata_t m = sample_meta(300u + i);
        legacy_record(i, i + 1u, &m);
    }
    TEST_ASSERT(Metadata_Read(&read) && read.counter == 300u + META_RECORD_SLOTS - 1u, "a full log reads its newest record");
}

// Every flash step of the first write fails in turn (a program or an erase): the old state is always still in force, the old log is untouched, and the same
// write succeeds when it is tried again.
static void test_failed_move_keeps_the_board(int *failures)
{
    int lost = 0, touched = 0, retry_failed = 0, tried = 0;
    for (int kind = 0; kind < 2; kind++) {              // 0: a program fails, 1: an erase fails
        for (int nth = 1; nth <= 60; nth++) {
            installed_board();
            uint8_t old_page[2048];
            memcpy(old_page, page_bytes(METADATA_ADDR), sizeof old_page);
            if (kind == 0) fake_flash_fail_program_nth = nth; else fake_flash_fail_erase_nth = nth;
            const FirmwareMetadata_t next = sample_meta(103u);
            const uint8_t done = Metadata_EraseAndWrite(&next);
            fake_flash_fail_program_nth = 0; fake_flash_fail_erase_nth = 0;
            tried++;
            FirmwareMetadata_t read;
            uint32_t serial = 0, counter = 0;
            const uint8_t has = Metadata_Read(&read);
            const uint8_t has_serial = Serial_ReadNewest(&serial, &counter);
            if (!has || !has_serial || serial != SAMPLE_SERIAL || counter != 5u) lost++;
            else if (read.counter != (done ? 103u : 102u)) lost++;
            if (!done && memcmp(page_bytes(METADATA_ADDR), old_page, sizeof old_page) != 0) touched++;
            if (!Metadata_EraseAndWrite(&next) || !Metadata_Read(&read) || read.counter != 103u || !Serial_ReadNewest(&serial, &counter) || serial != SAMPLE_SERIAL) retry_failed++;
        }
    }
    TEST_ASSERT(tried == 120, "every step of the first write was failed in turn");
    TEST_ASSERT(lost == 0, "a failing step never loses the serial or the rollback counter, and the state is the old one or the new one");
    TEST_ASSERT(touched == 0, "the old log is not touched while page 1 has no complete record");
    TEST_ASSERT(retry_failed == 0, "the write succeeds when it is tried again");
}

int main(void)
{
    int failures = 0;
    printf("URTC host tests - the metadata of the master bootloader in two alternating pages\n");
    if (!fake_flash_init()) { printf("  SKIP: cannot map the fake flash on this host\n"); return 0; }
    test_blank_board(&failures);
    test_installed_board_moves_over(&failures);
    test_failed_move_keeps_the_board(&failures);
    if (failures == 0) {
        printf("  OK   all A/B metadata bootloader host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
