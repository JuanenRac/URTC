// =============================================================================
// URTC Firmware - Host logic tests for firmware_can_slavebridge.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The CAN <-> I2C bridge that lets the host update and query the expansion
// slave chip through the main board (0x210-0x219, 0x220/0x221). The module is
// compiled unchanged against the fake HAL in tests/host_hal/ and a scripted
// I2C slave: what each CAN frame writes to which register, how the four
// signature chunks are assembled, how the version answer is split over
// 0x217 / 0x218 (legacy and with the patch extension) and what happens when
// the slave does not answer.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"
#include "firmware_can_slavebridge.h"
#include "firmware_expansion_i2c.h"
#include "urtc_version_wire.h"

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];

// ---- scripted I2C slave -----------------------------------------------------
#define MAX_WRITES 16
typedef struct { uint8_t reg; uint8_t len; uint8_t data[40]; } WriteRecord;
static WriteRecord writes[MAX_WRITES];
static int write_count;
static uint8_t reg_value[256][16];
static uint8_t reg_readable[256];
static int read_count;

uint8_t ExpansionI2C_SlaveWriteRegister(uint8_t reg, const uint8_t *data, uint8_t len)
{
    if (write_count < MAX_WRITES) {
        writes[write_count].reg = reg;
        writes[write_count].len = len;
        memcpy(writes[write_count].data, data, len > 40 ? 40 : len);
        write_count++;
    }
    return 1;
}

uint8_t ExpansionI2C_SlaveReadRegister(uint8_t reg, uint8_t *data, uint8_t len)
{
    read_count++;
    if (!reg_readable[reg]) return 0;
    memcpy(data, reg_value[reg], len > 16 ? 16 : len);
    return 1;
}

static void clear(void)
{
    fake_hal_reset_recording();
    write_count = 0;
    read_count = 0;
    memset(reg_value, 0, sizeof reg_value);
    memset(reg_readable, 0, sizeof reg_readable);
}

static void rx(uint32_t id, uint32_t dlc, const uint8_t *data)
{
    memset(rxData, 0, sizeof rxData);
    if (data) memcpy(rxData, data, dlc);
    rxHeader.StdId = id;
    rxHeader.DLC = dlc;
    Handle_CAN_SlaveBridge();
}

static const FakeTxFrame *tx(uint32_t id)
{
    for (int i = 0; i < fake_tx_count; i++) if (fake_tx[i].id == id) return &fake_tx[i];
    return 0;
}

