// =============================================================================
// URTC Firmware - Host logic tests for the status LED and the camera ring (firmware_led.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The colour rule of the status LED (error red, host override, bus activity blue, otherwise green), the SPI stream that carries it to the
// WS2812 (three SPI bytes per colour byte, green first, then the reset gap) and the bit-banged ring. The real firmware_led.c is compiled against
// the fake HAL. The SPI stream is checked against an encoder written here from the WS2812 timing (a one is 110, a zero is 100), for all 256 values.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_led.h"
#include "test_runner.h"

extern uint8_t led_spi_buffer[200];
void DMA1_Channel3_IRQHandler(void);

static void world(void)
{
    fake_hal_reset_recording();
    fake_tick = 100000;
    fake_tick_step = 0;
    system_error_flag = 0;
    led_host_override_active = 0;
    led_host_override_expire_tick = 0;
    can_led_tick = 0;
    update_led_ring_flag = 0;
    memset((void *)&led_state_pixel, 0, sizeof led_state_pixel);
    memset((void *)ring_pixels, 0, sizeof ring_pixels);
}

static void encode(uint8_t value, uint8_t out[3])
{
    uint32_t bits = 0;
    for (int i = 7; i >= 0; i--) bits = (bits << 3) | (((value >> i) & 1) ? 0x6u : 0x4u);
    out[0] = (uint8_t)(bits >> 16); out[1] = (uint8_t)(bits >> 8); out[2] = (uint8_t)bits;
}

static void test_auto_color(int *failures)
{
    world();
    Update_StatusLED_AutoColor();
    TEST_ASSERT(led_state_pixel.R == 0 && led_state_pixel.G == 255 && led_state_pixel.B == 0, "no bus activity: green");
    TEST_ASSERT(update_led_ring_flag == 1, "a change of colour asks for the ring to be redrawn");

    update_led_ring_flag = 0;
    Update_StatusLED_AutoColor();
    TEST_ASSERT(update_led_ring_flag == 0, "the same colour again does not");

    can_led_tick = fake_tick - (LED_FUNCTIONING_WINDOW_MS - 1);
    Update_StatusLED_AutoColor();
    TEST_ASSERT(led_state_pixel.R == 0 && led_state_pixel.G == 0 && led_state_pixel.B == 255 && update_led_ring_flag == 1, "a frame inside the window: blue");
    can_led_tick = fake_tick - LED_FUNCTIONING_WINDOW_MS;
    Update_StatusLED_AutoColor();
    TEST_ASSERT(led_state_pixel.G == 255 && led_state_pixel.B == 0, "at the end of the window it is green again");

    can_led_tick = fake_tick;
    led_host_override_active = 1;
    led_host_override_expire_tick = fake_tick + 5000;
    led_state_pixel.R = 9; led_state_pixel.G = 8; led_state_pixel.B = 7;
    update_led_ring_flag = 0;
    Update_StatusLED_AutoColor();
    TEST_ASSERT(led_state_pixel.R == 9 && led_state_pixel.G == 8 && led_state_pixel.B == 7 && led_host_override_active == 1 && update_led_ring_flag == 0, "while the host override lasts the colour the host set is kept");

    fake_tick = led_host_override_expire_tick;
    can_led_tick = fake_tick;
    Update_StatusLED_AutoColor();
    TEST_ASSERT(led_host_override_active == 0 && led_state_pixel.B == 255, "when the override expires the rule takes over again");

    fake_tick = 0xFFFFFFF0u;
    led_host_override_active = 1;
    led_host_override_expire_tick = 0x10u;                  // the tick counter wraps before the override ends
    led_state_pixel.R = 1; led_state_pixel.G = 2; led_state_pixel.B = 3;
    can_led_tick = 0;
    Update_StatusLED_AutoColor();
    TEST_ASSERT(led_host_override_active == 1 && led_state_pixel.R == 1, "an override that ends after the counter wraps is still running before the wrap");

    led_state_pixel.R = 0; led_state_pixel.G = 0; led_state_pixel.B = 0;
    system_error_flag = 1;
    update_led_ring_flag = 0;
    Update_StatusLED_AutoColor();
    TEST_ASSERT(led_state_pixel.R == 255 && led_state_pixel.G == 0 && led_state_pixel.B == 0 && led_host_override_active == 0 && update_led_ring_flag == 1, "a latched error is red and cancels any host override");
}

