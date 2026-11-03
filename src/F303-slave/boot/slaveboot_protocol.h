// =============================================================================
// URTC Expansion Slave Bootloader - I2C-link protocol handlers, application
// validation, and jump declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef SLAVEBOOT_PROTOCOL_H
#define SLAVEBOOT_PROTOCOL_H

#include <stdint.h>

// Defined in slaveboot_protocol.c, updated throughout the update state
// machine. Read by the I2C1 slave read-path in slaveboot_main.c whenever
// the link-bus master polls REG_STATUS.
extern volatile uint8_t current_status;
extern uint8_t  update_in_progress;
extern uint8_t  update_failed;
// HAL_GetTick() timestamp of the last byte/register write belonging to an
// in-progress update - read by slaveboot_main.c's own main loop to abort
// back to STATUS_LISTENING after too long without one, same reasoning and
// timeout as the main board's own bootloader_main.c.
extern uint32_t update_last_activity_tick;

// Defined here, read by the I2C1 interrupt handler's REG_VERIFY_FAIL_REASON
// read path - set alongside current_status whenever it's assigned
// STATUS_VERIFY_FAIL, one of the VERIFY_FAIL_REASON_* values from
// slaveboot_common.h. Stale (holds the previous update's reason) until the
// next STATUS_VERIFY_FAIL, same as REG_PROGRESS is stale until the next
// update starts - only meaningful right after REG_STATUS itself reports
// STATUS_VERIFY_FAIL.
extern volatile uint8_t verify_fail_reason;

uint8_t ApplicationIsValid(void);
void JumpToApplication(void);

// Called from the I2C1 slave event interrupt handler (see slaveboot_main.c)
// when the link-bus master (main board) writes to the corresponding
// REG_* register. data points into the I2C peripheral's own receive
// buffer for the duration of the call; len is however many bytes the
// master actually sent in that transaction (REG_DATA's len varies
// transaction to transaction, up to 32; every other register has a fixed
// length already documented alongside its REG_* definition in
// slaveboot_common.h).
void HandleStartUpdate(uint8_t *data);
void HandleHmacExpected(uint8_t *data);
void HandleData(uint8_t *data, uint32_t len);
void HandleEndUpdate(uint8_t *data);
// REG_CONFIRM, 32 bytes: the master's confirmation (urtc_slave_confirm.h). Only acted on while update_awaiting_confirm is set; it is used once whatever the
// outcome, so a wrong tag fails the update at once.
void HandleConfirm(uint8_t *data);
// REG_RECORD_COUNTER, 4 bytes big-endian: the signed counter of the record the master verified. Before the END frame only.
void HandleRecordCounter(uint8_t *data);
// The rollback floor (REG_COUNTER_FLOOR) and its 4-byte big-endian answer.
uint32_t Counter_Floor(void);
void BuildCounterFloorResponse(uint8_t out[4]);
// 1 between a verified END_UPDATE and the confirmation (or the failure). The main loop of slaveboot_main.c gives up on it after ten seconds with
// AbortUnconfirmed(): fail closed, reason VERIFY_FAIL_REASON_NOT_CONFIRMED, main slot untouched.
extern uint8_t update_awaiting_confirm;
void AbortUnconfirmed(void);
// REG_CHALLENGE: the 16-byte nonce of the session in progress, or zeros when there is none.
void BuildChallengeResponse(uint8_t out[16]);

// Called from the I2C1 slave event interrupt handler when the link-bus
// master reads REG_QUERY_VERSION - fills out[10] with this bootloader's
// answer (see REG_QUERY_VERSION's own field layout comment in
// slaveboot_common.h) rather than transmitting anything itself; I2C slave
// reads are inherently master-paced; the peripheral clocks this buffer
// out one byte at a time as the master's own read continues.
void BuildVersionQueryResponse(uint8_t out[10]);

// Fills out[4] for REG_QUERY_VERSION_EXT: marker, installed application
// PATCH, this bootloader's MINOR and PATCH.
void BuildVersionExtResponse(uint8_t out[4]);

#endif // SLAVEBOOT_PROTOCOL_H