void run_can_slavebridge_tests(int *failures)
{
    uint8_t eight[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t magic[4] = { 0xB0, 0x07, 0x1D, 0x5A };

    // ---- plain forwarding ----------------------------------------------------
    clear();
    rx(0x210, 4, magic);
    TEST_ASSERT(write_count == 1 && writes[0].reg == 0x02 && writes[0].len == 4 && memcmp(writes[0].data, magic, 4) == 0, "0x210 forwards the 4-byte enter-bootloader payload to register 0x02");
    clear();
    rx(0x210, 3, magic);
    rx(0x210, 8, eight);
    TEST_ASSERT(write_count == 0, "0x210 with a DLC other than 4 is ignored");

    clear();
    rx(0x211, 8, eight);
    TEST_ASSERT(write_count == 1 && writes[0].reg == 0x01 && writes[0].len == 8 && memcmp(writes[0].data, eight, 8) == 0, "0x211 forwards the start-update frame to register 0x01");
    clear();
    rx(0x211, 7, eight);
    TEST_ASSERT(write_count == 0, "0x211 with a short frame is ignored");

    clear();
    rx(0x214, 8, eight);
    TEST_ASSERT(write_count == 1 && writes[0].reg == 0x04 && writes[0].len == 8, "0x214 forwards the end-update frame to register 0x04");
    clear();
    rx(0x214, 6, eight);
    TEST_ASSERT(write_count == 0, "0x214 with a short frame is ignored");

    clear();
    int ok = 1;
    for (uint32_t dlc = 1; dlc <= 8; dlc++) {
        clear();
        rx(0x213, dlc, eight);
        if (!(write_count == 1 && writes[0].reg == 0x03 && writes[0].len == dlc && memcmp(writes[0].data, eight, dlc) == 0)) ok = 0;
    }
    TEST_ASSERT(ok, "0x213 forwards 1 to 8 data bytes with their real length");
    clear();
    rx(0x213, 0, 0);
    TEST_ASSERT(write_count == 0, "0x213 without data is ignored");

    // ---- signature chunks ----------------------------------------------------
    clear();
    uint8_t chunks[4][8];
    for (int c = 0; c < 4; c++) for (int i = 0; i < 8; i++) chunks[c][i] = (uint8_t)(c * 16 + i);
    for (int c = 0; c < 3; c++) rx(0x212, 8, chunks[c]);
    TEST_ASSERT(write_count == 0, "three signature chunks are held back");
    rx(0x212, 8, chunks[3]);
    TEST_ASSERT(write_count == 1 && writes[0].reg == 0x02 && writes[0].len == 32, "the fourth chunk sends all 32 bytes to register 0x02 in one write");
    int in_order = 1;
    for (int c = 0; c < 4; c++) if (memcmp(&writes[0].data[c * 8], chunks[c], 8) != 0) in_order = 0;
    TEST_ASSERT(in_order, "the chunks are assembled in the order they arrived");
    for (int c = 0; c < 4; c++) rx(0x212, 8, chunks[3 - c]);
    TEST_ASSERT(write_count == 2 && memcmp(&writes[1].data[0], chunks[3], 8) == 0, "the next update starts again with an empty buffer");

    clear();
    rx(0x212, 8, chunks[0]);
    rx(0x212, 8, chunks[1]);
    rx(0x211, 8, eight);                  // a new update starts: the half-received signature is dropped
    write_count = 0;
    for (int c = 0; c < 3; c++) rx(0x212, 8, chunks[c]);
    TEST_ASSERT(write_count == 0, "after a new start the earlier chunks do not count: three more are still not enough");
    rx(0x212, 8, chunks[3]);
    TEST_ASSERT(write_count == 1 && memcmp(&writes[0].data[0], chunks[0], 8) == 0, "the signature written is made of the four chunks sent after the start");
    clear();
    rx(0x212, 7, chunks[0]);
    rx(0x212, 4, chunks[0]);
    TEST_ASSERT(write_count == 0, "a chunk of the wrong length is not counted");

    // ---- status, progress, verify-fail reason --------------------------------
    clear();
    reg_readable[0x00] = 1; reg_value[0x00][0] = 0x03;
    rx(0x215, 0, 0);
    TEST_ASSERT(tx(0x215) && tx(0x215)->dlc == 1 && tx(0x215)->data[0] == 0x03, "0x215 answers the slave bootloader status");
    clear();
    rx(0x215, 0, 0);
    TEST_ASSERT(fake_tx_count == 0, "a slave that does not answer produces no 0x215 frame");
    clear();
    reg_readable[0x05] = 1; reg_value[0x05][0] = 57;
    rx(0x216, 0, 0);
    TEST_ASSERT(tx(0x216) && tx(0x216)->data[0] == 57, "0x216 answers the progress");
    clear();
    reg_readable[0x07] = 1; reg_value[0x07][0] = 0x03;
    rx(0x219, 0, 0);
    TEST_ASSERT(tx(0x219) && tx(0x219)->data[0] == 0x03, "0x219 answers the verify-fail reason");
    clear();
    reg_readable[0x00] = 1;
    fake_tx_mailboxes_free = 0;
    rx(0x215, 0, 0);
    TEST_ASSERT(fake_tx_count == 0, "with no free CAN mailbox the answer is dropped, not queued behind");

    // ---- version: 0x217 + 0x218 ----------------------------------------------
    clear();
    reg_readable[0x06] = 1;
    uint8_t version[10] = { 0x01, 0x03, 0x03, 0xCB, 0x01, 0x00, 0x00, 0x02, 0x04, 0x01 };
    memcpy(reg_value[0x06], version, 10);
    rx(0x217, 0, 0);
    TEST_ASSERT(tx(0x217) && tx(0x217)->dlc == 8 && memcmp(tx(0x217)->data, version, 8) == 0, "0x217 carries the first 8 bytes of the slave version register");
    TEST_ASSERT(tx(0x218) && tx(0x218)->dlc == 2 && tx(0x218)->data[0] == 0x04 && tx(0x218)->data[1] == 0x01, "a slave without the extension answers 0x218 in its legacy 2-byte form");

    clear();
    reg_readable[0x06] = 1;
    memcpy(reg_value[0x06], version, 10);
    reg_readable[0x08] = 1;
    uint8_t ext[4];
    urtc_slave_version_ext_build(ext, 7, 3, 5);
    memcpy(reg_value[0x08], ext, 4);
    rx(0x217, 0, 0);
    const FakeTxFrame *p2 = tx(0x218);
    TEST_ASSERT(p2 && p2->dlc == 5, "a slave with the extension answers 0x218 in the 5-byte form");
    if (p2) TEST_ASSERT(p2->data[0] == 0x04 && p2->data[1] == 0x01 && p2->data[2] == 7 && p2->data[3] == 3 && p2->data[4] == 5, "0x218 = legacy 2 bytes, application patch, slave bootloader minor and patch");

    clear();
    reg_readable[0x06] = 1;
    memcpy(reg_value[0x06], version, 10);
    reg_readable[0x08] = 1;
    reg_value[0x08][0] = 0x01;           // a status code in the register, not the extension marker
    rx(0x217, 0, 0);
    TEST_ASSERT(tx(0x218) && tx(0x218)->dlc == 2, "a first byte that is not the marker is not taken for the extension");

    clear();
    rx(0x217, 0, 0);
    TEST_ASSERT(fake_tx_count == 0, "a slave that does not answer the version query produces neither 0x217 nor 0x218");

    // ---- generic register access ---------------------------------------------
    clear();
    uint8_t wr[4] = { 0x20, 0x84, 0x83, 0 };
    rx(0x220, 3, wr);
    TEST_ASSERT(write_count == 1 && writes[0].reg == 0x20 && writes[0].len == 2 && writes[0].data[0] == 0x84, "0x220 writes the bytes after the register number to that register");
    clear();
    rx(0x220, 4, wr);
    TEST_ASSERT(write_count == 0, "0x220 with a payload length the register does not take is dropped");
    clear();
    uint8_t boot[5] = { 0x02, 0xB0, 0x07, 0x1D, 0x5A };
    rx(0x220, 5, boot);
    TEST_ASSERT(write_count == 0, "0x220 never reaches REG_ENTER_BOOTLOADER");
    clear();
    uint8_t unknown[3] = { 0x7E, 1, 2 };
    rx(0x220, 3, unknown);
    TEST_ASSERT(write_count == 0, "0x220 to a register that is not in the list is dropped");
    clear();
    rx(0x220, 1, wr);
    TEST_ASSERT(write_count == 0, "0x220 without data is ignored");

    clear();
    reg_readable[0x13] = 1;
    for (int i = 0; i < 16; i++) reg_value[0x13][i] = (uint8_t)(0xA0 + i);
    uint8_t rd[8] = { 0x13, 5 };
    rx(0x221, 2, rd);
    TEST_ASSERT(tx(0x221) && tx(0x221)->dlc == 5 && tx(0x221)->data[0] == 0xA0 && tx(0x221)->data[4] == 0xA4, "0x221 reads the requested number of bytes");
    clear();
    reg_readable[0x13] = 1;
    uint8_t rd_big[8] = { 0x13, 32 };
    rx(0x221, 2, rd_big);
    TEST_ASSERT(tx(0x221) && tx(0x221)->dlc == 8, "0x221 never answers more than one frame of 8 bytes");
    clear();
    reg_readable[0x13] = 1;
    uint8_t rd_over[8] = { 0x13, 33 };
    rx(0x221, 2, rd_over);
    TEST_ASSERT(read_count == 0, "0x221 asking for more bytes than the register has is dropped");
    clear();
    reg_readable[0x30] = 1;
    uint8_t rd_wo[8] = { 0x30, 3 };
    rx(0x221, 2, rd_wo);
    TEST_ASSERT(read_count == 0, "0x221 of a write-only or unlisted register is dropped");
    clear();
    uint8_t rd_zero[8] = { 0x13, 0 };
    rx(0x221, 2, rd_zero);
    TEST_ASSERT(read_count == 0, "0x221 with a zero length is dropped");
    clear();
    rx(0x221, 2, rd);
    TEST_ASSERT(fake_tx_count == 0, "0x221 for a register the slave does not answer produces no frame");
    clear();
    rx(0x221, 3, rd);
    TEST_ASSERT(read_count == 0, "0x221 with a DLC other than 2 is ignored");

    // ---- other identifiers -----------------------------------------------------
    clear();
    rx(0x321, 8, eight);
    rx(0x222, 8, eight);
    TEST_ASSERT(write_count == 0 && read_count == 0 && fake_tx_count == 0, "identifiers outside the bridge range are ignored");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - firmware_can_slavebridge.c\n");
    run_can_slavebridge_tests(&failures);
    if (failures == 0) {
        printf("  OK   all CAN slave bridge host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
