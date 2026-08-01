// =============================================================================
// URTC Firmware - Status LED (SPI1+DMA) and ring LED (bit-banged) driver
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_led.h"

uint8_t led_spi_buffer[200];

// WS2812B-via-SPI byte expansion table. Each WS2812B color byte becomes 3
// SPI-transmitted bytes (24 SPI bits): a WS '1' bit -> SPI pattern 110
// (~66% high), a WS '0' bit -> SPI pattern 100 (~33% high), MSB-first,
// matching the standard WS2812B-over-SPI technique. At 2MHz SPI (64MHz/32),
// each SPI bit is 500ns, so each WS bit is 1.5us total - comfortably within
// the WS2812B's tolerance. Generated and verified programmatically (0x00
// and 0xFF spot-checked bit-by-bit) rather than hand-computed, since a single
// wrong bit here would show up as literally every LED color being slightly
// wrong in a way that's hard to trace back to this table.
static const uint8_t ws2812_lut[256][3] = {
    {0x92,0x49,0x24}, {0x92,0x49,0x26}, {0x92,0x49,0x34}, {0x92,0x49,0x36}, {0x92,0x49,0xA4}, {0x92,0x49,0xA6}, {0x92,0x49,0xB4}, {0x92,0x49,0xB6},
    {0x92,0x4D,0x24}, {0x92,0x4D,0x26}, {0x92,0x4D,0x34}, {0x92,0x4D,0x36}, {0x92,0x4D,0xA4}, {0x92,0x4D,0xA6}, {0x92,0x4D,0xB4}, {0x92,0x4D,0xB6},
    {0x92,0x69,0x24}, {0x92,0x69,0x26}, {0x92,0x69,0x34}, {0x92,0x69,0x36}, {0x92,0x69,0xA4}, {0x92,0x69,0xA6}, {0x92,0x69,0xB4}, {0x92,0x69,0xB6},
    {0x92,0x6D,0x24}, {0x92,0x6D,0x26}, {0x92,0x6D,0x34}, {0x92,0x6D,0x36}, {0x92,0x6D,0xA4}, {0x92,0x6D,0xA6}, {0x92,0x6D,0xB4}, {0x92,0x6D,0xB6},
    {0x93,0x49,0x24}, {0x93,0x49,0x26}, {0x93,0x49,0x34}, {0x93,0x49,0x36}, {0x93,0x49,0xA4}, {0x93,0x49,0xA6}, {0x93,0x49,0xB4}, {0x93,0x49,0xB6},
    {0x93,0x4D,0x24}, {0x93,0x4D,0x26}, {0x93,0x4D,0x34}, {0x93,0x4D,0x36}, {0x93,0x4D,0xA4}, {0x93,0x4D,0xA6}, {0x93,0x4D,0xB4}, {0x93,0x4D,0xB6},
    {0x93,0x69,0x24}, {0x93,0x69,0x26}, {0x93,0x69,0x34}, {0x93,0x69,0x36}, {0x93,0x69,0xA4}, {0x93,0x69,0xA6}, {0x93,0x69,0xB4}, {0x93,0x69,0xB6},
    {0x93,0x6D,0x24}, {0x93,0x6D,0x26}, {0x93,0x6D,0x34}, {0x93,0x6D,0x36}, {0x93,0x6D,0xA4}, {0x93,0x6D,0xA6}, {0x93,0x6D,0xB4}, {0x93,0x6D,0xB6},
    {0x9A,0x49,0x24}, {0x9A,0x49,0x26}, {0x9A,0x49,0x34}, {0x9A,0x49,0x36}, {0x9A,0x49,0xA4}, {0x9A,0x49,0xA6}, {0x9A,0x49,0xB4}, {0x9A,0x49,0xB6},
    {0x9A,0x4D,0x24}, {0x9A,0x4D,0x26}, {0x9A,0x4D,0x34}, {0x9A,0x4D,0x36}, {0x9A,0x4D,0xA4}, {0x9A,0x4D,0xA6}, {0x9A,0x4D,0xB4}, {0x9A,0x4D,0xB6},
    {0x9A,0x69,0x24}, {0x9A,0x69,0x26}, {0x9A,0x69,0x34}, {0x9A,0x69,0x36}, {0x9A,0x69,0xA4}, {0x9A,0x69,0xA6}, {0x9A,0x69,0xB4}, {0x9A,0x69,0xB6},
    {0x9A,0x6D,0x24}, {0x9A,0x6D,0x26}, {0x9A,0x6D,0x34}, {0x9A,0x6D,0x36}, {0x9A,0x6D,0xA4}, {0x9A,0x6D,0xA6}, {0x9A,0x6D,0xB4}, {0x9A,0x6D,0xB6},
    {0x9B,0x49,0x24}, {0x9B,0x49,0x26}, {0x9B,0x49,0x34}, {0x9B,0x49,0x36}, {0x9B,0x49,0xA4}, {0x9B,0x49,0xA6}, {0x9B,0x49,0xB4}, {0x9B,0x49,0xB6},
    {0x9B,0x4D,0x24}, {0x9B,0x4D,0x26}, {0x9B,0x4D,0x34}, {0x9B,0x4D,0x36}, {0x9B,0x4D,0xA4}, {0x9B,0x4D,0xA6}, {0x9B,0x4D,0xB4}, {0x9B,0x4D,0xB6},
    {0x9B,0x69,0x24}, {0x9B,0x69,0x26}, {0x9B,0x69,0x34}, {0x9B,0x69,0x36}, {0x9B,0x69,0xA4}, {0x9B,0x69,0xA6}, {0x9B,0x69,0xB4}, {0x9B,0x69,0xB6},
    {0x9B,0x6D,0x24}, {0x9B,0x6D,0x26}, {0x9B,0x6D,0x34}, {0x9B,0x6D,0x36}, {0x9B,0x6D,0xA4}, {0x9B,0x6D,0xA6}, {0x9B,0x6D,0xB4}, {0x9B,0x6D,0xB6},
    {0xD2,0x49,0x24}, {0xD2,0x49,0x26}, {0xD2,0x49,0x34}, {0xD2,0x49,0x36}, {0xD2,0x49,0xA4}, {0xD2,0x49,0xA6}, {0xD2,0x49,0xB4}, {0xD2,0x49,0xB6},
    {0xD2,0x4D,0x24}, {0xD2,0x4D,0x26}, {0xD2,0x4D,0x34}, {0xD2,0x4D,0x36}, {0xD2,0x4D,0xA4}, {0xD2,0x4D,0xA6}, {0xD2,0x4D,0xB4}, {0xD2,0x4D,0xB6},
    {0xD2,0x69,0x24}, {0xD2,0x69,0x26}, {0xD2,0x69,0x34}, {0xD2,0x69,0x36}, {0xD2,0x69,0xA4}, {0xD2,0x69,0xA6}, {0xD2,0x69,0xB4}, {0xD2,0x69,0xB6},
    {0xD2,0x6D,0x24}, {0xD2,0x6D,0x26}, {0xD2,0x6D,0x34}, {0xD2,0x6D,0x36}, {0xD2,0x6D,0xA4}, {0xD2,0x6D,0xA6}, {0xD2,0x6D,0xB4}, {0xD2,0x6D,0xB6},
    {0xD3,0x49,0x24}, {0xD3,0x49,0x26}, {0xD3,0x49,0x34}, {0xD3,0x49,0x36}, {0xD3,0x49,0xA4}, {0xD3,0x49,0xA6}, {0xD3,0x49,0xB4}, {0xD3,0x49,0xB6},
    {0xD3,0x4D,0x24}, {0xD3,0x4D,0x26}, {0xD3,0x4D,0x34}, {0xD3,0x4D,0x36}, {0xD3,0x4D,0xA4}, {0xD3,0x4D,0xA6}, {0xD3,0x4D,0xB4}, {0xD3,0x4D,0xB6},
    {0xD3,0x69,0x24}, {0xD3,0x69,0x26}, {0xD3,0x69,0x34}, {0xD3,0x69,0x36}, {0xD3,0x69,0xA4}, {0xD3,0x69,0xA6}, {0xD3,0x69,0xB4}, {0xD3,0x69,0xB6},
    {0xD3,0x6D,0x24}, {0xD3,0x6D,0x26}, {0xD3,0x6D,0x34}, {0xD3,0x6D,0x36}, {0xD3,0x6D,0xA4}, {0xD3,0x6D,0xA6}, {0xD3,0x6D,0xB4}, {0xD3,0x6D,0xB6},
    {0xDA,0x49,0x24}, {0xDA,0x49,0x26}, {0xDA,0x49,0x34}, {0xDA,0x49,0x36}, {0xDA,0x49,0xA4}, {0xDA,0x49,0xA6}, {0xDA,0x49,0xB4}, {0xDA,0x49,0xB6},
    {0xDA,0x4D,0x24}, {0xDA,0x4D,0x26}, {0xDA,0x4D,0x34}, {0xDA,0x4D,0x36}, {0xDA,0x4D,0xA4}, {0xDA,0x4D,0xA6}, {0xDA,0x4D,0xB4}, {0xDA,0x4D,0xB6},
    {0xDA,0x69,0x24}, {0xDA,0x69,0x26}, {0xDA,0x69,0x34}, {0xDA,0x69,0x36}, {0xDA,0x69,0xA4}, {0xDA,0x69,0xA6}, {0xDA,0x69,0xB4}, {0xDA,0x69,0xB6},
    {0xDA,0x6D,0x24}, {0xDA,0x6D,0x26}, {0xDA,0x6D,0x34}, {0xDA,0x6D,0x36}, {0xDA,0x6D,0xA4}, {0xDA,0x6D,0xA6}, {0xDA,0x6D,0xB4}, {0xDA,0x6D,0xB6},
    {0xDB,0x49,0x24}, {0xDB,0x49,0x26}, {0xDB,0x49,0x34}, {0xDB,0x49,0x36}, {0xDB,0x49,0xA4}, {0xDB,0x49,0xA6}, {0xDB,0x49,0xB4}, {0xDB,0x49,0xB6},
    {0xDB,0x4D,0x24}, {0xDB,0x4D,0x26}, {0xDB,0x4D,0x34}, {0xDB,0x4D,0x36}, {0xDB,0x4D,0xA4}, {0xDB,0x4D,0xA6}, {0xDB,0x4D,0xB4}, {0xDB,0x4D,0xB6},
    {0xDB,0x69,0x24}, {0xDB,0x69,0x26}, {0xDB,0x69,0x34}, {0xDB,0x69,0x36}, {0xDB,0x69,0xA4}, {0xDB,0x69,0xA6}, {0xDB,0x69,0xB4}, {0xDB,0x69,0xB6},
    {0xDB,0x6D,0x24}, {0xDB,0x6D,0x26}, {0xDB,0x6D,0x34}, {0xDB,0x6D,0x36}, {0xDB,0x6D,0xA4}, {0xDB,0x6D,0xA6}, {0xDB,0x6D,0xB4}, {0xDB,0x6D,0xB6}
};

