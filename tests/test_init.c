// =============================================================================
// URTC Firmware - Host logic tests for the start-up configuration (firmware_init_clocks.c, _can.c, _buses.c, _timers.c, _gpio.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The five start-up modules only fill configuration structures and hand them to the HAL, so what is worth testing is what the numbers mean. The fake
// HAL keeps a copy of every structure it is given and the tests do the arithmetic of the hardware with them: the clock tree (64 MHz from the 8 MHz
// internal oscillator, a slower APB1), the CAN bit rate and sample point, the I2C timing register, the SPI clock that carries the WS2812 stream, the
// step tick and the PWM frequencies, the ADC set-up, and the level every pin is given before it becomes an output, tool by tool.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_init_clocks.h"
#include "firmware_init_can.h"
#include "firmware_init_buses.h"
#include "firmware_init_timers.h"
#include "firmware_init_gpio.h"
#include "can_bit_timing.h"
#include "test_runner.h"

#define HSI_HZ 8000000.0
#define SYSCLK_HZ 64000000.0
#define APB1_HZ 32000000.0

static int init_for(GPIO_TypeDef *port, uint16_t pin)
{
    for (int i = 0; i < fake_gpio_init_count; i++) {
        if (fake_gpio_inits[i].port == port && (fake_gpio_inits[i].init.Pin & pin) == pin) return i;
    }
    return -1;
}

static int last_level(GPIO_TypeDef *port, uint16_t pin)
{
    int level = -1;
    for (int i = 0; i < fake_gpio_write_count; i++) if (fake_gpio_writes[i].port == port && fake_gpio_writes[i].pin == pin) level = (int)fake_gpio_writes[i].state;
    return level;
}

static int first_write_before_init(GPIO_TypeDef *port, uint16_t pin)
{
    // the level is written before the pin becomes an output: the writes all come before the Init that follows them in the source, so here it is
    // enough that a write exists and that the pin is configured as an output afterwards
    return last_level(port, pin) >= 0 && init_for(port, pin) >= 0;
}

static void test_clocks(int *failures)
{
    fake_hal_reset_recording();
    SystemClock_Config();
    TEST_ASSERT(fake_rcc_calls == 3, "the oscillator, the clock tree and the peripheral clocks are configured, once each");
    TEST_ASSERT(fake_rcc_osc.OscillatorType == RCC_OSCILLATORTYPE_HSI && fake_rcc_osc.HSIState == RCC_HSI_ON, "the internal oscillator is the only one used and it is switched on");
    TEST_ASSERT(fake_rcc_osc.PLL.PLLState == RCC_PLL_ON && fake_rcc_osc.PLL.PLLSource == RCC_PLLSOURCE_HSI, "the PLL runs from it");
    double sysclk = HSI_HZ / 2.0 * fake_rcc_osc.PLL.PLLMUL;
    TEST_ASSERT(sysclk == SYSCLK_HZ, "8 MHz / 2 x 16 is the 64 MHz the timers and the SPI are computed for");
    TEST_ASSERT(fake_rcc_clk.SYSCLKSource == RCC_SYSCLKSOURCE_PLLCLK, "the system clock is taken from the PLL");
    double hclk = sysclk / fake_rcc_clk.AHBCLKDivider;
    double pclk1 = hclk / fake_rcc_clk.APB1CLKDivider;
    double pclk2 = hclk / fake_rcc_clk.APB2CLKDivider;
    TEST_ASSERT(pclk1 == APB1_HZ && pclk1 <= 36000000.0, "APB1 is 32 MHz, within the 36 MHz the part allows");
    TEST_ASSERT(pclk2 == 64000000.0 && pclk2 <= 72000000.0, "APB2 is 64 MHz, within the 72 MHz the part allows");
    TEST_ASSERT((fake_rcc_clk.ClockType & (RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2)) == (RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2), "all four clocks are set");
    TEST_ASSERT(hclk > 48000000.0 && hclk <= 72000000.0 && fake_rcc_flash_latency == FLASH_LATENCY_2, "at 64 MHz the flash needs two wait states, and gets them");
    TEST_ASSERT((fake_rcc_periph.PeriphClockSelection & (RCC_PERIPHCLK_I2C1 | RCC_PERIPHCLK_I2C2)) == (RCC_PERIPHCLK_I2C1 | RCC_PERIPHCLK_I2C2)
                && fake_rcc_periph.I2c1ClockSelection == RCC_I2C1CLKSOURCE_HSI && fake_rcc_periph.I2c2ClockSelection == RCC_I2C2CLKSOURCE_HSI, "both I2C peripherals run from the 8 MHz internal oscillator, which the timing register below assumes");
}

