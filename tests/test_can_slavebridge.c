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
#include "firmware_slave_attest.h"
#include "urtc_slave_confirm.h"
#include "boot_sig_cases.h"
#include "boot/urtc_trust_dev.h"

IWDG_HandleTypeDef hiwdg;
CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];

// ---- scripted I2C slave -----------------------------------------------------
#define MAX_WRITES 512
typedef struct { uint8_t reg; uint8_t len; uint8_t data[40]; } WriteRecord;
static WriteRecord writes[MAX_WRITES];
static int write_count;
static uint8_t reg_value[256][16];
static uint8_t reg_readable[256];
static int read_count;

static int fail_data_writes_from = -1;   // when >= 0: the data writes (register 0x03) after this many succeed no more
static int data_writes_seen;

uint8_t ExpansionI2C_SlaveWriteRegister(uint8_t reg, const uint8_t *data, uint8_t len)
{
    if (reg == 0x03 && fail_data_writes_from >= 0 && data_writes_seen++ >= fail_data_writes_from) return 0;
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

// ---- the master vouches for the slave's image ------------------------------------------------------------------------------------------------------
// The signed cases of tests/boot_sig_cases.h (throwaway test keys, made by tools/make_boot_sig_cases.py): a patterned image of 1500 bytes for the expansion
// slave's hardware id, signed with the development key. The module is the real one, the Ed25519 verifier is the real one; only the I2C bus is scripted.
#define SLAVE_CASE 14      // 1500 bytes, slave hardware id, version 0.2.0, counter 1, development key
#define MASTER_CASE 1      // the same size and version, for the main board's hardware id

static uint8_t keys_in_use[2][32];
static uint8_t key_count_in_use;

static const uint8_t *table_in_use;      // when set, the keys come from this trust table, as they do from the bootloader on the board
static uint32_t table_in_use_length;

void SlaveAttest_LoadTrustFromBootloader(void)
{
    if (table_in_use) (void)SlaveAttest_SetKeysFromTable(table_in_use, table_in_use_length);
    else SlaveAttest_SetKeys((const uint8_t (*)[32])keys_in_use, key_count_in_use);
}

static void use_dev_key(void) { memcpy(keys_in_use[0], BOOT_SIG_PUBLIC_DEV, 32); key_count_in_use = 1; }

static uint32_t ref_crc32(const uint8_t *d, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
    }
    return ~c;
}

typedef struct {
    int send_record;          // 0: no 0x21A frames
    int damage_record;        // flip a bit of the signature
    int damage_data;          // flip a bit of the image in transit
    int drop_last_frame;      // the last data frame never arrives
    int start_hardware_id;    // the hardware id of 0x211 (0: the slave's)
    int slave_waits;          // the scripted slave reports AWAIT_CONFIRM
    int skip_end;             // no 0x214
    int floor_unreadable;     // the slave does not answer REG_COUNTER_FLOOR (an older bootloader)
    uint32_t slave_floor;     // the floor the scripted slave reports
} Scenario;

static uint8_t scenario_nonce[16];
static uint8_t image_buf[1500];

