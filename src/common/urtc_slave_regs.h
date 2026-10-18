// =============================================================================
// URTC - closed list of the expansion slave's application registers that the
// CAN bridge (0x220 write, 0x221 read) may reach. Plain C99 with no STM32 HAL
// dependency, so tests/test_can_slavebridge.c exercises exactly the code that
// runs on the main board.
//
// Anything that is not in these tables is refused by the bridge: in
// particular REG_ENTER_BOOTLOADER (0x02) is never reachable from the generic
// frames (the bootloader entry has its own magic-checked path), and neither
// are registers that do not exist yet. Writes need the exact payload length of
// the register; reads need 1..max bytes. Keep the numbers and lengths in sync
// with src/F303-slave/slave_common.h and docs/CANBUS.TXT (0x220 / 0x221).
// =============================================================================
#ifndef URTC_SLAVE_REGS_H
#define URTC_SLAVE_REGS_H

#include <stdint.h>

typedef struct {
    uint8_t reg;
    uint8_t len;   // writes: exact payload length; reads: maximum length
} UrtcSlaveRegRule;

static const UrtcSlaveRegRule URTC_SLAVE_WRITE_RULES[] = {
    { 0x10, 0 },   // REG_MLX_TRIGGER_CAPTURE
    { 0x12, 1 },   // REG_MLX_RAW_CHUNK (chunk index)
    { 0x13, 1 },   // REG_MLX_CALIBRATED_CHUNK (chunk index)
    { 0x14, 1 },   // REG_MLX_SENSOR_VARIANT
    { 0x20, 2 },   // REG_ADS_CONFIGURE
    { 0x21, 0 },   // REG_ADS_TRIGGER
    { 0x30, 3 },   // REG_PWM_CONFIGURE
    { 0x31, 3 },   // REG_PWM_PULSE
    { 0x32, 1 },   // REG_PWM_STOP
};

static const UrtcSlaveRegRule URTC_SLAVE_READ_RULES[] = {
    { 0x00, 1 },   // REG_APP_STATUS
    { 0x01, 10 },  // REG_APP_VERSION
    { 0x11, 1 },   // REG_MLX_CAPTURE_STATUS
    { 0x12, 32 },  // REG_MLX_RAW_CHUNK
    { 0x13, 32 },  // REG_MLX_CALIBRATED_CHUNK
    { 0x14, 1 },   // REG_MLX_SENSOR_VARIANT
    { 0x22, 2 },   // REG_ADS_READ
};

static inline uint8_t urtc_slave_app_write_allowed(uint8_t reg, uint8_t len)
{
    for (unsigned i = 0; i < sizeof URTC_SLAVE_WRITE_RULES / sizeof URTC_SLAVE_WRITE_RULES[0]; i++) {
        if (URTC_SLAVE_WRITE_RULES[i].reg == reg) return len == URTC_SLAVE_WRITE_RULES[i].len;
    }
    return 0;
}

static inline uint8_t urtc_slave_app_read_allowed(uint8_t reg, uint8_t len)
{
    if (len == 0) return 0;
    for (unsigned i = 0; i < sizeof URTC_SLAVE_READ_RULES / sizeof URTC_SLAVE_READ_RULES[0]; i++) {
        if (URTC_SLAVE_READ_RULES[i].reg == reg) return len <= URTC_SLAVE_READ_RULES[i].len;
    }
    return 0;
}

#endif
