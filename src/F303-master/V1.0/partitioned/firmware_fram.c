// =============================================================================
// URTC Firmware - Low-level F-RAM I2C read/write (FM24CL64B)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_fram.h"


// =============================================================================
// 14. PARAMETER PERSISTENCE (FM24CL64B F-RAM, SHARED I2C2)
// =============================================================================
// Saves a snapshot of the active tool's setpoints and the global LED/OLED
// settings periodically, so a sudden power loss doesn't leave the state
// "before" a shutdown as unknowable as the shutdown itself was unplanned.
// Deliberately does NOT auto-apply a recovered setpoint to anything that
// could re-energize on its own after boot (a heater, the laser, a spinning
// motor) - see SavedState_Load()'s own comment for the full reasoning.
// This is a diagnostic/recovery aid, not a "resume exactly where it left
// off" feature.

// Low-level chunked write. addr/len are trusted to be within
// FRAM_SIZE_BYTES - this is an internal helper, not exposed over CAN,
// so the only caller is this same file. Returns 1 on success, 0 on any
// HAL error (I2C bus fault, chip not present/not acking, etc.) - every
// caller treats a failure as "couldn't save this time, try again later"
// rather than anything fatal, since nothing about this board's core
// operation depends on this chip being present or working.
uint8_t FRAM_WriteBytes(uint16_t addr, const uint8_t *data, uint16_t len) {
    while (len > 0) {
        uint16_t space_in_chunk = FRAM_WRITE_CHUNK - (addr % FRAM_WRITE_CHUNK);
        uint16_t chunk = (len < space_in_chunk) ? len : space_in_chunk;
        // HAL_I2C_Mem_Write handles the 2-byte (16-bit) internal memory
        // address this chip's 8KB capacity needs (MEMADD_SIZE_16BIT) -
        // confirmed against the FM24CL64B's own documented control-byte
        // + address-byte protocol, the same scheme a serial EEPROM of the
        // same capacity would use (this chip is a drop-in replacement by
        // design). No delay follows the write below: F-RAM writes
        // complete at bus speed with no internal write cycle to wait for
        // ("NoDelay" writes, per the datasheet - unlike a real EEPROM,
        // there's nothing here to poll or wait out).
        if (HAL_I2C_Mem_Write(&hi2c2, FRAM_I2C_ADDR, addr, I2C_MEMADD_SIZE_16BIT,
                               (uint8_t *)data, chunk, 50) != HAL_OK) {
            return 0;
        }
        addr += chunk;
        data += chunk;
        len -= chunk;
    }
    return 1;
}

// Low-level read - one single transaction regardless of len, same as a
// write would be if FRAM_WRITE_CHUNK's conservative chunking weren't
// applied to it (reads never needed that chunking to begin with, on
// this chip or a real EEPROM either).
uint8_t FRAM_ReadBytes(uint16_t addr, uint8_t *data, uint16_t len) {
    return HAL_I2C_Mem_Read(&hi2c2, FRAM_I2C_ADDR, addr, I2C_MEMADD_SIZE_16BIT,
                             data, len, 50) == HAL_OK;
}