// Plays the whole exchange of an update through the bridge; the scripted slave answers the status and the nonce.
static void play(const BootSigCase *c, Scenario s)
{
    for (uint32_t i = 0; i < c->size; i++) image_buf[i] = (uint8_t)((i * 13 + 5) & 0xFF);
    uint32_t crc = ref_crc32(image_buf, c->size);
    if (s.damage_data) image_buf[700] ^= 0x04;
    uint8_t record[77];
    memset(record, 0, sizeof record);
    memcpy(record, c->record, 76);
    if (s.damage_record) record[40] ^= 0x01;
    reg_readable[0x00] = 1; reg_value[0x00][0] = s.slave_waits ? 0x08 : 0x03;
    reg_readable[0x09] = 1; memcpy(reg_value[0x09], scenario_nonce, 16);
    reg_readable[0x0B] = s.floor_unreadable ? 0 : 1;
    reg_value[0x0B][0] = (uint8_t)(s.slave_floor >> 24); reg_value[0x0B][1] = (uint8_t)(s.slave_floor >> 16); reg_value[0x0B][2] = (uint8_t)(s.slave_floor >> 8); reg_value[0x0B][3] = (uint8_t)s.slave_floor;

    uint8_t start[8] = { (uint8_t)(c->size >> 24), (uint8_t)(c->size >> 16), (uint8_t)(c->size >> 8), (uint8_t)c->size, 0, 0, 0, 0 };
    uint32_t hw = s.start_hardware_id ? (uint32_t)s.start_hardware_id : 0x0303CB01u;
    start[4] = (uint8_t)(hw >> 24); start[5] = (uint8_t)(hw >> 16); start[6] = (uint8_t)(hw >> 8); start[7] = (uint8_t)hw;
    rx(0x211, 8, start);
    uint8_t digest_chunk[8] = {0};
    for (int i = 0; i < 4; i++) rx(0x212, 8, digest_chunk);
    if (s.send_record) {
        for (uint8_t i = 0; i < 11; i++) {
            uint8_t chunk[8];
            chunk[0] = i;
            memcpy(&chunk[1], &record[i * 7], 7);
            rx(0x21A, 8, chunk);
        }
    }
    uint32_t frames = (c->size + 7) / 8;
    for (uint32_t f = 0; f < frames; f++) {
        if (s.drop_last_frame && f == frames - 1) break;
        uint32_t n = c->size - f * 8 < 8 ? c->size - f * 8 : 8;
        rx(0x213, n, &image_buf[f * 8]);
    }
    if (!s.skip_end) {
        uint8_t end[8] = { (uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc,
                           (uint8_t)(c->major >> 8), (uint8_t)c->major, (uint8_t)(c->minor_field >> 8), (uint8_t)c->minor_field };
        rx(0x214, 8, end);
    }
}

static int find_write(uint8_t reg)
{
    for (int i = write_count - 1; i >= 0; i--) if (writes[i].reg == reg) return i;
    return -1;
}

static int confirm_matches(const BootSigCase *c, const uint8_t nonce[16])
{
    int w = find_write(0x0A);
    if (w < 0 || writes[w].len != 32) return 0;
    uint8_t message[URTC_SLAVE_CONFIRM_MESSAGE_SIZE], tag[32];
    urtc_slave_confirm_message(message, nonce, c->sha256, c->hardware_id, (uint16_t)c->major, (uint16_t)c->minor_field, c->size, c->counter);
    SlaveAttest_Sha256(message, sizeof message, tag);
    return memcmp(tag, writes[w].data, 32) == 0;
}

static void attest_tests(int *failures)
{
    const BootSigCase *slave = &BOOT_SIG_CASES[SLAVE_CASE];
    Scenario good = { 1, 0, 0, 0, 0, 1, 0, 0, 0 };
    for (int i = 0; i < 16; i++) scenario_nonce[i] = (uint8_t)(0xA0 + i);

    // a valid record: the slave gets END first and then the confirmation, which carries the right tag
    clear(); use_dev_key();
    play(slave, good);
    TEST_ASSERT(find_write(0x04) >= 0 && find_write(0x0A) > find_write(0x04), "a valid record: END is relayed first and then the confirmation is written to register 0x0A");
    TEST_ASSERT(confirm_matches(slave, scenario_nonce), "the confirmation is the SHA-256 of the message with the nonce the slave published, the image digest, hardware id, version and size");
    int slave_registers_written = 1;
    for (int i = 0; i < write_count; i++) if (writes[i].reg > 0x0C) slave_registers_written = 0;
    TEST_ASSERT(slave_registers_written, "the record chunks are not relayed to the slave: only its bootloader registers 0x01 to 0x0C are written");
    {
        int wc = find_write(0x0C);
        TEST_ASSERT(wc >= 0 && wc < find_write(0x04) && writes[wc].len == 4 && writes[wc].data[3] == (uint8_t)slave->counter && writes[wc].data[0] == 0 && writes[wc].data[1] == 0 && writes[wc].data[2] == 0, "the signed counter of the record is written to register 0x0C before the END frame is relayed");
    }
    int cw = find_write(0x0A);
    TEST_ASSERT(writes[cw].len == 32, "the confirmation is exactly 32 bytes");

    // the tag follows the session: another nonce, another tag
    uint8_t first_tag[32];
    memcpy(first_tag, writes[cw].data, 32);
    scenario_nonce[0] ^= 0xFF;
    clear(); use_dev_key();
    play(slave, good);
    TEST_ASSERT(confirm_matches(slave, scenario_nonce) && memcmp(first_tag, writes[find_write(0x0A)].data, 32) != 0, "a different nonce gives a different confirmation");
    scenario_nonce[0] ^= 0xFF;

    // the same END frame again does not confirm a second time
    {
        int confirms_before = 0, confirms_after = 0;
        for (int i = 0; i < write_count; i++) if (writes[i].reg == 0x0A) confirms_before++;
        uint8_t again[8] = { 0, 0, 0, 0, 0, 0, 0, 2 };
        rx(0x214, 8, again);
        for (int i = 0; i < write_count; i++) if (writes[i].reg == 0x0A) confirms_after++;
        TEST_ASSERT(confirms_before == 1 && confirms_after == 1, "a second END after the update is only relayed, never confirmed again");
    }

    // nothing is confirmed, but the END frame is still relayed (the slave fails closed by itself)
    Scenario s;
    s = good; s.send_record = 0;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) < 0 && find_write(0x04) >= 0, "no record: END is relayed and there is no confirmation");
    s = good; s.damage_record = 1;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) < 0, "a record with a damaged signature is not confirmed");
    s = good; s.damage_data = 1;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) < 0, "an image altered in transit does not match the signed digest: no confirmation");
    s = good; s.drop_last_frame = 1;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) < 0, "an image that is shorter than declared is not confirmed");
    s = good; s.skip_end = 1;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) < 0 && find_write(0x04) < 0, "without END there is nothing to confirm");
    s = good; s.start_hardware_id = 0x0303CC01;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) < 0, "an update declared for the main board's hardware id is never confirmed for the slave");
    clear(); use_dev_key(); play(&BOOT_SIG_CASES[MASTER_CASE], good);
    TEST_ASSERT(find_write(0x0A) < 0, "a record signed for the main board's hardware id does not vouch for a slave image");

    // the rollback floor of the slave
    s = good; s.slave_floor = slave->counter;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) >= 0, "a record whose counter equals the floor of the slave is confirmed");
    s = good; s.slave_floor = slave->counter + 1;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) < 0 && find_write(0x0C) < 0 && find_write(0x04) >= 0, "a record below the floor the slave reports is never confirmed: the master refuses before the slave does, and END is still relayed");
    s = good; s.floor_unreadable = 1;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) < 0 && find_write(0x0C) < 0, "a slave that does not report its floor (an older bootloader) gets no confirmation: an unknown floor vouches for nothing");

    // keys
    clear(); key_count_in_use = 0; play(slave, good);
    TEST_ASSERT(find_write(0x0A) < 0, "a closed trust table (no key) confirms nothing");
    clear(); memcpy(keys_in_use[0], BOOT_SIG_PUBLIC_OTHER, 32); key_count_in_use = 1; play(slave, good);
    TEST_ASSERT(find_write(0x0A) < 0, "a record signed by a key that is not in the table is not confirmed");
    clear(); memcpy(keys_in_use[0], BOOT_SIG_PUBLIC_OTHER, 32); memcpy(keys_in_use[1], BOOT_SIG_PUBLIC_DEV, 32); key_count_in_use = 2; play(slave, good);
    TEST_ASSERT(find_write(0x0A) >= 0, "the second key of the table (rotation) vouches too");

    // the trust table of the bootloader, parsed by the same rules as the bootloader
    {
        uint8_t table[URTC_TRUST_TABLE_SIZE] = { URTC_TRUST_TABLE_BYTES };
        table_in_use = table; table_in_use_length = sizeof table;
        clear(); play(slave, good);
        TEST_ASSERT(find_write(0x0A) >= 0, "the keys read from the development trust table of the bootloader vouch for the test image");
        table[10] ^= 0x01;   // a bit of the key id: the CRC-32 of the table no longer matches
        clear(); play(slave, good);
        TEST_ASSERT(SlaveAttest_KeyCount() == 0 && find_write(0x0A) < 0, "a damaged trust table closes it: no key, no confirmation");
        table[10] ^= 0x01;
        table[4] = 2;        // a schema this code does not know
        clear(); play(slave, good);
        TEST_ASSERT(SlaveAttest_KeyCount() == 0 && find_write(0x0A) < 0, "an unknown schema closes the table too");
        table_in_use = 0;
    }

    // the bus
    s = good; s.slave_waits = 0;
    clear(); use_dev_key(); play(slave, s);
    TEST_ASSERT(find_write(0x0A) < 0, "a slave that never reports AWAIT_CONFIRM gets no confirmation (and the bridge gives up)");
    clear(); use_dev_key(); fail_data_writes_from = 100; data_writes_seen = 0; play(slave, good); fail_data_writes_from = -1;
    TEST_ASSERT(find_write(0x0A) < 0, "data that did not reach the slave is not confirmed");
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

    // ---- the patch number of the slave application (REG_APP_VERSION byte 9) -----
    uint8_t rd_version[8] = { 0x01, 8 };
    clear();
    reg_readable[0x01] = 1;
    { const uint8_t v[10] = { 0x00, 0x03, 0x03, 0xCB, 0x01, 0x00, 0x00, 0x00, 0x02, 0x04 }; memcpy(reg_value[0x01], v, 10); }
    rx(0x221, 2, rd_version);
    TEST_ASSERT(tx(0x221) && tx(0x221)->dlc == 8 && tx(0x221)->data[7] == 0x00 && read_count == 1, "the version answer is the same eight bytes as before");
    TEST_ASSERT(tx(0x222) && tx(0x222)->dlc == 3 && tx(0x222)->data[0] == 0x01 && tx(0x222)->data[1] == 0x04, "a patch in the tenth byte follows in the 0x222 companion, with the register it belongs to");
    TEST_ASSERT(tx(0x222)->data[2] == 0x02, "and so does the low byte of the minor, the ninth byte of the register, which the eight-byte answer cuts off");
    TEST_ASSERT(fake_tx_count == 2, "and nothing else is sent");
    clear();
    reg_readable[0x01] = 1;                       // a slave application from before the field: the tenth byte reads 0
    { const uint8_t v[10] = { 0x00, 0x03, 0x03, 0xCB, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00 }; memcpy(reg_value[0x01], v, 10); }
    rx(0x221, 2, rd_version);
    TEST_ASSERT(tx(0x221) && tx(0x221)->dlc == 8 && !tx(0x222) && fake_tx_count == 1, "a tenth byte of 0 is ignored: no companion, the answer a legacy slave always got");
    clear();
    reg_readable[0x01] = 1;
    reg_value[0x01][9] = 0x80;
    rx(0x221, 2, rd_version);
    TEST_ASSERT(!tx(0x222), "a tenth byte above 127 is not a patch number");
    clear();
    reg_readable[0x01] = 1;
    reg_value[0x01][9] = 0x7F;
    rx(0x221, 2, rd_version);
    TEST_ASSERT(tx(0x222) && tx(0x222)->data[1] == 0x7F, "127 is the largest patch number");
    clear();
    reg_readable[0x01] = 1;
    reg_value[0x01][8] = 0xFF;
    reg_value[0x01][9] = 0x03;
    rx(0x221, 2, rd_version);
    TEST_ASSERT(tx(0x222) && tx(0x222)->data[2] == 0xFF && tx(0x222)->dlc == 3, "the low byte of the minor goes through whole, up to 255");
    TEST_ASSERT(tx(0x221) && tx(0x221)->data[7] == 0x00 && tx(0x221)->dlc == 8, "while the answer to 0x221 is still the same eight bytes");
    clear();
    reg_readable[0x01] = 1;
    reg_value[0x01][9] = 5;
    uint8_t rd_short[8] = { 0x01, 2 };
    rx(0x221, 2, rd_short);
    TEST_ASSERT(tx(0x221) && tx(0x221)->dlc == 2 && tx(0x222) && tx(0x222)->data[1] == 5, "a host that asks for fewer bytes still gets the companion");
    clear();
    rx(0x221, 2, rd_version);
    TEST_ASSERT(fake_tx_count == 0, "a slave that does not answer produces no companion either");
    clear();
    reg_readable[0x13] = 1;
    reg_value[0x13][9] = 5;
    uint8_t rd_other[8] = { 0x13, 10 };
    rx(0x221, 2, rd_other);
    TEST_ASSERT(!tx(0x222), "only the version register has a companion");

    // ---- other identifiers -----------------------------------------------------
    clear();
    rx(0x321, 8, eight);
    rx(0x222, 8, eight);
    TEST_ASSERT(write_count == 0 && read_count == 0 && fake_tx_count == 0, "identifiers outside the bridge range are ignored");

    attest_tests(failures);
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
