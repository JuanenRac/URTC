// =============================================================================
// URTC Firmware - a device-level model of the bit-banged I2C bus of the expansion connector, for host-side tests: fake_i2c_bus.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include <string.h>

#include "firmware_common.h"
#include "fake_i2c_bus.h"

enum { S_IDLE, S_RX_BITS, S_SLAVE_ACK, S_TX_BITS, S_MASTER_ACK, S_IGNORE };

FakeI2cBusStats fake_i2c_bus;
static FakeI2cDevice devices[4];
static int device_count;
static uint16_t mlx_memory[65536];

static struct {
    int scl, sda, pull;
    int state, nbits, tx_idx, master_ack, is_addr, reading;
    uint8_t shift, tx_byte;
    FakeI2cDevice *dev;
} line;

static int bus_sda(void) { return line.sda && !line.pull; }
static int bus_scl(void) { return line.scl && !fake_i2c_bus.stretch; }

#define MLX642_CONFIG_OPCODE 0x3A2E
#define MLX642_CMD_OPCODE 0x0180
#define MLX642_START_SYNC 0x0001
#define MLX642_FLAGS_ADDR 0x3C14
#define MLX642_READY 0x0100
#define MLX642_TO_ADDR 0x342C

static void mlx642_write(FakeI2cDevice *d, uint8_t byte)
{
    int n = d->wcount++;
    if (n < 2) { d->mptr = (d->mptr << 8) | byte; d->mptr &= 0xFFFF; return; }
    uint32_t head = d->mptr;
    if (head == MLX642_CONFIG_OPCODE) {
        static uint16_t address, data;
        if (n == 2) address = (uint16_t)(byte << 8);
        else if (n == 3) address |= byte;
        else if (n == 4) data = (uint16_t)(byte << 8);
        else if (n == 5) { data |= byte; d->words16[address] = data; d->config_writes++; }
    } else if (head == MLX642_CMD_OPCODE) {
        static uint16_t cmd;
        if (n == 2) cmd = (uint16_t)(byte << 8);
        else if (n == 3) {
            cmd |= byte;
            d->last_cmd = cmd;
            d->cmd_writes++;
            if (cmd == MLX642_START_SYNC && !d->ignore_sync) d->words16[MLX642_FLAGS_ADDR] |= MLX642_READY;
        }
    }
}

static void device_write(FakeI2cDevice *d, uint8_t byte)
{
    if (d->kind == FAKE_I2C_MLX642) { mlx642_write(d, byte); return; }
    if (d->kind == FAKE_I2C_MLX) {
        if (d->wcount == 0) d->mptr = (uint32_t)byte << 8;
        else if (d->wcount == 1) d->mptr |= byte;
        else if ((d->wcount & 1) == 0) d->wr_hi = byte;
        else { if ((int32_t)(d->mptr & 0xFFFF) != d->read_only_addr) d->words16[d->mptr & 0xFFFF] = (uint16_t)((d->wr_hi << 8) | byte); d->mptr++; }
        d->wcount++;
        return;
    }
    if (d->expect_ptr) { d->ptr = byte; d->expect_ptr = 0; d->half = 0; return; }
    if (d->kind == FAKE_I2C_MEM8) { d->mem[d->ptr++] = byte; return; }
    if (d->half == 0) { d->wr_hi = byte; d->half = 1; }
    else { d->words[d->ptr & 3] = (uint16_t)((d->wr_hi << 8) | byte); d->half = 0; }
}