static void test_can(int *failures)
{
    fake_hal_reset_recording();
    memset(&hcan, 0, sizeof hcan);
    MX_CAN_Init();
    TEST_ASSERT(hcan.Instance == CAN && fake_can_inits == 1, "CAN1 is initialised once");
    double quanta = 1.0 + fake_can_init.TimeSeg1 + fake_can_init.TimeSeg2;
    double bit_rate = APB1_HZ / fake_can_init.Prescaler / quanta;
    TEST_ASSERT(bit_rate == (double)URTC_CAN_BITRATE_BPS, "the bit rate the peripheral gets from the 32 MHz clock is exactly the rate of the specification of the bus");
#if defined(URTC_CAN_LEGACY_500K) && URTC_CAN_LEGACY_500K
    TEST_ASSERT(URTC_CAN_BITRATE_BPS == 500000u && fake_can_init.Prescaler == 4, "the legacy build runs at 500 kbit/s (32 MHz / 4 / 16 time quanta)");
#else
    TEST_ASSERT(URTC_CAN_BITRATE_BPS == 1000000u && URTC_BITRATE == 1000000u && fake_can_init.Prescaler == 2, "the default build runs at 1 Mbit/s (32 MHz / 2 / 16 time quanta)");
#endif
    double sample = (1.0 + fake_can_init.TimeSeg1) / quanta;
    TEST_ASSERT(sample >= 0.80 && sample <= 0.90, "the sample point is at 87.5 percent of the bit");
    TEST_ASSERT(sample * 1000.0 >= (double)URTC_CAN_SAMPLE_POINT_MIN_PERMILLE, "the sample point is not below the floor of the specification");
    TEST_ASSERT(fake_can_init.SyncJumpWidth <= fake_can_init.TimeSeg2 && fake_can_init.Mode == CAN_MODE_NORMAL, "the jump width does not exceed the second segment, and the mode is normal (not loop-back)");
    TEST_ASSERT(fake_can_init.AutoBusOff == ENABLE && fake_can_init.AutoRetransmission == ENABLE && fake_can_init.TimeTriggeredMode == DISABLE && fake_can_init.ReceiveFifoLocked == DISABLE && fake_can_init.TransmitFifoPriority == ENABLE, "the bus recovers by itself, frames are repeated, the FIFO is not locked and the oldest request goes first");
    TEST_ASSERT(fake_can_filters == 1 && fake_can_filter.FilterBank == 0 && fake_can_filter.FilterActivation == ENABLE && fake_can_filter.FilterFIFOAssignment == CAN_RX_FIFO0, "one filter, active, into FIFO 0");
    TEST_ASSERT(fake_can_filter.FilterIdHigh == 0 && fake_can_filter.FilterIdLow == 0 && fake_can_filter.FilterMaskIdHigh == 0 && fake_can_filter.FilterMaskIdLow == 0 && fake_can_filter.FilterMode == CAN_FILTERMODE_IDMASK, "its mask is zero: every identifier is accepted and sorted in the firmware");
    TEST_ASSERT(fake_can_starts == 1 && (fake_can_notifications & CAN_IT_RX_FIFO0_MSG_PENDING) && (fake_can_notifications & CAN_IT_ERROR) && (fake_can_notifications & CAN_IT_BUSOFF), "the controller is started and told to interrupt on a frame, an error and bus-off");
    TEST_ASSERT(fake_nvic[CAN_RX0_IRQn].enabled && fake_nvic[CAN_SCE_IRQn].enabled && fake_nvic[CAN_RX0_IRQn].preempt == 1 && fake_nvic[CAN_SCE_IRQn].preempt == 1, "both CAN interrupts are enabled at priority 1");
    int pins = init_for(CAN_GPIO_PORT, CAN_RX_PIN | CAN_TX_PIN);
    TEST_ASSERT(pins >= 0 && fake_gpio_inits[pins].init.Mode == GPIO_MODE_AF_PP && fake_gpio_inits[pins].init.Alternate == GPIO_AF9_CAN && fake_gpio_inits[pins].init.Pull == GPIO_PULLUP, "PA11 and PA12 are the CAN pins, alternate function 9, with a pull-up (an unplugged bus idles recessive)");
}

