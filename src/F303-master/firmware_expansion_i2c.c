// =============================================================================
// URTC Firmware - CONN_EXPANSION bit-banged I2C
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Bit-banged I2C master for CONN_EXPANSION's own bus (PB10=SCL, PB11=SDA).
// Not a hardware I2C peripheral: confirmed against DS9118 Tables 14/15
// that neither pin actually offers an I2C alternate function on this
// chip (the only 3 real hardware-I2C1 pin-pairs - PA14/15, PB6/7, PB8/9 -
// are all already committed to other functions on this design, with no
// free pins to relocate anything to). Bit-banging sidesteps the
// alternate-function requirement entirely - these pins just need to be
// plain open-drain GPIO, which every pin supports regardless of AF
// capability. Same electrical requirements as any I2C bus either way:
// R44/R48 (4.7k pull-ups to +3.3V) are already populated on both lines.
// ============================================================================
#include "firmware_common.h"
#include "firmware_expansion_i2c.h"

// Address the expansion slave chip (STM32F303CBT6, advanced boards only)
// answers on this same physical bus, in BOTH its bootloader and
// application firmware (see slaveboot_common.h / slave_common.h's own
// I2C_SLAVE_ADDRESS - one chip, one address, whichever firmware happens
// to be running at the time).
#define EXPANSION_SLAVE_I2C_ADDR 0x42

// Standard Mode timing (~100kHz, matching this project's hardware I2C2
// bus for the OLED/F-RAM) - roughly 5us per half-bit-period. Not
// cycle-counted precisely (unlike the WS2812B driver, which is
// time-triggered against a protocol-defined bit period); I2C is
// edge-triggered from the slave's perspective, so a plain small delay
// between edges is sufficient and correct, same reasoning as the
// expansion SPI driver. 320 iterations, not 40: confirmed (compiled to
// assembly and compared instruction-for-instruction, not just estimated)
// that this loop's own per-iteration cost is identical to the T12
// thermal loop's 640-iteration/~10us delay elsewhere in this firmware -
// 40 iterations here was actually producing ~0.625us, not ~5us,
// pushing this bus toward ~800kHz instead of the intended 100kHz. 320
// (640 * 5/10) targets the actual documented ~5us.
static void ExpansionI2C_Delay(void) {
    for (volatile uint32_t d = 0; d < 320; d++);
}