static uint8_t device_read(FakeI2cDevice *d)
{
    if (d->kind == FAKE_I2C_MEM8) return d->mem[d->ptr++];
    if (d->kind == FAKE_I2C_MLX || d->kind == FAKE_I2C_MLX642) {
        uint16_t w = d->words16[d->mptr & 0xFFFF];
        if (d->half == 0) {
            if (d->kind == FAKE_I2C_MLX642 && (d->mptr & 0xFFFF) == MLX642_TO_ADDR) d->words16[MLX642_FLAGS_ADDR] &= (uint16_t)~MLX642_READY;
            d->half = 1;
            return (uint8_t)(w >> 8);
        }
        d->half = 0;
        d->mptr += d->kind == FAKE_I2C_MLX642 ? 2 : 1;      // the MLX90642 is addressed in bytes: a word is two addresses
        return (uint8_t)(w & 0xFF);
    }
    uint16_t w = d->words[d->ptr & 3];
    if (d->half == 0) { d->half = 1; return (uint8_t)(w >> 8); }
    d->half = 0;
    return (uint8_t)(w & 0xFF);
}

static void present_bit(void) { line.pull = !((line.tx_byte >> (7 - line.tx_idx)) & 1); }

static void begin_write(FakeI2cDevice *d)
{
    d->expect_ptr = d->kind == FAKE_I2C_MEM8 || d->kind == FAKE_I2C_WORD16;
    d->wcount = 0;
    d->half = 0;
    if (d->kind == FAKE_I2C_MLX642) d->mptr = 0;
}

static void byte_received(void)
{
    uint8_t byte = line.shift;
    if (fake_i2c_bus.byte_count < (int)sizeof fake_i2c_bus.seen) fake_i2c_bus.seen[fake_i2c_bus.byte_count] = byte;
    fake_i2c_bus.byte_count++;
    if (line.is_addr) {
        line.is_addr = 0;
        FakeI2cDevice *d = 0;
        for (int i = 0; i < device_count; i++) if (devices[i].present && devices[i].addr == (byte >> 1)) d = &devices[i];
        line.dev = d;
        line.reading = byte & 1;
        if (!d) { line.pull = 0; line.state = S_IGNORE; fake_i2c_bus.ack_nacks++; return; }
        if (!line.reading) begin_write(d);
        line.pull = 1;
        line.state = S_SLAVE_ACK;
        return;
    }
    device_write(line.dev, byte);
    line.pull = 1;
    line.state = S_SLAVE_ACK;
}

static void scl_rising(void)
{
    int sample = bus_sda();
    if (line.state == S_RX_BITS) {
        line.shift = (uint8_t)((line.shift << 1) | sample);
        line.nbits++;
    } else if (line.state == S_MASTER_ACK) {
        line.master_ack = (sample == 0);
    }
}

static void scl_falling(void)
{
    if (line.state == S_RX_BITS && line.nbits == 8) {
        byte_received();
    } else if (line.state == S_SLAVE_ACK) {
        line.pull = 0;
        if (line.reading) {
            line.tx_byte = device_read(line.dev);
            line.tx_idx = 0;
            present_bit();
            line.state = S_TX_BITS;
        } else {
            line.shift = 0; line.nbits = 0; line.state = S_RX_BITS;
        }
    } else if (line.state == S_TX_BITS) {
        line.tx_idx++;
        if (line.tx_idx < 8) present_bit();
        else { line.pull = 0; line.state = S_MASTER_ACK; }
    } else if (line.state == S_MASTER_ACK) {
        if (line.master_ack) {
            line.tx_byte = device_read(line.dev);
            line.tx_idx = 0;
            present_bit();
            line.state = S_TX_BITS;
        } else {
            line.state = S_IDLE;
        }
    }
}

static void write_hook(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (port != EXP_I2C2_SCL_PORT) return;
    int level = state == GPIO_PIN_SET;
    if (pin == EXP_I2C2_SDA_PIN) {
        int scl_high = bus_scl();
        if (scl_high && line.sda && !level) {          // SDA falls while SCL is high: START
            fake_i2c_bus.starts++;
            line.state = S_RX_BITS; line.shift = 0; line.nbits = 0; line.is_addr = 1; line.pull = 0;
        } else if (scl_high && !line.sda && level) {   // SDA rises while SCL is high: STOP
            fake_i2c_bus.stops++;
            line.state = S_IDLE; line.pull = 0;
        }
        line.sda = level;
    } else if (pin == EXP_I2C2_SCL_PIN) {
        int before = bus_scl();
        line.scl = level;
        int after = bus_scl();
        if (!before && after) scl_rising();
        else if (before && !after) scl_falling();
    }
}

