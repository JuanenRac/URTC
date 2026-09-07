// =============================================================================
// URTC Firmware - CONN_EXPANSION bit-banged SPI bus declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_EXPANSION_SPI_H
#define FIRMWARE_EXPANSION_SPI_H

#include <stdint.h>

// Bit-banged SPI Mode 3 transfer for CONN_EXPANSION. Caller handles
// asserting/releasing EXP_SPI_CS_PIN - this only shifts one byte.
uint8_t ExpansionSPI_TransferByte(uint8_t tx_byte);

void MX_ExpansionSPI_Init(void);

#endif // FIRMWARE_EXPANSION_SPI_H