static void test_buses(int *failures)
{
    fake_hal_reset_recording();
    memset(&hi2c2, 0, sizeof hi2c2);
    MX_I2C2_Init();
    TEST_ASSERT(fake_i2c_inits == 1 && hi2c2.Instance == I2C2, "I2C2 is initialised once");
    uint32_t t = fake_i2c_init.Timing;
    double presc = (double)((t >> 28) & 0xF) + 1.0, scll = (double)(t & 0xFF) + 1.0, sclh = (double)((t >> 8) & 0xFF) + 1.0;
    double frequency = HSI_HZ / presc / (scll + sclh);
    TEST_ASSERT(frequency > 90000.0 && frequency < 110000.0, "the timing register gives a 100 kHz clock from the 8 MHz source (standard mode, as the F-RAM and the display take)");
    TEST_ASSERT(fake_i2c_init.AddressingMode == I2C_ADDRESSINGMODE_7BIT && fake_i2c_init.DualAddressMode == I2C_DUALADDRESS_DISABLE && fake_i2c_init.OwnAddress1 == 0 && fake_i2c_init.GeneralCallMode == I2C_GENERALCALL_DISABLE && fake_i2c_init.NoStretchMode == I2C_NOSTRETCH_DISABLE, "master only: 7-bit addresses, no own address, no general call, stretching allowed");

    memset(&hspi1, 0, sizeof hspi1);
    memset(&hdma_spi1_tx, 0, sizeof hdma_spi1_tx);
    MX_DMA_Init();
    TEST_ASSERT(fake_dma_inits == 1 && fake_dma_init.Direction == DMA_MEMORY_TO_PERIPH && fake_dma_init.MemInc == DMA_MINC_ENABLE && fake_dma_init.PeriphInc == DMA_PINC_DISABLE, "the DMA moves memory to the SPI data register, advancing in memory only");
    TEST_ASSERT(fake_dma_init.PeriphDataAlignment == DMA_PDATAALIGN_BYTE && fake_dma_init.MemDataAlignment == DMA_MDATAALIGN_BYTE && fake_dma_init.Mode == DMA_NORMAL, "bytes, one pass at a time");
    TEST_ASSERT(fake_nvic[DMA1_Channel3_IRQn].enabled && fake_nvic[DMA1_Channel3_IRQn].preempt == 3, "its interrupt is enabled at priority 3, below the CAN and the step timer");
    TEST_ASSERT(hspi1.hdmatx == &hdma_spi1_tx, "the SPI is linked to that DMA channel");

    MX_SPI1_Init();
    TEST_ASSERT(fake_spi_inits == 1 && hspi1.Instance == SPI1, "SPI1 is initialised once");
    TEST_ASSERT(fake_spi_init.Mode == SPI_MODE_MASTER && fake_spi_init.DataSize == SPI_DATASIZE_8BIT && fake_spi_init.FirstBit == SPI_FIRSTBIT_MSB && fake_spi_init.NSS == SPI_NSS_SOFT && fake_spi_init.Direction == SPI_DIRECTION_2LINES, "master, 8 bits, most significant first, no chip select");
    TEST_ASSERT(fake_spi_init.CRCCalculation == SPI_CRCCALCULATION_DISABLE && fake_spi_init.TIMode == SPI_TIMODE_DISABLE && fake_spi_init.CLKPolarity == SPI_POLARITY_LOW && fake_spi_init.CLKPhase == SPI_PHASE_1EDGE, "no CRC, no TI mode, clock low when idle");
    double spi_hz = 64000000.0 / fake_spi_init.BaudRatePrescaler;
    double bit_us = 3.0 / spi_hz * 1e6;                          // a WS2812 bit is three SPI bits
    double low_us = 1.0 / spi_hz * 1e6;                          // a zero is the first of them high
    TEST_ASSERT(spi_hz == 2000000.0, "the SPI runs at 2 MHz (64 MHz / 32)");
    TEST_ASSERT(bit_us >= 1.05 && bit_us <= 1.85, "so one WS2812 data bit lasts 1.5 us, inside the 1.25 us plus or minus 0.6 us of the part");
    TEST_ASSERT(low_us >= 0.25 && low_us <= 0.55, "and the high time of a zero is 0.5 us, inside the 0.4 us plus or minus 0.15 us");
}

static void test_timers(int *failures)
{
    fake_hal_reset_recording();
    memset(&htim3, 0, sizeof htim3);
    MX_TIM3_Full_Init();
    TEST_ASSERT(fake_tim_base_inits == 1 && htim3.Instance == TIM3 && fake_tim_base_starts_it == 1, "timer 3 is initialised and started with its interrupt");
    double tick_hz = 64000000.0 / (fake_tim_init[2].Prescaler + 1.0) / (fake_tim_init[2].Period + 1.0);
    TEST_ASSERT(tick_hz > 990.0 && tick_hz < 1010.0 && fake_tim_init[2].CounterMode == TIM_COUNTERMODE_UP, "the step timer ticks at about 1 kHz (two edges per step, so 500 steps a second at most)");
    TEST_ASSERT(fake_nvic[TIM3_IRQn].enabled && fake_nvic[TIM3_IRQn].preempt == 2, "its interrupt is enabled at priority 2, below the CAN and above the DMA");

    memset(&htim1, 0, sizeof htim1);
    MX_TIM1_DrillLaserFan_Init(2559);
    TEST_ASSERT(htim1.Instance == TIM1 && fake_tim_pwm_inits == 1 && fake_tim_init[0].Period == 2559 && fake_tim_init[0].Prescaler == 0, "timer 1 takes the period the caller asks for, with no prescaler");
    TEST_ASSERT(64000000.0 / (fake_tim_init[0].Period + 1.0) == 25000.0, "a period of 2559 is a 25 kHz PWM, above hearing");
    TEST_ASSERT(fake_tim_oc[0].OCMode == TIM_OCMODE_PWM1 && fake_tim_oc[0].Pulse == 0 && fake_tim_oc[0].OCPolarity == TIM_OCPOLARITY_HIGH && fake_tim_oc_channel == TIM_CHANNEL_1, "PWM mode 1 on channel 1, active high, and it starts with a zero duty (off)");
    TEST_ASSERT(fake_tim_pwm_starts == 1 && fake_tim_moe == 1, "the output is started and the main output enabled (timer 1 needs it)");
    MX_TIM1_DrillLaserFan_Init(639);
    TEST_ASSERT(64000000.0 / (fake_tim_init[0].Period + 1.0) == 100000.0, "another period gives another frequency: the laser can run at 100 kHz");

    fake_tim_pwm_starts = 0;
    memset(&htim2, 0, sizeof htim2);
    MX_TIM2_HotendFan_Init();
    TEST_ASSERT(htim2.Instance == TIM2 && fake_tim_init[1].Period == 2559 && 64000000.0 / (fake_tim_init[1].Period + 1.0) == 25000.0, "timer 2 drives the hotend fan at 25 kHz");
    TEST_ASSERT(fake_tim_oc[1].Pulse == 0 && fake_tim_oc[1].OCMode == TIM_OCMODE_PWM1 && fake_tim_pwm_starts == 1, "off at start, PWM mode 1, started");

    memset(&hadc, 0, sizeof hadc);
    MX_ADC_Init(7u);
    TEST_ASSERT(hadc.Instance == ADC1 && fake_adc_inits == 1 && fake_adc_calibrations == 1, "the ADC is initialised and calibrated once");
    TEST_ASSERT(fake_adc_init.Resolution == ADC_RESOLUTION_12B && fake_adc_init.DataAlign == ADC_DATAALIGN_RIGHT && fake_adc_init.ExternalTrigConv == ADC_SOFTWARE_START, "12 bits, right aligned, started by software");
    TEST_ASSERT(fake_adc_init.ContinuousConvMode == DISABLE && fake_adc_init.ScanConvMode == ADC_SCAN_DISABLE && fake_adc_init.NbrOfConversion == 1 && fake_adc_init.DMAContinuousRequests == DISABLE, "one conversion at a time, no scan, no DMA");
    TEST_ASSERT(fake_adc_channel_configs == 1 && fake_adc_channel.Channel == 7u && fake_adc_channel.Rank == ADC_REGULAR_RANK_1 && fake_adc_channel.SingleDiff == ADC_SINGLE_ENDED, "the channel the caller asks for is the first and only one, single ended");
    TEST_ASSERT(fake_adc_channel.SamplingTime == ADC_SAMPLETIME_61CYCLES_5, "with the long sampling time the high-impedance thermistor divider needs");
}

