// =============================================================================
// URTC Expansion Slave Application Firmware - Entry point and
// clock/peripheral init
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "stm32f3xx_hal.h"
#include "slave_common.h"
#include "slave_i2c_link.h"
#include "slave_i2c_sensors.h"
#include "slave_pwm.h"

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
IWDG_HandleTypeDef hiwdg;
TIM_HandleTypeDef htim1;
uint8_t mlx_sensor_variant = MLX_VARIANT_NONE; // safe default (no sensor configured) until the main board's own REG_MLX_SENSOR_VARIANT write tells this chip otherwise - matches this project's own established pattern for every other host-told-not-detected setting (expansion_board_type on the main board is the same idea)

static void SystemClock_Config(void) {
    // Identical to slaveboot_main.c's own SystemClock_Config - same HSI-
    // only, no-external-crystal reasoning applies to the application as
    // much as the bootloader; see that file's own top-of-file note.
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                 | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2; // I2C1/I2C2 both live on APB1, max 36MHz - 64/2=32MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1; // TIM1 lives on APB2, no such ceiling at 64MHz
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

static void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    // I2C1 (link, slave) on PB6/PB7 - same pins the bootloader already
    // used for this same bus.
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // I2C2 (local sensor bus, master) on PB10/PB11 - this chip's default
    // I2C2 remap, open-drain with pull-ups required on this board (same
    // convention as every other I2C bus in this project).
    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static uint8_t MX_I2C1_Init_Slave(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x2000090E; // same computed value as the bootloader's own I2C1 init - identical PCLK1 (32MHz), identical target (100kHz standard mode)
    hi2c1.Init.OwnAddress1 = (I2C_SLAVE_ADDRESS << 1);
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; // same reasoning as the bootloader - register handlers here (particularly anything touching the local sensor bus) aren't necessarily instant
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) return 0;

    __HAL_RCC_I2C1_CLK_ENABLE();
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    return 1;
}

static void MX_I2C2_Init_Master(void) {
    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = 0x2000090E; // same 100kHz-at-32MHz timing as I2C1 - both ADS1115 and MLX90640 are comfortably within standard mode's own 100kHz ceiling (MLX90640 supports up to 1MHz Fast Mode+, but nothing here currently needs that speed)
    hi2c2.Init.OwnAddress1 = 0; // master-only role on this bus - no slave address of its own needed
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c2);
    __HAL_RCC_I2C2_CLK_ENABLE();
    // No interrupt/NVIC setup here deliberately - slave_i2c_sensors.c
    // talks to both local chips with HAL's own blocking
    // Master_Transmit/Receive calls (50ms timeout each), not interrupt-
    // driven, so nothing here needs an ISR the way I2C1's slave role does.
}

static void MX_IWDG_Init(void) {
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 999;
    HAL_IWDG_Init(&hiwdg);
}

void I2C1_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hi2c1); }
void I2C1_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hi2c1); }

int main(void) {
    // Relocates its own vector table right at the start, before
    // HAL_Init() - same defensive convention already established for the
    // main board's own application firmware (see this project's own
    // note on that: the bootloader already does this before jumping
    // here, so this isn't covering a live gap, it's cheap insurance
    // against a future refactor reordering either side's own VTOR write).
    SCB->VTOR = 0x08005000UL; // MAIN_APP_ADDR from slaveboot_common.h - duplicated as a literal here since the application doesn't share that bootloader-only header, same as how the main board's own application firmware doesn't include bootloader_common.h either
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_IWDG_Init();
    if (!MX_I2C1_Init_Slave()) {
        // I2C1 is this chip's only link to the main board - if it fails to
        // initialize (bad timing config, peripheral fault), continuing
        // into a half-configured listen loop would leave it silently
        // deaf to the link bus with no diagnostic. Refusing to refresh
        // the IWDG here lets it force a full reset instead (~0.8s, same
        // Prescaler/Reload as configured above), retrying the whole init
        // sequence fresh - the same low-risk, self-retrying reasoning
        // already established for CAN bus-off recovery on the main board.
        while (1);
    }
    MX_I2C2_Init_Master();
    PWM_Init();
    Sensors_Init();
    I2CLink_Init();

    while (1) {
        HAL_IWDG_Refresh(&hiwdg);
        PWM_Tick();

        // Link-comms-loss failsafe: PWM_Tick above only ever auto-stops a
        // timed pulse (duration_ms>0) counting down to 0 - a continuous
        // pulse (duration_ms=0, used by spot-welder/ultrasonic-transducer
        // tools) has no such countdown and would otherwise keep energizing
        // its output forever if the main board crashes or the link cable
        // is unplugged mid-pulse. A no-op on any channel that isn't
        // currently running, so calling it unconditionally here is safe.
        if (HAL_GetTick() - i2c_link_last_activity_tick > 1000) {
            PWM_StopAll();
        }

        HAL_Delay(1);
    }
}