void Update_StatusLED_AutoColor(void) {
    uint8_t prev_r = led_state_pixel.R, prev_g = led_state_pixel.G, prev_b = led_state_pixel.B;

    if (system_error_flag) {
        led_state_pixel.R = 255; led_state_pixel.G = 0; led_state_pixel.B = 0;
        led_host_override_active = 0; // a fault clearing shouldn't snap back
                                       // to a stale host color from before it
    } else if (led_host_override_active &&
               (int32_t)(HAL_GetTick() - led_host_override_expire_tick) < 0) {
        // still within the held window - leave the host's color alone
    } else {
        led_host_override_active = 0; // expired, or was never active
        if (HAL_GetTick() - can_led_tick < LED_FUNCTIONING_WINDOW_MS) {
            led_state_pixel.R = 0; led_state_pixel.G = 0; led_state_pixel.B = 255;
        } else {
            led_state_pixel.R = 0; led_state_pixel.G = 255; led_state_pixel.B = 0;
        }
    }

    // Only ask for an actual SPI transmission if the color genuinely
    // changed - this runs every main loop iteration (it has to, since the
    // blue/green split depends on elapsed time), but re-sending the same
    // three bytes every iteration would be pointless traffic on the SPI/DMA
    // link and could fight with the busy-check in Update_StatusLED_SPI_DMA.
    if (led_state_pixel.R != prev_r || led_state_pixel.G != prev_g || led_state_pixel.B != prev_b) {
        update_led_ring_flag = 1;
    }
}