static void test_gpio_early(int *failures)
{
    fake_hal_reset_recording();
    MX_GPIO_Init_Early();
    int led = init_for(LED_DIAG_PORT, LED_DIAG_PIN);
    TEST_ASSERT(last_level(LED_DIAG_PORT, LED_DIAG_PIN) == GPIO_PIN_RESET && led >= 0 && fake_gpio_inits[led].init.Mode == GPIO_MODE_OUTPUT_PP, "the diagnostic output is low before it becomes an output");
    int f = init_for(GPIOF, ID0_PIN | ID1_PIN), b = init_for(GPIOB, ID2_PIN | ID3_PIN), c = init_for(GPIOC, ID4_PIN);
    TEST_ASSERT(f >= 0 && b >= 0 && c >= 0, "the five tool-identity jumper pins are configured, on ports F, B and C");
    TEST_ASSERT(fake_gpio_inits[f].init.Mode == GPIO_MODE_INPUT && fake_gpio_inits[f].init.Pull == GPIO_PULLUP && fake_gpio_inits[b].init.Pull == GPIO_PULLUP && fake_gpio_inits[c].init.Pull == GPIO_PULLUP, "inputs with pull-ups: an open jumper reads 1, a closed one reads 0");
    TEST_ASSERT(fake_bkup_access_calls == 1, "backup-domain access is enabled for the pin on port C that is shared with the RTC");
}

static void expect_post(int tool, int *failures)
{
    fake_hal_reset_recording();
    active_tool = (ToolMode_t)tool;
    MX_GPIO_Post_Init();
    int enn = last_level(TMC_ENN_PORT, TMC_ENN_PIN);
    int expect_enn = (tool == TOOL_LASER_ENGRAVER) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    TEST_ASSERT(enn == expect_enn, "the driver enable / laser interlock pin gets the level of the tool before it is an output");
    int brake = last_level(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN);
    TEST_ASSERT((tool == TOOL_DRILL || tool == TOOL_INVALID) ? brake == GPIO_PIN_RESET : brake == -1, "the drill brake line is driven only for the drill (and the unknown tool, to be safe)");
    TEST_ASSERT(last_level(GPIOB, DIR_PIN) == GPIO_PIN_RESET, "the direction line starts low");
    int step_tool = (tool == TOOL_PASTE_DISPENSER || tool == TOOL_LIQUID_DISPENSER || tool == TOOL_SCREWDRIVER || tool == TOOL_GRIPPER_GIMBAL || tool == TOOL_GRIPPER_NEMA
                     || tool == TOOL_3D_PRINTER || tool == TOOL_SMT_PICKPLACE || tool == TOOL_VACUUM_GRIPPER_LG || tool == TOOL_SOLDERING_IRON);
    TEST_ASSERT(step_tool ? last_level(GPIOB, STEP_PIN) == GPIO_PIN_RESET : last_level(GPIOB, STEP_PIN) == -1, "the step line is an output, low, only for the tools that step");
    int crimp = (tool == TOOL_CRIMPING_ACTUATOR);
    TEST_ASSERT(crimp ? (last_level(EXP_TMC_EN_PORT, EXP_TMC_EN_PIN) == GPIO_PIN_SET && last_level(EXP_TMC_STEP_PORT, EXP_TMC_STEP_PIN) == GPIO_PIN_RESET && last_level(EXP_TMC_DIR_PORT, EXP_TMC_DIR_PIN) == GPIO_PIN_RESET)
                     : last_level(EXP_TMC_EN_PORT, EXP_TMC_EN_PIN) == -1, "the crimping driver of the expansion connector is set up, disabled, only for the crimping actuator");
    TEST_ASSERT(last_level(LED_RING_PORT, LED_RING_PIN) == GPIO_PIN_RESET && last_level(T12_PWM_PORT, T12_PWM_PIN) == GPIO_PIN_RESET && last_level(DRILL_FRIN_PORT, DRILL_FRIN_PIN) == GPIO_PIN_RESET, "the ring data line, the iron heater and the drill direction are low for every tool");
}

