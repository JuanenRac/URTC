// =============================================================================
// URTC Bootloader - CAN update state machine and application-jump declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef BOOTLOADER_PROTOCOL_H
#define BOOTLOADER_PROTOCOL_H

#include <stdint.h>

// These 5 are read (and, for the inactivity-timeout abort, written)
// directly by main()'s own heartbeat-progress and timeout logic -
// genuinely shared state, not just accessed through the functions below.
extern uint32_t update_total_size;
extern uint32_t update_bytes_received;
extern uint8_t  update_in_progress;
extern uint32_t update_last_activity_tick;
extern uint8_t  update_failed;

// Sent BY this bootloader in response to a CAN command - status codes,
// see bootloader_common.h's STATUS_* / VERIFY_FAIL_REASON_* defines.
void CAN_SendStatus(uint8_t status);

// Sent every ~1s while listening or mid-update, so the master can tell
// "node is alive but hasn't started listening yet" apart from "node is
// completely unresponsive".
void CAN_SendHeartbeat(uint8_t status, uint8_t progress_percent);

// Answers CAN_ID_QUERY_VERSION (0x7F8) with this bootloader's own identity
// (0x7F9 + 0x7FA) - read-only, answered even during a declared error.
void HandleVersionQuery(void);
void HandleErrorCounterQuery(void);

// Checks the main slot's metadata (size/CRC32/HMAC/HardwareID) against its
// actual flash contents. Also handles first-boot bootstrapping: a blank
// metadata page with a plausible-looking application already in the main
// slot gets its metadata reconstructed and adopted, rather than being
// rejected outright.
uint8_t ApplicationIsValid(void);

// Validates the main slot's own vector table (stack alignment, Thumb bit,
// reset vector range) before handing control to it. Returns to the
// caller (not the application) if any check fails, leaving CAN available
// to fall back to listening with.
void JumpToApplication(void);

// CAN_ID_START_UPDATE (0x7F1) - begins a new update: erases the backup
// slot, resets all per-update state.
void HandleStartUpdate(uint8_t *data);

// CAN_ID_HMAC_CHUNK (0x7F7) - receives one of the 4 8-byte chunks making
// up the expected 32-byte HMAC-SHA256 for this update.
void HandleHmacChunk(uint8_t *data);

// CAN_ID_DATA (0x7F2) - receives up to 8 bytes of firmware data, buffered
// and written to the backup slot one page at a time.
void HandleData(uint8_t *data, uint32_t dlc);

// CAN_ID_END_UPDATE (0x7F4) - verifies size/CRC32/HMAC-SHA256/HardwareID/
// anti-rollback on the backup slot, and if all pass, copies it into the
// main slot and resets into the new application.
void HandleEndUpdate(uint8_t *data);

#endif // BOOTLOADER_PROTOCOL_H
