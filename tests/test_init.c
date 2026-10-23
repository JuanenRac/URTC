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
    TEST_ASSERT(bit_rate == 500000.0, "32 MHz / 4 / 16 time quanta is 500 kbit/s, the rate of the whole family");
    double sample = (1.0 + fake_can_init.TimeSeg1) / quanta;
    TEST_ASSERT(sample >= 0.80 && sample <= 0.90, "the sample point is at 87.5 percent of the bit");
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
    int expect_enn = (tool == TOOL_LASER_ENGRAVER || tool == TOOL_INVALID) ? GPIO_PIN_RESET : GPIO_PIN_SET;
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

void run_init_tests(int *failures)
{
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