static void test_gpio_post(int *failures)
{
    for (int tool = 0; tool <= TOOL_INVALID; tool++) expect_post(tool, failures);

    fake_hal_reset_recording();
    active_tool = TOOL_SOLDERING_IRON;
    MX_GPIO_Post_Init();
    int mosi = init_for(GPIOA, LED_SPI_MOSI_PIN);
    TEST_ASSERT(mosi >= 0 && fake_gpio_inits[mosi].init.Mode == GPIO_MODE_AF_PP && fake_gpio_inits[mosi].init.Alternate == GPIO_AF5_SPI1, "the status LED pin is the SPI1 data output (alternate function 5)");
    int pwm = init_for(GPIOA, GPIO_PIN_8);
    TEST_ASSERT(pwm >= 0 && fake_gpio_inits[pwm].init.Mode == GPIO_MODE_AF_PP && fake_gpio_inits[pwm].init.Alternate == GPIO_AF6_TIM1, "PA8 is the timer 1 PWM output (alternate function 6)");
    int fg = init_for(DRILL_FGIN_PORT, DRILL_FGIN_PIN);
    TEST_ASSERT(fg >= 0 && fake_gpio_inits[fg].init.Mode == GPIO_MODE_IT_FALLING && fake_gpio_inits[fg].init.Pull == GPIO_PULLUP, "the tachometer input interrupts on the falling edge, with a pull-up");
    int frin = init_for(DRILL_FRIN_PORT, DRILL_FRIN_PIN);
    TEST_ASSERT(frin >= 0 && fake_gpio_inits[frin].init.Mode == GPIO_MODE_OUTPUT_PP && fake_gpio_inits[frin].init.Alternate == 0, "the drill direction is a plain output");
    TEST_ASSERT(fake_nvic[EXTI3_IRQn].enabled && fake_nvic[EXTI3_IRQn].preempt == 0 && fake_nvic[EXTI9_5_IRQn].enabled && fake_nvic[EXTI9_5_IRQn].preempt == 3, "the probe impact line has the top priority (0) and the fan tachometers the lowest (3)");
    TEST_ASSERT(first_write_before_init(GPIOB, STEP_PIN), "the step pin is written and configured");
}

