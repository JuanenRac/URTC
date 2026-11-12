// =============================================================================
// URTC - the check byte of the application link on the expansion connector (I2C1, master <-> expansion slave)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The register protocol of the link has no check: a bit flipped on the wire by noise on the cable is a different register number or a different PWM duty
// that nobody notices. This header adds one CRC-8 byte (polynomial 0x07, initial value 0, no reflection, no final XOR: the CRC-8 of ATM/SMBus, whose check
// value for the text "123456789" is 0xF4) to the application protocol, when both sides are built with URTC_SLAVE_LINK_CRC=1 (default 0: the link is then
// byte for byte what it always was).
//
//   write   register | payload ... | crc8(register | payload ...)         the slave drops a write whose check does not match and counts it
//   read    the master selects the register with a write (itself checked), then reads  data ... | crc8(register | data ...)
//
// The register is part of what the check covers on a read, so an answer that belongs to another register (a stale selection) does not pass. The
// bootloader of the slave keeps its own protocol, which has no check: the master must use the checked calls only with the application running. Pure logic,
// no HAL, shared by the master driver and the slave link.
// =============================================================================
#ifndef URTC_LINK_CRC_H
#define URTC_LINK_CRC_H

#include <stdint.h>

#define URTC_LINK_CRC_POLY 0x07u
#define URTC_LINK_CRC_MAX_FRAME 33u    // the longest frame with its check byte: 32 data bytes of a thermal chunk + the check

static inline uint8_t urtc_link_crc8(uint8_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) crc = (uint8_t)((crc & 0x80u) ? (((unsigned)crc << 1) ^ URTC_LINK_CRC_POLY) : ((unsigned)crc << 1));
    }
    return crc;
}

// frame[0..len-1] is a complete write (register and payload); the check byte is written at frame[len] and the new length (len + 1) is returned.
static inline uint8_t urtc_link_crc_seal(uint8_t *frame, uint8_t len)
{
    frame[len] = urtc_link_crc8(0, frame, len);
    return (uint8_t)(len + 1u);
}

// frame[0..len-1] is a write that ends with its check byte; returns the length without the check byte, or 0 when the frame is too short or the check does not match.
static inline uint8_t urtc_link_crc_open(const uint8_t *frame, uint8_t len)
{
    if (len < 2u) return 0;
    return urtc_link_crc8(0, frame, (uint32_t)len - 1u) == frame[len - 1u] ? (uint8_t)(len - 1u) : 0;
}

// The check byte of an answer: it covers the register that was selected and the data.
static inline uint8_t urtc_link_crc_answer(uint8_t reg, const uint8_t *data, uint8_t len)
{
    return urtc_link_crc8(urtc_link_crc8(0, &reg, 1), data, len);
}

#endif // URTC_LINK_CRC_H
