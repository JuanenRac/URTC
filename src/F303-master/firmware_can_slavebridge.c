// =============================================================================
// URTC Firmware - CAN-to-I2C bridge to the expansion slave chip
// (0x210-0x219 OTA relay, 0x220-0x221 application register access)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
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
#include "urtc_version_wire.h"
#include "urtc_slave_regs.h"
#include "urtc_can_ids.h"
#include "firmware_slave_attest.h"
#include "urtc_slave_confirm.h"
// URTC_APP_VERSION_COMPAT_RELAY=1: when the metadata of the master holds "accept an image without a patch" (urtc_vc_annex.h, written only by a bootloader built
// with URTC_BL_VERSION_COMPAT_COMMAND), the slave is told so for this one update. OFF by default. The application only reads the pages.
#ifndef URTC_APP_VERSION_COMPAT_RELAY
#define URTC_APP_VERSION_COMPAT_RELAY 0
#endif
#if URTC_APP_VERSION_COMPAT_RELAY
#include "urtc_flash_layout.h"
#include "urtc_vc_annex.h"
#define REG_BOOT_VERSION_COMPAT URTC_VC_SLAVE_REG   // a register of the slave bootloader, like the REG_BOOT_* below
#endif

// Slave register numbers - duplicated here rather than shared via a
// common header, since the slave is a genuinely separate chip/project
// with its own build (same relationship the main board's own
// bootloader_common.h has with its own application - see that pair's
// own precedent for this pattern). Values must stay in sync with
// slaveboot_common.h (REG_BOOT_*) and slave_common.h (REG_APP_*) by
// hand - there's no compiler to catch a mismatch here.
#define REG_BOOT_STATUS         0x00
#define REG_BOOT_START_UPDATE   0x01
#define REG_BOOT_DIGEST_EXPECTED  0x02
#define REG_BOOT_DATA           0x03
#define REG_BOOT_END_UPDATE     0x04
#define REG_BOOT_PROGRESS       0x05
#define REG_BOOT_QUERY_VERSION  0x06
#define REG_BOOT_VERIFY_FAIL_REASON 0x07
#define REG_BOOT_QUERY_VERSION_EXT  0x08
#define REG_BOOT_CHALLENGE      0x09
#define REG_BOOT_CONFIRM        0x0A
#define REG_BOOT_COUNTER_FLOOR  0x0B
#define REG_BOOT_RECORD_COUNTER 0x0C
#define SLAVE_BOOT_STATUS_AWAIT_CONFIRM 0x08
#define REG_APP_ENTER_BOOTLOADER 0x02
// First byte of the answer to REG_BOOT_QUERY_VERSION: 1 when the slave bootloader answers (BuildVersionQueryResponse). The slave application has no such
// register: it answers every unknown register with its one-byte idle status (0), and the nine bytes after it are not data of the slave.
#define SLAVE_BOOT_VERSION_MARKER 0x01

static uint8_t digest_chunk_buf[32];

// The slave bootloader holds a verified image until the master confirms that it checked the signature record (urtc_slave_confirm.h). Called after the END
// frame has been relayed and only when SlaveAttest_Verify said yes: waits (one second at most) until the slave reports that it is waiting, reads the nonce of
// the session it published, and writes the 32-byte confirmation. In every other case nothing is written and the slave fails closed on its own after ten
// seconds.
static void ConfirmToSlave(void) {
    uint8_t status = 0;
    uint8_t waiting = 0;
    for (uint8_t tries = 0; tries < 100u && !waiting; tries++) {
        if (ExpansionI2C_SlaveReadRegister(REG_BOOT_STATUS, &status, 1) && status == SLAVE_BOOT_STATUS_AWAIT_CONFIRM) {
            waiting = 1;
        } else {
            HAL_Delay(10);
            HAL_IWDG_Refresh(&hiwdg);
        }
    }
    if (!waiting) return;
    uint8_t nonce[SLAVE_ATTEST_NONCE_SIZE];
    uint8_t tag[URTC_SLAVE_CONFIRM_TAG_SIZE];
    if (!ExpansionI2C_SlaveReadRegister(REG_BOOT_CHALLENGE, nonce, SLAVE_ATTEST_NONCE_SIZE)) return;
    if (!SlaveAttest_Tag(nonce, tag)) return;
    ExpansionI2C_SlaveWriteRegister(REG_BOOT_CONFIRM, tag, URTC_SLAVE_CONFIRM_TAG_SIZE);
}
static uint8_t digest_chunks_received = 0;
// The four digest chunks carry no index: their place is implicit, the count of chunks of the group of four that has arrived (0 after the fourth). 0x211 zeroes it
// and 0x214 checks it: a count that is not zero there means a chunk was lost on the bus. The flag then stays up until the next 0x211, so that the status and the
// reason queries can say so instead of leaving a signature mismatch to be guessed.
static uint8_t digest_chunk_lost = 0;