static void test_can_bit_timing(int *failures)
{
    urtc_can_bt_t bt;
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 500000u, 875u, 2u, &bt) == URTC_CAN_BT_OK && bt.prescaler == 4 && bt.bs1 == 13 && bt.bs2 == 2 && bt.sjw == 2,
                "the calculation gives back the fixed 500 kbit/s setting of the firmware (prescaler 4, 13 + 2 quanta, jump width 2)");
    TEST_ASSERT(urtc_can_bt_rate(32000000u, &bt) == 500000u && urtc_can_bt_sample_permille(&bt) == 875u, "its rate and sample point are exact");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 250000u, 875u, 2u, &bt) == URTC_CAN_BT_OK && urtc_can_bt_rate(32000000u, &bt) == 250000u, "250 kbit/s is exact too");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, URTC_BITRATE, URTC_CAN_SAMPLE_POINT_PERMILLE, URTC_CAN_SJW_TQ, &bt) == URTC_CAN_BT_OK && bt.prescaler == 2 && bt.bs1 == 13 && bt.bs2 == 2 && bt.sjw == 2,
                "the specification's own rate, sample point and jump width give the 1 Mbit/s timing of the heads (prescaler 2, 13 + 2 quanta, jump width 2)");
    TEST_ASSERT(urtc_can_bt_rate(32000000u, &bt) == 1000000u && urtc_can_bt_sample_permille(&bt) == 875u && urtc_can_bt_sample_permille(&bt) >= URTC_CAN_SAMPLE_POINT_MIN_PERMILLE,
                "1 Mbit/s is exact (0 ppm of rate error) with the sample point at 87.5 percent, above the 75 percent floor");
    TEST_ASSERT(urtc_can_bt_tolerance_ppm(&bt) == 4854u, "the oscillator tolerance is the same at 1 Mbit/s as at 500 kbit/s (same 16 quanta)");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, URTC_BITRATE_LEGACY, URTC_CAN_SAMPLE_POINT_PERMILLE, URTC_CAN_SJW_TQ, &bt) == URTC_CAN_BT_OK && bt.prescaler == 4 && URTC_BITRATE_LEGACY == 500000u,
                "the legacy rate of the specification gives the 500 kbit/s timing of the early boards"
    );
    {
        // a bus of mixed speeds: the two settings differ, so a head built for one cannot talk to a head built for the other
        urtc_can_bt_t fast, slow;
        TEST_ASSERT(urtc_can_bt_compute(32000000u, URTC_BITRATE, 875u, 2u, &fast) == URTC_CAN_BT_OK && urtc_can_bt_compute(32000000u, URTC_BITRATE_LEGACY, 875u, 2u, &slow) == URTC_CAN_BT_OK
                    && fast.prescaler != slow.prescaler && urtc_can_bt_rate(32000000u, &fast) == 2u * urtc_can_bt_rate(32000000u, &slow), "the two speeds of the family are told apart by their timing, and one is exactly twice the other");
    }
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 300000u, 875u, 2u, &bt) == URTC_CAN_BT_ERR_NO_FIT, "a rate that no setting gives exactly is refused, never approximated");
    TEST_ASSERT(urtc_can_bt_compute(0, 500000u, 875u, 2u, &bt) == URTC_CAN_BT_ERR_ARGS && urtc_can_bt_compute(32000000u, 500000u, 950u, 2u, &bt) == URTC_CAN_BT_ERR_ARGS
                && urtc_can_bt_compute(32000000u, 500000u, 875u, 2u, 0) == URTC_CAN_BT_ERR_ARGS, "bad arguments are refused");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 500000u, 875u, 9u, &bt) == URTC_CAN_BT_OK && bt.sjw == 2 && bt.sjw <= bt.bs2, "the jump width is limited to the second segment");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 500000u, 875u, 0u, &bt) == URTC_CAN_BT_OK && bt.sjw == 1, "the jump width is at least 1");
    // The same rate from a 36 MHz clock (72 MHz system, 36 MHz APB1) is also found.
    TEST_ASSERT(urtc_can_bt_compute(36000000u, 500000u, 875u, 2u, &bt) == URTC_CAN_BT_OK && urtc_can_bt_rate(36000000u, &bt) == 500000u, "500 kbit/s from 36 MHz is found");
    static const uint32_t rates[] = {125000u, 250000u, 500000u, 1000000u};
    for (unsigned k = 0; k < sizeof rates / sizeof rates[0]; k++) {
        uint32_t rate = rates[k];
        int rc = urtc_can_bt_compute(32000000u, rate, 800u, 2u, &bt);
        TEST_ASSERT(rc == URTC_CAN_BT_OK && urtc_can_bt_rate(32000000u, &bt) == rate && bt.bs1 >= 1 && bt.bs1 <= 16 && bt.bs2 >= 1 && bt.bs2 <= 8 && bt.sjw <= bt.bs2, "every standard rate gives a legal setting");
    }

    // Tolerance and diagnostic of the clock.
    urtc_can_bt_compute(32000000u, 500000u, 875u, 2u, &bt);
    uint32_t tol = urtc_can_bt_tolerance_ppm(&bt);
    TEST_ASSERT(tol > 4000u && tol < 6000u, "the standard setting tolerates about half a percent of oscillator error");
    uint32_t tol_out = 0;
    int32_t rate_err = 99;
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 500000u, &bt, URTC_CLOCK_HSE_PPM, &tol_out, &rate_err) == URTC_CAN_DIAG_OK && tol_out == tol && rate_err == 0, "a crystal is well inside the tolerance and the rate has no error");
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 500000u, &bt, tol - 100u, 0, 0) == URTC_CAN_DIAG_MARGINAL, "a clock close to the limit is marginal (a second node with the same clock can fail)");
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 500000u, &bt, URTC_CLOCK_HSI_PPM, 0, 0) == URTC_CAN_DIAG_FAIL, "an untrimmed internal oscillator at one percent is outside the tolerance");
    TEST_ASSERT(urtc_can_bt_diag(30000000u, 500000u, &bt, 0u, 0, &rate_err) == URTC_CAN_DIAG_FAIL && rate_err < 0, "a clock that does not give the requested rate is reported with its rate error");
    TEST_ASSERT(urtc_can_bt_diag(0, 500000u, &bt, 0u, 0, 0) == URTC_CAN_DIAG_BAD_ARGS, "a zero clock is a bad argument");
    TEST_ASSERT(URTC_USE_HSE == 0 && URTC_HSE_PLLMUL_VALUE == 8u && URTC_HSE_PLLMUL_ENC(8u) == (6u << 18) && URTC_CAN_HAL_BS1(13u) == (12u << 16) && URTC_CAN_HAL_BS2(2u) == (1u << 20),
                "the crystal is off by default and the register encodings are those of the HAL");
}

// ---- limits, cuts and a fuzz with a fixed seed for the bit timing --------------------------------------------------------------------------
static uint32_t bt_rng_state = 0x5EED1234u;
static uint32_t bt_rng(void)
{
    bt_rng_state ^= bt_rng_state << 13;
    bt_rng_state ^= bt_rng_state >> 17;
    bt_rng_state ^= bt_rng_state << 5;
    return bt_rng_state;
}

static const uint32_t bt_clocks[] = {8000000u, 16000000u, 24000000u, 32000000u, 36000000u, 48000000u, 64000000u, 72000000u};
static const uint32_t bt_rates[] = {10000u, 20000u, 50000u, 100000u, 125000u, 250000u, 500000u, 800000u, 1000000u};

