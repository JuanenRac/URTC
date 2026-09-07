// =============================================================================
// URTC Firmware - Peripheral bus init: I2C2, DMA, SPI1
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_init_buses.h"

// is derived (not guessed) from ST's official AN4235 100kHz-Standard-Mode
// reference figures, scaled from 48MHz down to this chip's actual 8MHz I2C
// kernel clock (both I2C1 and I2C2 are explicitly HSI-sourced - see
// PeriphClkInit.I2c1ClockSelection/I2c2ClockSelection in SystemClock_Config
// - so this same timing value is valid for either peripheral) - getting
// this wrong means the OLED silently doesn't respond with no obvious
// error to debug.
void MX_I2C2_Init(void) {
    __HAL_RCC_I2C2_CLK_ENABLE();

    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = 0x0010232A; // ~100kHz @ 8MHz I2CCLK (this chip's I2C2
    // kernel clock is HSI-sourced directly at 8MHz, independent of the
    // 64MHz system clock - see SystemClock_Config). Derived by scaling the
    // AN4235 100kHz@48MHz reference timing targets down to 8MHz; worth
    // confirming on a scope/logic analyzer before relying on it in the field.
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c2);
}

// DMA1_Channel3 is this chip's default (unremapped) SPI1_TX request line -
// confirmed via SYSCFG_CFGR3's SPI1_TX_DMA_RMP field, which defaults to 00
// (no remap) on reset; this firmware never touches that register, so it
// stays at the default mapping. Must be initialized before MX_SPI1_Init
// links it to hspi1.hdmatx.
void MX_DMA_Init(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_spi1_tx.Instance = DMA1_Channel3;
    hdma_spi1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_spi1_tx.Init.Mode = DMA_NORMAL;
    hdma_spi1_tx.Init.Priority = DMA_PRIORITY_LOW;
    HAL_DMA_Init(&hdma_spi1_tx);
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);

    __HAL_LINKDMA(&hspi1, hdmatx, hdma_spi1_tx);
}

// SPI1 configured purely as a WS2812B-via-SPI bit-pattern generator: only
// MOSI (PA7) carries a meaningful signal, so SCK/MISO/NSS are left
// unconfigured (still their GPIO reset defaults) and software NSS management
// is used so the peripheral doesn't wait on a hardware NSS pin that isn't
// wired to anything. 2MHz clock (64MHz APB2 / 32) - see the ws2812_lut
// comment for why this specific rate.
void MX_SPI1_Init(void) {
    __HAL_RCC_SPI1_CLK_ENABLE();
    // PA7's SPI1_MOSI/AF5 config lives in MX_GPIO_Post_Init (unconditional,
    // alongside the rest of this board's universal pin setup) - not repeated here.

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES; // MISO unused but this is the standard/simplest mode
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT; // no physical NSS pin wired - avoid waiting on one
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32; // 64MHz/32 = 2MHz
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB; // matches the ws2812_lut generation
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    HAL_SPI_Init(&hspi1);
}
