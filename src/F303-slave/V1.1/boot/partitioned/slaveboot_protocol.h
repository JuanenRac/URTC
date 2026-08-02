// =============================================================================
// URTC Expansion Slave Bootloader - I2C-link protocol handlers, application
// validation, and jump declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef SLAVEBOOT_PROTOCOL_H
#define SLAVEBOOT_PROTOCOL_H

#include <stdint.h>

// Defined in slaveboot_protocol.c, updated throughout the update state
// machine. Read by the I2C1 slave read-path in slaveboot_main.c whenever
// the link-bus master polls REG_STATUS.
extern volatile uint8_t current_status;

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

// Called from the I2C1 slave event interrupt handler when the link-bus
// master reads REG_QUERY_VERSION - fills out[10] with this bootloader's
// answer (see REG_QUERY_VERSION's own field layout comment in
// slaveboot_common.h) rather than transmitting anything itself; I2C slave
// reads are inherently master-paced; the peripheral clocks this buffer
// out one byte at a time as the master's own read continues.
void BuildVersionQueryResponse(uint8_t out[10]);

#endif // SLAVEBOOT_PROTOCOL_H