// Open-drain "write": SET releases the line (pulled high externally by
// R44/R48), RESET actively pulls it low. Never actually drives high -
// matches real I2C's wired-AND requirement, where two devices pulling
// opposite directions on the same line at once would short the bus if
// either one drove a real high/low push-pull output instead.
static void ExpansionI2C_SetSCL(uint8_t high) {
    HAL_GPIO_WritePin(EXP_I2C2_SCL_PORT, EXP_I2C2_SCL_PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static uint8_t ExpansionI2C_ReadSCL(void) {
    return (HAL_GPIO_ReadPin(EXP_I2C2_SCL_PORT, EXP_I2C2_SCL_PIN) == GPIO_PIN_SET) ? 1 : 0;
}
// Set by ExpansionI2C_SetSCL_WaitHigh() whenever it times out, cleared at
// the start of every transaction - not yet consumed by any caller (this
// whole API isn't wired to a CAN command yet, unlike the expansion SPI's
// own 0x180/0x181 passthrough), but every high-level function below was
// silently ignoring this function's own return value, meaning a stuck-low
// SCL (a shorted bus, or a slave stretching indefinitely) would have gone
// completely unnoticed - the master would sample/drive SDA as if the
// clock edge had genuinely happened, on a bus that was never actually
// ready. Whenever this API is wired to something, check this flag after
// the transaction completes rather than trusting the data unconditionally.
static uint8_t expansion_i2c_scl_timeout = 0;
uint8_t ExpansionI2C_HadTimeout(void) {
    return expansion_i2c_scl_timeout;
}

// Releases SCL high, then waits (bounded) for it to actually read high
// before returning - a real I2C slave is allowed to hold SCL low itself
// to pause the transaction (clock stretching, a standard part of the I2C
// spec, not an error condition). Skipping this wait and assuming SCL
// went high the instant it's released would let this master sample/
// change SDA while a stretching slave still has the clock held down,
// corrupting the transfer.
static uint8_t ExpansionI2C_SetSCL_WaitHigh(void) {
    ExpansionI2C_SetSCL(1);
    for (uint16_t timeout = 0; timeout < 2000; timeout++) {
        if (ExpansionI2C_ReadSCL()) return 1;
    }
    expansion_i2c_scl_timeout = 1; // SCL never actually went high - a stuck slave or a wiring fault
    return 0;
}
static void ExpansionI2C_SetSDA(uint8_t high) {
    HAL_GPIO_WritePin(EXP_I2C2_SDA_PORT, EXP_I2C2_SDA_PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static uint8_t ExpansionI2C_ReadSDA(void) {
    return (HAL_GPIO_ReadPin(EXP_I2C2_SDA_PORT, EXP_I2C2_SDA_PIN) == GPIO_PIN_SET) ? 1 : 0;
}

// START condition: SDA falls while SCL is high. Caller's responsibility
// to have both lines idling high beforehand (the reset/idle state).
void ExpansionI2C_Start(void) {
    expansion_i2c_scl_timeout = 0; // fresh transaction, fresh timeout state
    ExpansionI2C_SetSDA(1);
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_Delay();
    ExpansionI2C_SetSDA(0);
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL(0);
    ExpansionI2C_Delay();
}

// STOP condition: SDA rises while SCL is high.
void ExpansionI2C_Stop(void) {
    ExpansionI2C_SetSDA(0);
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_Delay();
    ExpansionI2C_SetSDA(1);
    ExpansionI2C_Delay();
}

// Clocks out 8 bits (MSB first) then reads the 9th (ACK/NACK) bit.
// Returns 1 if the slave ACKed (pulled SDA low), 0 for NACK/no response -
// matching how a real hardware I2C peripheral's status flag would read.
uint8_t ExpansionI2C_WriteByte(uint8_t byte) {
    for (int8_t i = 7; i >= 0; i--) {
        ExpansionI2C_SetSDA((byte >> i) & 0x01);
        ExpansionI2C_Delay();
        ExpansionI2C_SetSCL_WaitHigh();
        ExpansionI2C_Delay();
        ExpansionI2C_SetSCL(0);
        ExpansionI2C_Delay();
    }
    // Release SDA so the slave can drive it for the ACK bit.
    ExpansionI2C_SetSDA(1);
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_Delay();
    uint8_t ack = (ExpansionI2C_ReadSDA() == 0) ? 1 : 0;
    ExpansionI2C_SetSCL(0);
    ExpansionI2C_Delay();
    return ack;
}

// Clocks in 8 bits (MSB first), then sends ACK (send_ack=1, more bytes
// to follow) or NACK (send_ack=0, this is the last byte being read).
uint8_t ExpansionI2C_ReadByte(uint8_t send_ack) {
    uint8_t byte = 0;
    ExpansionI2C_SetSDA(1); // release - let the slave drive each data bit
    for (uint8_t i = 0; i < 8; i++) {
        ExpansionI2C_Delay();
        ExpansionI2C_SetSCL_WaitHigh();
        ExpansionI2C_Delay();
        byte = (byte << 1) | ExpansionI2C_ReadSDA();
        ExpansionI2C_SetSCL(0);
    }
    ExpansionI2C_SetSDA(send_ack ? 0 : 1);
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_Delay();
    ExpansionI2C_SetSCL(0);
    ExpansionI2C_SetSDA(1); // release again for whatever comes next
    ExpansionI2C_Delay();
    return byte;
}

void MX_ExpansionI2C_Init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_OD; // open-drain, matching I2C's wired-AND bus
    gpio.Pull = GPIO_NOPULL; // R44/R48 already provide the real pull-ups externally
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    // Idle state: both lines released high.
    ExpansionI2C_SetSCL_WaitHigh();
    ExpansionI2C_SetSDA(1);
}

// -----------------------------------------------------------------------
// Slave register access - the master-side half of the CAN-to-I2C bridge
// (see firmware_can_slavebridge.c) that lets the Flasher's own CAN-OTA
// protocol and any application-mode command reach the expansion slave
// chip, which has no CAN peripheral of its own. Same "write register,
// then read" pattern the slave's own bootloader/application protocol
// documents (slaveboot_common.h / slave_common.h) - a 1-byte write
// selects which register a following read serves, matching how every
// other I2C peripheral in this project (F-RAM, the ADS1115 this same
// slave chip itself talks to on its own local bus) already works.
// -----------------------------------------------------------------------

// Writes len bytes to the given register in one I2C transaction (register
// byte, then len data bytes, one STOP). Matches how the slave's own
// listen-mode handler expects a register write to arrive - see
// slaveboot_main.c/slave_main.c's own HAL_I2C_SlaveRxCpltCallback.
uint8_t ExpansionI2C_SlaveWriteRegister(uint8_t reg, const uint8_t *data, uint8_t len) {
    ExpansionI2C_Start();
    uint8_t ack = ExpansionI2C_WriteByte((EXPANSION_SLAVE_I2C_ADDR << 1) | 0);
    if (ack) ack = ExpansionI2C_WriteByte(reg);
    for (uint8_t i = 0; i < len && ack; i++) {
        ack = ExpansionI2C_WriteByte(data[i]);
    }
    ExpansionI2C_Stop();
    return ack && !ExpansionI2C_HadTimeout();
}

// Reads len bytes from the given register: a 1-byte write to select the
// register (same "pointer write" the slave's own protocol already uses
// for a bare 1-byte write), then a repeated START into a read of len
// bytes, ACKing every byte except the last.
uint8_t ExpansionI2C_SlaveReadRegister(uint8_t reg, uint8_t *data, uint8_t len) {
    ExpansionI2C_Start();
    uint8_t ack = ExpansionI2C_WriteByte((EXPANSION_SLAVE_I2C_ADDR << 1) | 0);
    if (ack) ack = ExpansionI2C_WriteByte(reg);
    if (!ack) {
        ExpansionI2C_Stop();
        return 0;
    }

    ExpansionI2C_Start(); // repeated START, same bus transaction
    ack = ExpansionI2C_WriteByte((EXPANSION_SLAVE_I2C_ADDR << 1) | 1);
    if (ack) {
        for (uint8_t i = 0; i < len; i++) {
            data[i] = ExpansionI2C_ReadByte(i < (len - 1)); // ACK every byte but the last
        }
    }
    ExpansionI2C_Stop();
    return ack && !ExpansionI2C_HadTimeout();
}

// Same as ExpansionI2C_SlaveReadRegister above, but for registers whose
// own read-pointer selection needs a 2-byte write (register + one
// parameter byte) rather than the register alone - specifically the
// slave's own REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK (chunk index
// as the parameter), the one place in this project's I2C protocols that
// needs this - see slave_common.h's own note on why those 2 registers
// are the exception to the plain "write register, then read" pattern
// everything else here follows.
uint8_t ExpansionI2C_SlaveReadRegisterWithParam(uint8_t reg, uint8_t param, uint8_t *data, uint8_t len) {
    ExpansionI2C_Start();
    uint8_t ack = ExpansionI2C_WriteByte((EXPANSION_SLAVE_I2C_ADDR << 1) | 0);
    if (ack) ack = ExpansionI2C_WriteByte(reg);
    if (ack) ack = ExpansionI2C_WriteByte(param);
    if (!ack) {
        ExpansionI2C_Stop();
        return 0;
    }

    ExpansionI2C_Start();
    ack = ExpansionI2C_WriteByte((EXPANSION_SLAVE_I2C_ADDR << 1) | 1);
    if (ack) {
        for (uint8_t i = 0; i < len; i++) {
            data[i] = ExpansionI2C_ReadByte(i < (len - 1));
        }
    }
    ExpansionI2C_Stop();
    return ack && !ExpansionI2C_HadTimeout();
}


