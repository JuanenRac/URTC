// =============================================================================
// URTC Firmware - GPIO initialization (early ID-jumper pins + full post-init)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_init_gpio.h"

// =============================================================================
// 12. FULL PERIPHERAL INITIALIZATION BLOCK
// =============================================================================
void MX_GPIO_Init_Early(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    // SYSCFG's clock must be enabled for writes to SYSCFG->EXTICR (which
    // selects which GPIO port feeds each EXTI line) to actually take
    // effect - without it, routing EXTI3 to Port B (touch probe, endstop
    // tools' PB3 reads) would be silently ignored by hardware.
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    // Heartbeat / local diagnostic LED configuration (PA15 - Pin 25)
    HAL_GPIO_WritePin(LED_DIAG_PORT, LED_DIAG_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LED_DIAG_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_DIAG_PORT, &GPIO_InitStruct);

    // Logic inputs for reading the ID addressing matrix (PF0, PF1, PB4, PB7, PC13)
    GPIO_InitStruct.Pin = ID0_PIN | ID1_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ID2_PIN | ID3_PIN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PC13 (ID4) and PC15 (TMC_DIAG0, configured later) are both
    // backup-domain-adjacent pins. This design never enables the LSE
    // oscillator (no RTC crystal populated), so both should read/write
    // correctly as plain GPIO without this - but enabling PWR's clock and
    // backup-domain write access is cheap insurance either way, and
    // removes any doubt for pins with this much STM32-family-specific
    // nuance around their behavior.
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitStruct.Pin = ID4_PIN;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void MX_GPIO_Post_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Base dynamic configuration for critical Port B lines
    HAL_GPIO_WritePin(GPIOB, DIR_PIN, GPIO_PIN_RESET);
    // Tool-aware boot default for PB6 (active_tool is already known before
    // this function runs): HIGH = stepper disabled, the safe default for the
    // 6 motor tools - but per CANBUS.TXT's polarity, HIGH means interlock
    // ARMED for the laser, and HIGH means Spin (brake released) for the
    // drill, both wrong safe defaults, so those two get their own explicit case.
    if (active_tool == TOOL_LASER_ENGRAVER) {
        HAL_GPIO_WritePin(GPIOB, TMC_ENN_PIN, GPIO_PIN_RESET); // Laser: LOW is safe/locked
    } else if (active_tool == TOOL_DRILL) {
        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // Drill: LOW is braked, on its own dedicated pin
    } else if (active_tool == TOOL_INVALID) {
        // Covers every possible physical identity this state could
        // correspond to - LOW is safe for the shared stepper/laser pin,
        // and the drill's own, separate pin also needs to be forced to its
        // safe (braked) state independently.
        HAL_GPIO_WritePin(GPIOB, TMC_ENN_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(GPIOB, TMC_ENN_PIN, GPIO_PIN_SET); // Motors: HIGH is disabled
    }

    GPIO_InitStruct.Pin = DIR_PIN | TMC_ENN_PIN | DRILL_BRAKE_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // PB3/STEP_PIN is shared with the vacuum pickup's LM393 comparator
    // output and the generic probe/endstop input, so it's only ever
    // configured as an output here for the tools that actually drive a
    // stepper off it (R39's 330R series protection and the LM393's
    // open-collector output make this a low-risk net either way, but
    // getting the mode right still matters). Every other tool leaves PB3
    // untouched for the reconfiguration block further down (search "PB3
    // reconfiguration") to set once, correctly, as its actual final mode.
    if (active_tool == TOOL_PASTE_DISPENSER || active_tool == TOOL_LIQUID_DISPENSER ||
        active_tool == TOOL_SCREWDRIVER || active_tool == TOOL_GRIPPER_GIMBAL ||
        active_tool == TOOL_GRIPPER_NEMA || active_tool == TOOL_3D_PRINTER ||
        active_tool == TOOL_SMT_PICKPLACE || active_tool == TOOL_VACUUM_GRIPPER_LG) {
        HAL_GPIO_WritePin(GPIOB, STEP_PIN, GPIO_PIN_RESET);
        GPIO_InitStruct.Pin = STEP_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    }

    // doc #12's own driver interface - EXP_TMC_STEP/DIR/EN (PB12/13/14),
    // the expansion board's own driver rather than the onboard one this
    // block's other tools use. Only configured as outputs here (same
    // "only for the tool that actually drives them" reasoning as
    // STEP_PIN just above) - every other tool leaves these 3 pins in
    // their HAL reset-default state (floating inputs), same as the
    // onboard driver's own pins do for tools that don't use them.
    if (active_tool == TOOL_CRIMPING_ACTUATOR) {
        HAL_GPIO_WritePin(EXP_TMC_EN_PORT, EXP_TMC_EN_PIN, GPIO_PIN_SET); // disabled until a real move command arrives - same active-high-disabled convention as the onboard driver's own TMC_ENN
        HAL_GPIO_WritePin(EXP_TMC_STEP_PORT, EXP_TMC_STEP_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(EXP_TMC_DIR_PORT, EXP_TMC_DIR_PIN, GPIO_PIN_RESET);
        GPIO_InitStruct.Pin = EXP_TMC_STEP_PIN | EXP_TMC_DIR_PIN | EXP_TMC_EN_PIN;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    }

    // Pin init for camera ring control (PB1) - independent from the status
    // LED's PA7/SPI1, specifically so CONN_LED1/CONN_LED2 don't need a PCB
    // trace joining them in series.
    HAL_GPIO_WritePin(LED_RING_PORT, LED_RING_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LED_RING_PIN;
    HAL_GPIO_Init(LED_RING_PORT, &GPIO_InitStruct);

    // PA2 (bit-banged) is free. Left unconfigured (floating input, the reset default).

    // Soldering iron / hotend power actuator pin (PA1)
    HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = T12_PWM_PIN;
    HAL_GPIO_Init(T12_PWM_PORT, &GPIO_InitStruct);

    // PA7 carries SPI1_MOSI (AF5) for the LED chain (see MX_SPI1_Init).
    // Confirmed against this exact chip's datasheet
    // (DS9866 Table 13): PA7's alternate function list includes SPI1_MOSI.
    GPIO_InitStruct.Pin = LED_SPI_MOSI_PIN; // PA7 - SPI1_MOSI for the LED chain
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PA8 carries TIM1_CH1 (AF6) for the drill/laser/layer-fan PWM,
    // reconfigured per active tool. Confirmed against this
    // exact chip's datasheet (DS9866 Table 13): PA8's alternate function list
    // is "MCO, TIM1_CH1, USART1_CK, EVENTOUT".
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    // Restore to the baseline (plain digital output) so the following
    // HAL_GPIO_Init calls in this block, which reuse this same struct
    // without reassigning every field, don't accidentally inherit this
    // pin's alternate-function settings. All 4 fields that PA8's config
    // above touched, not just Mode/Alternate - Speed specifically was
    // being left at HIGH (this block's own original baseline, set at the
    // top of this function, is LOW) until this was caught, silently
    // giving every following digital output in this block faster edge
    // rates than intended.
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0;

    // Secondary drill pins (PA3 as EXTI, PA4 as rotation direction)
    HAL_GPIO_WritePin(DRILL_FRIN_PORT, DRILL_FRIN_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = DRILL_FRIN_PIN;
    HAL_GPIO_Init(DRILL_FRIN_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = DRILL_FGIN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DRILL_FGIN_PORT, &GPIO_InitStruct);

    // EXTI vector interrupt enable
    HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}