static int read_hook(GPIO_TypeDef *port, uint16_t pin)
{
    if (port != EXP_I2C2_SCL_PORT) return -1;
    if (pin == EXP_I2C2_SCL_PIN) return bus_scl();
    if (pin == EXP_I2C2_SDA_PIN) return bus_sda();
    return -1;
}

void fake_i2c_bus_reset(void)
{
    memset(&fake_i2c_bus, 0, sizeof fake_i2c_bus);
    memset(&line, 0, sizeof line);
    memset(devices, 0, sizeof devices);
    memset(mlx_memory, 0, sizeof mlx_memory);
    device_count = 0;
    line.scl = line.sda = 1;
    fake_gpio_read_hook = read_hook;
    fake_gpio_write_hook = write_hook;
}

FakeI2cDevice *fake_i2c_bus_add(uint8_t addr7, int kind)
{
    FakeI2cDevice *d = &devices[device_count++];
    memset(d, 0, sizeof *d);
    d->addr = addr7;
    d->kind = kind;
    d->present = 1;
    d->words16 = (kind == FAKE_I2C_MLX || kind == FAKE_I2C_MLX642) ? mlx_memory : 0;
    d->read_only_addr = -1;
    return d;
}

void fake_i2c_bus_clear_counters(void)
{
    fake_i2c_bus.starts = fake_i2c_bus.stops = fake_i2c_bus.byte_count = fake_i2c_bus.ack_nacks = 0;
    memset(fake_i2c_bus.seen, 0, sizeof fake_i2c_bus.seen);
}

// ---- the hardware I2C of the expansion slave: whole transfers instead of single bits ----------------------------------------------------------
static FakeI2cDevice *find_device(uint16_t addr8)
{
    for (int i = 0; i < device_count; i++) if (devices[i].present && devices[i].addr == (addr8 >> 1)) return &devices[i];
    return 0;
}

static HAL_StatusTypeDef hal_transmit(uint16_t addr8, uint8_t *data, uint16_t len)
{
    FakeI2cDevice *d = find_device(addr8);
    fake_i2c_bus.starts++;
    if (addr8 == 0) {                                  // the general call: every device listens
        for (int i = 0; i < device_count; i++) {
            if (devices[i].kind == FAKE_I2C_MLX && devices[i].clear_trigger_on_reset) devices[i].words16[0x800D] &= (uint16_t)~0x8000u;
        }
        return HAL_OK;
    }
    if (!d) { fake_i2c_bus.ack_nacks++; return HAL_ERROR; }
    begin_write(d);
    for (uint16_t i = 0; i < len; i++) {
        if (fake_i2c_bus.byte_count < (int)sizeof fake_i2c_bus.seen) fake_i2c_bus.seen[fake_i2c_bus.byte_count] = data[i];
        fake_i2c_bus.byte_count++;
        device_write(d, data[i]);
    }
    return HAL_OK;
}

static HAL_StatusTypeDef hal_receive(uint16_t addr8, uint8_t *data, uint16_t len)
{
    FakeI2cDevice *d = find_device(addr8);
    fake_i2c_bus.starts++;
    if (!d) { fake_i2c_bus.ack_nacks++; memset(data, 0, len); return HAL_ERROR; }
    d->half = 0;
    for (uint16_t i = 0; i < len; i++) data[i] = device_read(d);
    return HAL_OK;
}

void fake_i2c_bus_hal_install(void)
{
    fake_i2c_tx_hook = hal_transmit;
    fake_i2c_rx_hook = hal_receive;
}