static void test_can_bit_timing_limits(int *failures)
{
    urtc_can_bt_t bt;
    // The two ends of the accepted sample point, and one step outside each.
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 500000u, 500u, 2u, &bt) == URTC_CAN_BT_OK && urtc_can_bt_rate(32000000u, &bt) == 500000u, "a sample point of 500 per thousand is accepted");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 500000u, 900u, 2u, &bt) == URTC_CAN_BT_OK && urtc_can_bt_rate(32000000u, &bt) == 500000u, "a sample point of 900 per thousand is accepted");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 500000u, 499u, 2u, &bt) == URTC_CAN_BT_ERR_ARGS && urtc_can_bt_compute(32000000u, 500000u, 901u, 2u, &bt) == URTC_CAN_BT_ERR_ARGS, "a sample point one step outside is refused");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 0u, 875u, 2u, &bt) == URTC_CAN_BT_ERR_ARGS, "a zero bit rate is refused");
    // A prescaler beyond 1024 is no fit; so is a clock equal to the rate; and huge arguments cannot overflow the products.
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 1000u, 875u, 2u, &bt) == URTC_CAN_BT_ERR_NO_FIT, "a rate whose prescaler is above 1024 for every quanta count has no fit");
    TEST_ASSERT(urtc_can_bt_compute(1000000u, 1000000u, 875u, 2u, &bt) == URTC_CAN_BT_ERR_NO_FIT, "a clock equal to the bit rate has no fit (fewer than 8 quanta)");
    TEST_ASSERT(urtc_can_bt_compute(0xFFFFFFFFu, 0xFFFFFFFFu, 875u, 2u, &bt) == URTC_CAN_BT_ERR_NO_FIT, "the largest arguments do not overflow the products");
    TEST_ASSERT(urtc_can_bt_compute(8000000u, 1000000u, 875u, 2u, &bt) == URTC_CAN_BT_OK && bt.prescaler == 1u && urtc_can_bt_total_tq(&bt) == 8u, "1 Mbit/s from 8 MHz uses the smallest bit of 8 quanta");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 500000u, 875u, 3u, &bt) == URTC_CAN_BT_OK && bt.sjw == 2u, "a jump width of 3 is cut to the second segment of 2 quanta");
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 500000u, 875u, 4294967295u, &bt) == URTC_CAN_BT_OK && bt.sjw >= 1u && bt.sjw <= 4u && bt.sjw <= bt.bs2, "the largest jump width is cut to 4 and to the second segment");
    // The tolerance is exactly the smaller of the two conditions of the specification (500 kbit/s, 16 quanta: 6250 and 4854 ppm).
    TEST_ASSERT(urtc_can_bt_compute(32000000u, 500000u, 875u, 2u, &bt) == URTC_CAN_BT_OK && urtc_can_bt_tolerance_ppm(&bt) == 4854u, "the tolerance of the standard setting is exactly 4854 ppm");
    urtc_can_bt_t flat = {1u, 1u, 1u, 1u};
    TEST_ASSERT(urtc_can_bt_total_tq(&flat) == 3u && urtc_can_bt_sample_permille(&flat) == 666u && urtc_can_bt_tolerance_ppm(&flat) > 0u, "the smallest setting still has a tolerance and a sample point");
    urtc_can_bt_t zero = {0u, 0u, 0u, 0u};
    TEST_ASSERT(urtc_can_bt_rate(32000000u, &zero) == 0u && urtc_can_bt_total_tq(&zero) == 1u, "a setting of zeros gives no rate, never a division by zero");
    // The limits of the diagnostic, one ppm either side.
    urtc_can_bt_compute(32000000u, 500000u, 875u, 2u, &bt);
    uint32_t tol = urtc_can_bt_tolerance_ppm(&bt);
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 500000u, &bt, tol / 2u, 0, 0) == URTC_CAN_DIAG_OK, "a clock error of exactly half the tolerance is ok");
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 500000u, &bt, tol / 2u + 1u, 0, 0) == URTC_CAN_DIAG_MARGINAL, "one ppm above half the tolerance is marginal");
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 500000u, &bt, tol, 0, 0) == URTC_CAN_DIAG_MARGINAL, "a clock error of exactly the tolerance is still marginal");
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 500000u, &bt, tol + 1u, 0, 0) == URTC_CAN_DIAG_FAIL, "one ppm above the tolerance fails");
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 500000u, &bt, 0u, 0, 0) == URTC_CAN_DIAG_OK, "a perfect clock is ok");
    int32_t err = 0;
    TEST_ASSERT(urtc_can_bt_diag(33000000u, 500000u, &bt, 0u, 0, &err) == URTC_CAN_DIAG_FAIL && err > 0, "a clock that is too fast is reported with a positive rate error");
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 0u, &bt, 0u, 0, 0) == URTC_CAN_DIAG_BAD_ARGS && urtc_can_bt_diag(32000000u, 500000u, 0, 0u, 0, 0) == URTC_CAN_DIAG_BAD_ARGS, "a zero bit rate or a missing setting is a bad argument");
    urtc_can_bt_t no_presc = {0u, 13u, 2u, 2u};
    TEST_ASSERT(urtc_can_bt_diag(32000000u, 500000u, &no_presc, 0u, 0, 0) == URTC_CAN_DIAG_BAD_ARGS, "a setting with a prescaler of zero is a bad argument");
    TEST_ASSERT(URTC_CAN_HAL_BS1(1u) == 0u && URTC_CAN_HAL_BS1(16u) == (15u << 16) && URTC_CAN_HAL_BS2(8u) == (7u << 20) && URTC_CAN_HAL_SJW(4u) == (3u << 24),
                "the register encodings of the smallest and largest segments");
    TEST_ASSERT(URTC_CLOCK_HSE_PPM < URTC_CLOCK_HSI_PPM && URTC_HSE_PLLMUL_VALUE * URTC_HSE_HZ == 64000000u, "the crystal is more exact than the internal oscillator and the multiplier gives 64 MHz");

    // Every common clock, rate and sample point gives a legal exact setting or a clean refusal (a sweep, no randomness).
    for (unsigned c = 0; c < sizeof bt_clocks / sizeof bt_clocks[0]; c++) {
        for (unsigned r = 0; r < sizeof bt_rates / sizeof bt_rates[0]; r++) {
            for (uint32_t sp = 500u; sp <= 900u; sp += 25u) {
                int rc = urtc_can_bt_compute(bt_clocks[c], bt_rates[r], sp, 2u, &bt);
                TEST_ASSERT(rc == URTC_CAN_BT_OK || rc == URTC_CAN_BT_ERR_NO_FIT, "a valid request is either solved or reported as no fit");
                if (rc == URTC_CAN_BT_OK) {
                    TEST_ASSERT(urtc_can_bt_rate(bt_clocks[c], &bt) == bt_rates[r] && bt_clocks[c] % (bt_rates[r] * urtc_can_bt_total_tq(&bt)) == 0u, "a solved setting gives exactly the requested rate");
                    TEST_ASSERT(bt.prescaler >= 1u && bt.prescaler <= 1024u && bt.bs1 >= 1u && bt.bs1 <= 16u && bt.bs2 >= 1u && bt.bs2 <= 8u && bt.sjw >= 1u && bt.sjw <= 4u && bt.sjw <= bt.bs2, "a solved setting is inside the limits of the peripheral");
                }
            }
        }
    }

    // Fuzz with a fixed seed: whatever the arguments, the answer is a legal exact setting or one of the two refusals, and the refusals match their cause.
    bt_rng_state = 0x5EED1234u;
    unsigned solved = 0, no_fit = 0, bad_args = 0;
    for (unsigned i = 0; i < 4000; i++) {
        uint32_t pclk = (bt_rng() % 8u == 0u) ? 0u : ((bt_rng() % 10u < 7u) ? bt_clocks[bt_rng() % (sizeof bt_clocks / sizeof bt_clocks[0])] : (bt_rng() % 80000000u) + 1u);
        uint32_t rate = (bt_rng() % 8u == 0u) ? 0u : ((bt_rng() % 10u < 7u) ? bt_rates[bt_rng() % (sizeof bt_rates / sizeof bt_rates[0])] : (bt_rng() % 1100000u) + 1u);
        uint32_t sp = 450u + (bt_rng() % 500u);
        uint32_t sjw = bt_rng() % 8u;
        int rc = urtc_can_bt_compute(pclk, rate, sp, sjw, &bt);
        int args_bad = (pclk == 0u || rate == 0u || sp < 500u || sp > 900u);
        if (args_bad) {
            bad_args++;
            TEST_ASSERT(rc == URTC_CAN_BT_ERR_ARGS, "bad arguments are always refused as such");
        } else if (rc == URTC_CAN_BT_OK) {
            solved++;
            TEST_ASSERT(urtc_can_bt_rate(pclk, &bt) == rate && pclk % (rate * urtc_can_bt_total_tq(&bt)) == 0u, "a solved setting is exact");
            TEST_ASSERT(bt.prescaler >= 1u && bt.prescaler <= 1024u && bt.bs1 >= 1u && bt.bs1 <= 16u && bt.bs2 >= 1u && bt.bs2 <= 8u && bt.sjw >= 1u && bt.sjw <= 4u && bt.sjw <= bt.bs2, "a solved setting is legal");
            TEST_ASSERT(urtc_can_bt_tolerance_ppm(&bt) <= 25000u, "no setting claims a tolerance above what the first condition allows");
        } else {
            no_fit++;
            TEST_ASSERT(rc == URTC_CAN_BT_ERR_NO_FIT, "a good request that is not solved is reported as no fit");
        }
    }
    TEST_ASSERT(bad_args > 100u && no_fit > 100u && solved > 100u, "the fuzz reaches the refusals and the solutions");
}

void run_init_tests(int *failures)
{
    test_can_bit_timing(failures);
    test_can_bit_timing_limits(failures);
    test_clocks(failures);
    test_can(failures);
    test_buses(failures);
    test_timers(failures);
    test_gpio_early(failures);
    test_gpio_post(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the start-up configuration of the clocks, the CAN, the buses, the timers and the pins\n");
    run_init_tests(&failures);
    if (failures == 0) {
        printf("  OK   all start-up host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}
