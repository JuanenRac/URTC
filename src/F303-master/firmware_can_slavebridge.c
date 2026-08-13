// =============================================================================
// URTC Firmware - CAN-to-I2C bridge to the expansion slave chip
// (0x210-0x218 OTA relay, 0x220-0x221 application register access)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// The expansion slave chip (STM32F303CBT6, advanced boards only) has no
// CAN peripheral of its own - every byte the Flasher's OTA protocol
// sends it, and every application-mode command an advanced-board tool
// needs, reaches it relayed across the I2C link bus this board already
// bit-bangs as master (firmware_expansion_i2c.c's own
// ExpansionI2C_SlaveWriteRegister/ReadRegister, built and verified in an
// earlier session but never wired to a CAN command until now).
//
// Each CAN ID here maps to exactly one slave register - this project's
// SPI passthrough (0x180/0x181) genuinely IS byte-generic (it doesn't
// know or care what's on the other end), but this bridge isn't: the
// slave's own register numbers (slaveboot_common.h / slave_common.h)
// are fixed per command, so folding a register-selector byte into the
// CAN payload here would only cost a byte for no real flexibility -
// worse, several of these commands need the CAN frame's FULL 8 bytes as
// data (REG_START_UPDATE, REG_END_UPDATE both need exactly 8 payload
// bytes on the I2C side), which a register-selector byte inside the
// frame wouldn't leave room for. The I2C transaction itself has no
// CAN-style 8-byte ceiling, so the register byte gets added here,
// outside the CAN frame, not inside it.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_slavebridge.h"
#include "firmware_expansion_i2c.h"

// Slave register numbers - duplicated here rather than shared via a
// common header, since the slave is a genuinely separate chip/project
// with its own build (same relationship the main board's own
// bootloader_common.h has with its own application - see that pair's
// own precedent for this pattern). Values must stay in sync with
// slaveboot_common.h (REG_BOOT_*) and slave_common.h (REG_APP_*) by
// hand - there's no compiler to catch a mismatch here.
#define REG_BOOT_STATUS         0x00
#define REG_BOOT_START_UPDATE   0x01
#define REG_BOOT_HMAC_EXPECTED  0x02
#define REG_BOOT_DATA           0x03
#define REG_BOOT_END_UPDATE     0x04
#define REG_BOOT_PROGRESS       0x05
#define REG_BOOT_QUERY_VERSION  0x06
#define REG_APP_ENTER_BOOTLOADER 0x02

static uint8_t hmac_chunk_buf[32];
static uint8_t hmac_chunks_received = 0;

static void SendCanResponse(uint32_t id, const uint8_t *data, uint8_t len) {
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) return;
    CAN_TxHeaderTypeDef txH;
    txH.StdId = id;
    txH.IDE = CAN_ID_STD;
    txH.RTR = CAN_RTR_DATA;
    txH.DLC = len;
    txH.TransmitGlobalTime = DISABLE;
    uint32_t mb;
    HAL_CAN_AddTxMessage(&hcan, &txH, (uint8_t*)data, &mb);
}