// The end frame of an update waits here, written by the receive interrupt and taken by SlaveBridge_Service in the main loop.
static volatile uint8_t slave_end_pending = 0;
static uint8_t slave_end_data[8];

// The check of the signature record takes most of a second in the main loop, where the safety ticks of the board (the thermal and the pulse loops, the 250 ms
// watchdogs) stand still. So the end of an update is refused while a tool is active: the probe answers 1 when every actuator is at zero. Without a probe (a host test
// that does not set one) nothing is refused. The flag stays up until the next start (0x211), so that the status and the reason queries can say why.
static uint8_t (*actuators_off_probe)(void) = 0;
static uint8_t end_refused_busy = 0;

void SlaveBridge_SetActuatorProbe(uint8_t (*probe)(void)) {
    actuators_off_probe = probe;
}

// The switch that lets the end through while a tool is active: none set, or an answer of 0, keeps the refusal (the default).
static uint8_t (*allow_active_actuator)(void) = 0;

void SlaveBridge_SetAllowActiveActuator(uint8_t (*allowed)(void)) {
    allow_active_actuator = allowed;
}

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
    // While an end frame waits for the main loop, the frames that feed the check (data, record and digest chunks) are dropped: the check must read what was there at the end.
    if (slave_end_pending && (rxHeader.StdId == URTC_CAN_ID_SLAVE_DATA || rxHeader.StdId == URTC_CAN_ID_SLAVE_RECORD_CHUNK || rxHeader.StdId == URTC_CAN_ID_SLAVE_DIGEST_CHUNK)) return;
    // ---- OTA relay (Flasher -> this board -> slave's own bootloader) ----
    if (rxHeader.StdId == URTC_CAN_ID_SLAVE_ENTER_BOOTLOADER && rxHeader.DLC == 4) {
        // Enter slave bootloader - relayed to the SLAVE'S OWN application-
        // mode REG_ENTER_BOOTLOADER, same magic-payload requirement as
        // the main board's own CAN 0x7F0 (see slave_common.h's own
        // REG_ENTER_BOOTLOADER comment) - this board doesn't check the
        // payload itself, the slave's own firmware does, same reasoning
        // as every other passthrough in this project not duplicating a
        // check its destination already makes correctly.
        ExpansionI2C_AppWriteRegister(REG_APP_ENTER_BOOTLOADER, rxData, 4);

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_START_UPDATE && rxHeader.DLC == 8) {
        // Resets any partial accumulation left behind by an aborted/retried
        // sequence (bus glitch, Flasher restart) before this update's own
        // real 0x212 chunks start landing - without this, stale bytes from
        // a previous, never-completed attempt could still occupy the tail
        // of digest_chunk_buf when a new sequence only sends fewer than 4
        // fresh chunks before something re-triggers completion.
        digest_chunks_received = 0;
        digest_chunk_lost = 0;
        end_refused_busy = 0;
        slave_end_pending = 0;   // a new start cancels an end that has not been checked yet
        // The rollback floor of the slave is read BEFORE the START frame goes out: afterwards the slave is erasing its backup slot and does not answer. A slave
        // that does not answer (an older bootloader, no slave chip) leaves the floor unknown, and an unknown floor means nothing is ever vouched for.
        uint8_t floor_bytes[4];
        uint8_t floor_known = ExpansionI2C_SlaveReadRegister(REG_BOOT_COUNTER_FLOOR, floor_bytes, 4);
        ExpansionI2C_SlaveWriteRegister(REG_BOOT_START_UPDATE, rxData, 8);
        // The master also follows the update to be able to vouch for it: it hashes what it relays and collects the signature record (0x21A).
        SlaveAttest_LoadTrustFromBootloader();
        SlaveAttest_SetSlaveFloor(floor_known ? (((uint32_t)floor_bytes[0] << 24) | ((uint32_t)floor_bytes[1] << 16) | ((uint32_t)floor_bytes[2] << 8) | floor_bytes[3]) : 0u,
                                  floor_known);
        SlaveAttest_Start(((uint32_t)rxData[0] << 24) | ((uint32_t)rxData[1] << 16) | ((uint32_t)rxData[2] << 8) | rxData[3],
                          ((uint32_t)rxData[4] << 24) | ((uint32_t)rxData[5] << 16) | ((uint32_t)rxData[6] << 8) | rxData[7]);

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_RECORD_CHUNK && rxHeader.DLC == 8) {
        // One of the 11 chunks of the 76-byte Ed25519 signature record of the slave's image: the index and 7 bytes of it. Nothing goes to the slave; the
        // master keeps it and checks it when the END frame arrives.
        if (rxData[0] < SLAVE_ATTEST_CHUNK_COUNT) {
            SlaveAttest_RecordChunk(rxData[0], &rxData[1]);
        }

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_DIGEST_CHUNK && rxHeader.DLC == 8) {
        // Same 4-frames-of-8-bytes accumulation as the main board's own
        // bootloader HMAC chunk handling (CAN_ID_HMAC_CHUNK, bootloader_protocol.c) -
        // REG_DIGEST_EXPECTED expects the complete 32-byte signature in one
        // I2C transaction (slaveboot_protocol.c's own HandleDigestExpected
        // doesn't accumulate partial writes the way HandleData does), so
        // this waits for all 4 frames before relaying anything.
        if (digest_chunks_received < 4) {
            memcpy(&digest_chunk_buf[digest_chunks_received * 8], rxData, 8);
            digest_chunks_received++;
            if (digest_chunks_received == 4) {
                ExpansionI2C_SlaveWriteRegister(REG_BOOT_DIGEST_EXPECTED, digest_chunk_buf, 32);
                digest_chunks_received = 0; // ready for the next update's own 4 chunks
            }
        }

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_DATA && rxHeader.DLC >= 1) {
        // Firmware data - each CAN frame relayed as its own I2C
        // transaction. REG_DATA's own handler (slaveboot_protocol.c's
        // HandleData) accumulates across multiple calls exactly like
        // this, so no assembly buffer is needed here the way HMAC above
        // needs one.
        uint8_t relayed = ExpansionI2C_SlaveWriteRegister(REG_BOOT_DATA, rxData, (uint8_t)rxHeader.DLC);
        SlaveAttest_Data(rxData, (uint8_t)rxHeader.DLC, relayed);

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_END_UPDATE && rxHeader.DLC == 8 && digest_chunks_received != 0) {
        // The end arrives with a group of digest chunks left half way: one of them never came. Nothing is verified, forwarded or confirmed (the slave stays as it is
        // and times out on its own), and 0x215 / 0x219 answer VERIFY_FAIL / DIGEST_CHUNK_MISSING until the next start.
        digest_chunk_lost = 1;
        digest_chunks_received = 0;

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_END_UPDATE && rxHeader.DLC == 8) {
        // The end of the update is only NOTED here. Checking the signature record (a whole Ed25519 verification, longer than the period of the watchdog) and confirming to the
        // slave (a wait on its status) must not run inside the receive interrupt: SysTick cannot preempt it, so a wait would never end, and every watchdog and safety tick of
        // the main loop would stand still for the whole check. SlaveBridge_Service does the work in the main loop. A second end frame while one waits is dropped.
        if (!slave_end_pending) {
            memcpy((void *)slave_end_data, rxData, 8);
            slave_end_pending = 1;
        }

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_STATUS) {
        // Status query - request can arrive with any DLC (same "DLC:any"
        // convention as the main board's own 0x7F8), response is always
        // 1 byte.
        uint8_t status;
        if (digest_chunk_lost || end_refused_busy) {
            status = URTC_STATUS_VERIFY_FAIL;
            SendCanResponse(URTC_CAN_ID_SLAVE_STATUS, &status, 1);
        } else if (ExpansionI2C_SlaveReadRegister(REG_BOOT_STATUS, &status, 1)) {
            SendCanResponse(URTC_CAN_ID_SLAVE_STATUS, &status, 1);
        }
        // A failed I2C transaction (slave not present/not responding,
        // e.g. this is actually a BASIC expansion board with no slave
        // chip at all) gets no response sent at all, same as this
        // project's other passthrough commands - the Flasher's own
        // timeout is what surfaces that to the person, not a fabricated
        // error byte pretending to be real slave data.

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_PROGRESS) {
        uint8_t progress;
        if (ExpansionI2C_SlaveReadRegister(REG_BOOT_PROGRESS, &progress, 1)) {
            SendCanResponse(URTC_CAN_ID_SLAVE_PROGRESS, &progress, 1);
        }

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_VERSION_RESP_1) {
        // Bootloader version query - REG_QUERY_VERSION's own 10-byte
        // response doesn't fit one CAN frame, split across 0x217 (first
        // 8 bytes) and 0x218 (last 2) - same 2-response-ID split the
        // main board's own bootloader version query uses (0x7F9/0x7FA),
        // for the same reason.
        uint8_t version[10];
        // Only an answer that starts with the bootloader marker is a version: a slave that runs its application (or a chip that answers something else)
        // gets no 0x217 / 0x218, the same silence as a slave that does not answer at all.
        if (ExpansionI2C_SlaveReadRegister(REG_BOOT_QUERY_VERSION, version, 10) && version[0] == SLAVE_BOOT_VERSION_MARKER) {
            SendCanResponse(URTC_CAN_ID_SLAVE_VERSION_RESP_1, version, 8);
            // Optional PATCH extension: a slave bootloader that has it answers
            // REG_BOOT_QUERY_VERSION_EXT starting with a marker byte; an older
            // one answers with its 1-byte status instead, so the first byte is
            // read on its own before trusting the rest. 0x218 then carries 5
            // bytes, otherwise exactly the 2 legacy bytes.
            uint8_t ext[URTC_SLAVE_VERSION_EXT_LEN] = {0};
            uint8_t have_ext = 0;
            if (ExpansionI2C_SlaveReadRegister(REG_BOOT_QUERY_VERSION_EXT, ext, 1) &&
                ext[0] == URTC_SLAVE_VERSION_EXT_MARKER) {
                have_ext = ExpansionI2C_SlaveReadRegister(REG_BOOT_QUERY_VERSION_EXT, ext, URTC_SLAVE_VERSION_EXT_LEN);
            }
            uint8_t part2[5];
            uint8_t part2_len = urtc_slave_version_part2_build(part2, &version[8], have_ext ? ext : 0);
            SendCanResponse(URTC_CAN_ID_SLAVE_VERSION_RESP_2, part2, part2_len);
        }

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_VERIFY_FAIL_REASON) {
        // Verify-fail-reason query - only meaningful right after 0x215
        // above reports STATUS_VERIFY_FAIL; same "any DLC request, 1 byte
        // response" convention as 0x215/0x216.
        uint8_t reason;
        if (digest_chunk_lost) {
            reason = URTC_SLAVE_VERIFY_FAIL_REASON_DIGEST_CHUNK_MISSING;
            SendCanResponse(URTC_CAN_ID_SLAVE_VERIFY_FAIL_REASON, &reason, 1);
        } else if (end_refused_busy) {
            reason = URTC_SLAVE_VERIFY_FAIL_REASON_ACTUATOR_ACTIVE;
            SendCanResponse(URTC_CAN_ID_SLAVE_VERIFY_FAIL_REASON, &reason, 1);
        } else if (ExpansionI2C_SlaveReadRegister(REG_BOOT_VERIFY_FAIL_REASON, &reason, 1)) {
            SendCanResponse(URTC_CAN_ID_SLAVE_VERIFY_FAIL_REASON, &reason, 1);
        }

    // ---- Application register access (advanced-board tools, e.g. Paste
    // Jetting's own local PWM) ----
    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_REG_WRITE && rxHeader.DLC >= 2) {
        // Write - byte[0] is the slave's own application register
        // number (REG_PWM_*, REG_ADS_*, REG_MLX_* - see slave_common.h),
        // byte[1..] is that register's own payload, relayed as-is.
        // Only the registers and payload lengths listed in urtc_slave_regs.h
        // are relayed; anything else is dropped without an answer.
        if (urtc_slave_app_write_allowed(rxData[0], (uint8_t)(rxHeader.DLC - 1))) {
            ExpansionI2C_AppWriteRegister(rxData[0], &rxData[1], (uint8_t)(rxHeader.DLC - 1));
        }

    } else if (rxHeader.StdId == URTC_CAN_ID_SLAVE_REG_READ && rxHeader.DLC == 2) {
        // Read - byte[0] is the register to read, byte[1] is how many
        // bytes to read back (caller's own responsibility to ask for the
        // right length for whichever register it named - this bridge
        // doesn't maintain a length table per register the way it could,
        // since the caller already knows this from slave_common.h same
        // as this file does).
        uint8_t reg = rxData[0];
        uint8_t len = rxData[1];
        if (!urtc_slave_app_read_allowed(reg, len)) return; // not in the closed list
        if (len > 8) len = 8; // one CAN response frame's own ceiling
        // The version register is read whole: its ninth byte (the low byte of
        // the minor number) and its tenth (the PATCH number of the slave
        // application) do not fit the answer frame and travel in the 0x222
        // companion instead.
        uint8_t response[URTC_SLAVE_APP_VERSION_LEN > 8 ? URTC_SLAVE_APP_VERSION_LEN : 8];
        uint8_t is_version = (reg == URTC_SLAVE_APP_VERSION_REG);
        if (ExpansionI2C_AppReadRegister(reg, response, is_version ? (uint8_t)URTC_SLAVE_APP_VERSION_LEN : len)) {
            SendCanResponse(URTC_CAN_ID_SLAVE_REG_READ, response, len);
            if (is_version) {
                // Accepted when the slave sends one (1..127); a slave application
                // from before the field reads 0 there, which is ignored, so the
                // answer to such a slave is the same as it always was.
                uint8_t patch = urtc_slave_app_patch_from_version(response);
                if (patch != URTC_PATCH_UNKNOWN) {
                    uint8_t companion[URTC_SLAVE_APP_PATCH_FRAME_DLC];
                    urtc_slave_app_patch_frame_build(companion, patch, response[8]);
                    SendCanResponse(URTC_CAN_ID_SLAVE_APP_PATCH, companion, URTC_SLAVE_APP_PATCH_FRAME_DLC);
                }
            }
        }
    }
}