static void test_spi_stream(int *failures)
{
    world();
    fake_spi_state = HAL_SPI_STATE_BUSY;
    TEST_ASSERT(Update_StatusLED_SPI_DMA() == 0 && fake_spi_dma_count == 0, "a transfer still running is not started over");
    fake_spi_state = HAL_SPI_STATE_READY;

    led_state_pixel.R = 255; led_state_pixel.G = 0; led_state_pixel.B = 0;
    TEST_ASSERT(Update_StatusLED_SPI_DMA() == 1 && fake_spi_dma_count == 1, "a free peripheral starts one transfer");
    TEST_ASSERT(fake_spi_dma_len == 109, "nine bytes of colour and a hundred bytes of reset gap");
    uint8_t g0[3], r255[3];
    encode(0, g0); encode(255, r255);
    TEST_ASSERT(memcmp(&fake_spi_dma_data[0], g0, 3) == 0, "the green byte goes first");
    TEST_ASSERT(memcmp(&fake_spi_dma_data[3], r255, 3) == 0, "then the red byte");
    TEST_ASSERT(memcmp(&fake_spi_dma_data[6], g0, 3) == 0, "then the blue byte");
    int gap_clear = 1;
    for (int i = 9; i < 109; i++) if (fake_spi_dma_data[i] != 0) gap_clear = 0;
    TEST_ASSERT(gap_clear, "the line is held low for the latch gap");

    int all_match = 1;
    for (int value = 0; value < 256; value++) {
        uint8_t expected[3];
        encode((uint8_t)value, expected);
        led_state_pixel.G = (uint8_t)value; led_state_pixel.R = (uint8_t)(255 - value); led_state_pixel.B = (uint8_t)value;
        Update_StatusLED_SPI_DMA();
        uint8_t other[3];
        encode((uint8_t)(255 - value), other);
        if (memcmp(&fake_spi_dma_data[0], expected, 3) || memcmp(&fake_spi_dma_data[3], other, 3) || memcmp(&fake_spi_dma_data[6], expected, 3)) all_match = 0;
    }
    TEST_ASSERT(all_match, "every one of the 256 values is encoded the way the WS2812 timing needs, in the right byte");
    DMA1_Channel3_IRQHandler();
    TEST_ASSERT(fake_dma_irq_count == 1, "the DMA interrupt is handed to the HAL");
}

static void test_ring(int *failures)
{
    world();
    for (int i = 0; i < 8; i++) { ring_pixels[i].R = (uint8_t)(i * 30); ring_pixels[i].G = (uint8_t)(255 - i * 30); ring_pixels[i].B = (uint8_t)(i * 7); }
    GPIOB->BSRR = 0; GPIOB->BRR = 0;
    Update_RingLEDs_BitBang();
    TEST_ASSERT(GPIOB->BRR == LED_RING_PIN, "the data line is left low after the last bit");
    TEST_ASSERT(GPIOB->BSRR == LED_RING_PIN, "the pin the ring hangs on is the one that was driven");
    for (int i = 0; i < 8; i++) { ring_pixels[i].R = 255; ring_pixels[i].G = 255; ring_pixels[i].B = 255; }
    Update_RingLEDs_BitBang();
    memset((void *)ring_pixels, 0, sizeof ring_pixels);
    Update_RingLEDs_BitBang();
    TEST_ASSERT(GPIOB->BRR == LED_RING_PIN, "all ones and all zeros run to the end the same way");
}

void run_led_tests(int *failures)
{
    test_auto_color(failures);
    test_spi_stream(failures);
    test_ring(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - firmware_led.c\n");
    run_led_tests(&failures);
    if (failures == 0) {
        printf("  OK   all LED host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