void Handle_CAN_SlaveBridge(void) {
    // ---- OTA relay (Flasher -> this board -> slave's own bootloader) ----
    if (rxHeader.StdId == 0x210 && rxHeader.DLC == 4) {
        // Enter slave bootloader - relayed to the SLAVE'S OWN application-
        // mode REG_ENTER_BOOTLOADER, same magic-payload requirement as
        // the main board's own CAN 0x7F0 (see slave_common.h's own
        // REG_ENTER_BOOTLOADER comment) - this board doesn't check the
        // payload itself, the slave's own firmware does, same reasoning
        // as every other passthrough in this project not duplicating a
        // check its destination already makes correctly.
        ExpansionI2C_SlaveWriteRegister(REG_APP_ENTER_BOOTLOADER, rxData, 4);

    } else if (rxHeader.StdId == 0x211 && rxHeader.DLC == 8) {
        // Resets any partial accumulation left behind by an aborted/retried
        // sequence (bus glitch, Flasher restart) before this update's own
        // real 0x212 chunks start landing - without this, stale bytes from
        // a previous, never-completed attempt could still occupy the tail
        // of hmac_chunk_buf when a new sequence only sends fewer than 4
        // fresh chunks before something re-triggers completion.
        hmac_chunks_received = 0;
        ExpansionI2C_SlaveWriteRegister(REG_BOOT_START_UPDATE, rxData, 8);

    } else if (rxHeader.StdId == 0x212 && rxHeader.DLC == 8) {
        // Same 4-frames-of-8-bytes accumulation as the main board's own
        // bootloader HMAC chunk handling (CAN_ID_HMAC_CHUNK, bootloader_protocol.c) -
        // REG_HMAC_EXPECTED expects the complete 32-byte signature in one
        // I2C transaction (slaveboot_protocol.c's own HandleHmacExpected
        // doesn't accumulate partial writes the way HandleData does), so
        // this waits for all 4 frames before relaying anything.
        if (hmac_chunks_received < 4) {
            memcpy(&hmac_chunk_buf[hmac_chunks_received * 8], rxData, 8);
            hmac_chunks_received++;
            if (hmac_chunks_received == 4) {
                ExpansionI2C_SlaveWriteRegister(REG_BOOT_HMAC_EXPECTED, hmac_chunk_buf, 32);
                hmac_chunks_received = 0; // ready for the next update's own 4 chunks
            }
        }

    } else if (rxHeader.StdId == 0x213 && rxHeader.DLC >= 1) {
        // Firmware data - each CAN frame relayed as its own I2C
        // transaction. REG_DATA's own handler (slaveboot_protocol.c's
        // HandleData) accumulates across multiple calls exactly like
        // this, so no assembly buffer is needed here the way HMAC above
        // needs one.
        ExpansionI2C_SlaveWriteRegister(REG_BOOT_DATA, rxData, rxHeader.DLC);

    } else if (rxHeader.StdId == 0x214 && rxHeader.DLC == 8) {
        ExpansionI2C_SlaveWriteRegister(REG_BOOT_END_UPDATE, rxData, 8);

    } else if (rxHeader.StdId == 0x215) {
        // Status query - request can arrive with any DLC (same "DLC:any"
        // convention as the main board's own 0x7F8), response is always
        // 1 byte.
        uint8_t status;
        if (ExpansionI2C_SlaveReadRegister(REG_BOOT_STATUS, &status, 1)) {
            SendCanResponse(0x215, &status, 1);
        }
        // A failed I2C transaction (slave not present/not responding,
        // e.g. this is actually a BASIC expansion board with no slave
        // chip at all) gets no response sent at all, same as this
        // project's other passthrough commands - the Flasher's own
        // timeout is what surfaces that to the person, not a fabricated
        // error byte pretending to be real slave data.

    } else if (rxHeader.StdId == 0x216) {
        uint8_t progress;
        if (ExpansionI2C_SlaveReadRegister(REG_BOOT_PROGRESS, &progress, 1)) {
            SendCanResponse(0x216, &progress, 1);
        }

    } else if (rxHeader.StdId == 0x217) {
        // Bootloader version query - REG_QUERY_VERSION's own 10-byte
        // response doesn't fit one CAN frame, split across 0x217 (first
        // 8 bytes) and 0x218 (last 2) - same 2-response-ID split the
        // main board's own bootloader version query uses (0x7F9/0x7FA),
        // for the same reason.
        uint8_t version[10];
        if (ExpansionI2C_SlaveReadRegister(REG_BOOT_QUERY_VERSION, version, 10)) {
            SendCanResponse(0x217, version, 8);
            SendCanResponse(0x218, &version[8], 2);
        }

    // ---- Application register access (advanced-board tools, e.g. Paste
    // Jetting's own local PWM) ----
    } else if (rxHeader.StdId == 0x220 && rxHeader.DLC >= 2) {
        // Write - byte[0] is the slave's own application register
        // number (REG_PWM_*, REG_ADS_*, REG_MLX_* - see slave_common.h),
        // byte[1..] is that register's own payload, relayed as-is.
        ExpansionI2C_SlaveWriteRegister(rxData[0], &rxData[1], rxHeader.DLC - 1);

    } else if (rxHeader.StdId == 0x221 && rxHeader.DLC == 2) {
        // Read - byte[0] is the register to read, byte[1] is how many
        // bytes to read back (caller's own responsibility to ask for the
        // right length for whichever register it named - this bridge
        // doesn't maintain a length table per register the way it could,
        // since the caller already knows this from slave_common.h same
        // as this file does).
        uint8_t len = rxData[1];
        if (len > 8) len = 8; // one CAN response frame's own ceiling
        uint8_t response[8];
        if (ExpansionI2C_SlaveReadRegister(rxData[0], response, len)) {
            SendCanResponse(0x221, response, len);
        }
    }
}
