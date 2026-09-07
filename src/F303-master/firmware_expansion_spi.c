// =============================================================================
// URTC Firmware - CONN_EXPANSION bit-banged SPI (SPI Mode 3)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_expansion_spi.h"

// Bit-banged SPI transfer for CONN_EXPANSION's EXP_SPI_* pins - see the pin
// definitions above and PINOUT_CONNECTORS.TXT's CONN_EXPANSION note for why
// this is software-timed rather than a hardware SPI peripheral.
//
// Implements SPI Mode 3 (CPOL=1, CPHA=1) specifically - confirmed against
// the TMC5160/TMC5160A datasheet's own SPI Interface section (section 4.2:
// "the node latching the data from SDI on the rising edge of SCK and
// driving data to SDO following the falling edge... Hint: Usually this SPI
// timing is referred to as SPI MODE 3"), not assumed. Getting this wrong
// wouldn't cause a hard fault or an obviously broken symptom - it would
// just silently fail to communicate, or read back garbage that looks
// plausible enough to be confusing. Clock idles HIGH between transfers,
// matching Mode 3 - the caller is responsible for asserting EXP_SPI_CS_PIN
// low before the first byte of a transaction and releasing it high after
// the last one; this function only handles the 8-bit shift itself.
//
// Timing is intentionally not cycle-counted the way the WS2812B driver
// above is - unlike WS2812B's protocol-defined bit timing, SPI is
// edge-triggered rather than time-triggered: the slave samples relative to
// the clock edges this function generates, not against a wall-clock
// deadline, so a plain small delay between edges (not a precise one) is
// sufficient and correct.
static void ExpansionSPI_Delay(void) {
    for (volatile uint32_t d = 0; d < 20; d++);
}

uint8_t ExpansionSPI_TransferByte(uint8_t tx_byte) {
    uint8_t rx_byte = 0;
    for (int8_t i = 7; i >= 0; i--) {
        // Setup: drive the next output bit while SCK is still high (idle)
        // from the previous iteration (or CS assertion, for the first bit) -
        // matches Mode 3's "data changes on the falling edge" by having the
        // new value already stable before that edge happens below.
        if (tx_byte & (1 << i)) {
            HAL_GPIO_WritePin(EXP_SPI_MOSI_PORT, EXP_SPI_MOSI_PIN, GPIO_PIN_SET);
        } else {
            HAL_GPIO_WritePin(EXP_SPI_MOSI_PORT, EXP_SPI_MOSI_PIN, GPIO_PIN_RESET);
        }
        HAL_GPIO_WritePin(EXP_SPI_SCK_PORT, EXP_SPI_SCK_PIN, GPIO_PIN_RESET); // falling edge
        ExpansionSPI_Delay();
        HAL_GPIO_WritePin(EXP_SPI_SCK_PORT, EXP_SPI_SCK_PIN, GPIO_PIN_SET); // rising edge - sample here
        rx_byte <<= 1;
        if (HAL_GPIO_ReadPin(EXP_SPI_MISO_PORT, EXP_SPI_MISO_PIN) == GPIO_PIN_SET) {
            rx_byte |= 0x01;
        }
        ExpansionSPI_Delay();
    }
    return rx_byte;
}

void MX_ExpansionSPI_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // CS idles high (deselected) - set before configuring as output so
    // there's never a glitch low, even momentarily, during setup.
    HAL_GPIO_WritePin(EXP_SPI_CS_PORT, EXP_SPI_CS_PIN, GPIO_PIN_SET);
    // SCK idles high, matching Mode 3.
    HAL_GPIO_WritePin(EXP_SPI_SCK_PORT, EXP_SPI_SCK_PIN, GPIO_PIN_SET);

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    GPIO_InitStruct.Pin = EXP_SPI_CS_PIN;
    HAL_GPIO_Init(EXP_SPI_CS_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = EXP_SPI_SCK_PIN;
    HAL_GPIO_Init(EXP_SPI_SCK_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = EXP_SPI_MOSI_PIN;
    HAL_GPIO_Init(EXP_SPI_MOSI_PORT, &GPIO_InitStruct);

    // MISO is the only input of the four - pulled up so an expansion board
    // that isn't actually populated yet (or a chip in reset/unpowered)
    // reads back a stable, known 0xFF pattern rather than a floating,
    // noise-sensitive line.
    GPIO_InitStruct.Pin = EXP_SPI_MISO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(EXP_SPI_MISO_PORT, &GPIO_InitStruct);

    // TMC_DIAG0 - plain input, no internal pull. The board now has a real
    // external pull-down (R50, 10K) as part of the diode-OR combining this
    // with the onboard TMC2209's own DIAG - enabling the MCU's internal
    // pull-up here as well would fight that external pull-down and leave
    // the idle level ambiguous instead of a clean, unambiguous low. See
    // this pin's own #define comment for the full topology.
    GPIO_InitStruct.Pin = TMC_DIAG0_PIN;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(TMC_DIAG0_PORT, &GPIO_InitStruct);
}