#if URTC_APP_VERSION_COMPAT_RELAY
static const uint8_t *vc_page_ptr(void *ctx, uint8_t page) {
    (void)ctx;
    return (const uint8_t *)(uintptr_t)(page == 0u ? URTC_MASTER_METADATA_ADDR : URTC_MASTER_METADATA_B_ADDR);
}

static void RelayVersionRule(void) {
    const UrtcMetaAbOps ops = { vc_page_ptr, 0, 0, 0 };   // read only: urtc_vc_annex_load never erases or programs
    uint32_t mode, counter;
    urtc_vc_annex_load(&ops, &mode, &counter);
    if (mode == URTC_VC_MODE_ACCEPT_PATCHLESS) {
        uint8_t accept = URTC_VC_SLAVE_ACCEPT;
        ExpansionI2C_SlaveWriteRegister(REG_BOOT_VERSION_COMPAT, &accept, 1);   // a slave that does not know the register ignores it and stays strict
    }
}
#endif

// Main loop: the part of the end of a slave update that waits or takes long. The signature record is checked BEFORE the END frame reaches the slave (the slave waits only ten
// seconds for the confirmation, and the check takes a while on this chip); the verdict decides whether the slave is then told to install.
void SlaveBridge_Service(void) {
    if (!slave_end_pending) return;
    if (actuators_off_probe && !actuators_off_probe() && !(allow_active_actuator && allow_active_actuator())) {
        // A tool is active: nothing is verified, forwarded or confirmed (the slave stays as it is and times out on its own); 0x215 / 0x219 say why until the next start.
        end_refused_busy = 1;
        slave_end_pending = 0;
        return;
    }
    uint8_t data[8];
    memcpy(data, (const void *)slave_end_data, 8);
    uint32_t end_crc = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    uint16_t end_major = (uint16_t)(((uint16_t)data[4] << 8) | data[5]);
    uint16_t end_minor = (uint16_t)(((uint16_t)data[6] << 8) | data[7]);
    uint8_t vouched = SlaveAttest_Verify(end_crc, end_major, end_minor);
    if (vouched) {
        // The signed counter of the record, before the END frame: the slave binds its check to it. A counter that could not be delivered means no vouching.
        uint32_t counter = SlaveAttest_Counter();
        uint8_t counter_bytes[4] = { (uint8_t)(counter >> 24), (uint8_t)(counter >> 16), (uint8_t)(counter >> 8), (uint8_t)counter };
        if (!ExpansionI2C_SlaveWriteRegister(REG_BOOT_RECORD_COUNTER, counter_bytes, 4)) vouched = 0;
    }
#if URTC_APP_VERSION_COMPAT_RELAY
    if (vouched) RelayVersionRule();   // before END: the slave judges the version when it receives it
#endif
    ExpansionI2C_SlaveWriteRegister(REG_BOOT_END_UPDATE, data, 8);
    if (vouched) {
        ConfirmToSlave();
    }
    slave_end_pending = 0;   // after the work: a start that cancelled it meanwhile has already cleared it
}