uint8_t Update_StatusLED_SPI_DMA(void) {
    if (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) return 0;

    uint32_t idx = 0;
    memcpy(&led_spi_buffer[idx], ws2812_lut[led_state_pixel.G], 3); idx += 3;
    memcpy(&led_spi_buffer[idx], ws2812_lut[led_state_pixel.R], 3); idx += 3;
    memcpy(&led_spi_buffer[idx], ws2812_lut[led_state_pixel.B], 3); idx += 3;
    memset(&led_spi_buffer[idx], 0x00, 100); // reset/latch, same reasoning as before
    idx += 100;

    HAL_SPI_Transmit_DMA(&hspi1, led_spi_buffer, idx);
    return 1;
}

// CONN_LED2 (ring, 8 pixels) - back to bit-banging on PB1, independent of
// CONN_LED1/PA7. Same technique used throughout this project before the
// SPI/DMA redesign: NOPs tuned for this core's 64MHz clock (15.625ns/cycle),
// LOW-phase NOPs and the trailing latch delay both included from the start
// this time (rather than as later patches), and the color captured once,
// atomically, before the pixel loop.
void Update_RingLEDs_BitBang(void) {
    __disable_irq();
    for (uint8_t p = 0; p < 8; p++) {
        uint8_t g = ring_pixels[p].G, r = ring_pixels[p].R, b = ring_pixels[p].B;
        for (int8_t i = 7; i >= 0; i--) {
            if (g & (1 << i)) {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            } else {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            }
        }
        for (int8_t i = 7; i >= 0; i--) {
            if (r & (1 << i)) {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            } else {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            }
        }
        for (int8_t i = 7; i >= 0; i--) {
            if (b & (1 << i)) {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            } else {
                GPIOB->BSRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
                GPIOB->BRR = LED_RING_PIN;
                __asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");__asm__("nop");
            }
        }
    }
    // Reset/latch delay, re-enabled before it since the electrical hold
    // doesn't need cycle-precise timing the way bit transmission does -
    // matches the reasoning already used elsewhere in this file.
    __enable_irq();
    for (volatile uint32_t w = 0; w < 4000; w++);
}

// Fires when the SPI1 DMA transfer for the LED chain (see
// Update_StatusLED_SPI_DMA) completes. Without this, HAL_SPI_GetState
// never transitions back to READY after the first transfer, and
// Update_StatusLED_SPI_DMA's busy-check would silently skip every
// subsequent call forever.
void DMA1_Channel3_IRQHandler(void) {
    HAL_DMA_IRQHandler(&hdma_spi1_tx);
}
