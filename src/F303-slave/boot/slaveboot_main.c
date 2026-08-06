// =============================================================================
// URTC Expansion Slave Bootloader - Entry point, clock/peripheral init, and
// I2C1 slave-mode interrupt handling
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// DESIGN NOTE this file commits to, flagged for confirmation same as any
// other hardware assumption in this project: this chip runs entirely from
// its internal HSI oscillator, no external crystal. CAN bus timing needs
// the accuracy an external crystal provides (which is why the main
// board's own bootloader/firmware uses one), but this chip does no CAN at
// all - only I2C (this link bus, plus the local sensor bus the
// application firmware owns) and PWM generation, both of which tolerate
// HSI's accuracy comfortably. Skipping a crystal + load caps keeps the
// expansion board's own BOM simpler. If the board ends up with a crystal
// on it anyway for some other reason, this should change to use it -
// worth confirming before this becomes the assumption every other module
// on this chip is quietly built on top of.
// =============================================================================
#include "stm32f3xx_hal.h"
#include "slaveboot_common.h"
#include "slaveboot_protocol.h"
#include "slaveboot_flash.h"

IWDG_HandleTypeDef hiwdg;
I2C_HandleTypeDef hi2c1;

// -----------------------------------------------------------------------
// I2C1 slave-side register-protocol state. rx_buffer's size (33) is
// exactly 1 register-address byte + REG_DATA's own documented 32-byte
// ceiling - the largest single write this protocol ever needs to accept
// in one transaction (REG_HMAC_EXPECTED is the same 33-byte total: 1 +
// 32). tx_buffer's size (10) matches REG_QUERY_VERSION's own 10-byte
// response, the largest read.
// -----------------------------------------------------------------------
static uint8_t rx_buffer[33];
static uint8_t tx_buffer[10];
static uint8_t pending_read_register = 0xFF; // 0xFF = none set yet; a read before any register write has a value to serve reads REG_STATUS by default, chosen deliberately below rather than left undefined

static void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    // I2C1 on PB6(SCL)/PB7(SDA) - this chip's default I2C1 remap, open-
    // drain with the external pull-ups already required on this link bus
    // regardless of which side is master (the main board's own
    // ExpansionI2C_* already assumes pull-ups are present for its own
    // bit-banged master role on the other end of this same physical bus).
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static void SystemClock_Config(void) {
    // HSI (8MHz) /2 prescaler (fixed on this family's PLL input mux) x16
    // = 64MHz - see this file's own top-of-file note on why HSI rather
    // than HSE. 64MHz comfortably covers this chip's actual workload
    // (I2C1 slave + I2C2 master polling 2 sensor chips + PWM generation)
    // without pushing for this chip's own 72MHz ceiling, which only HSE
    // can reach anyway (HSI/2 x16 tops out at 64MHz; there's no faster
    // combination available without an external crystal).
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
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2; // I2C1 lives on APB1, max 36MHz - 64/2=32MHz, within spec
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2); // 2 wait states required above 48MHz at 3.3V per this chip's own datasheet
}

static void MX_IWDG_Init(void) {
    // Same reload/prescaler as the main board's own bootloader - ~800ms
    // window at LSI's nominal ~40kHz, same margin reasoning applies
    // (every flash operation in slaveboot_flash.c already refreshes this
    // well inside that window).
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 999;
    HAL_IWDG_Init(&hiwdg);
}

static void MX_I2C1_Init_Slave(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x2000090E; // 100kHz standard mode at 32MHz APB1 clock - computed with STM32CubeMX's own I2C timing calculator for this exact PCLK1, not hand-derived, since this register's bitfields don't map to a simple formula on this I2C peripheral generation
    hi2c1.Init.OwnAddress1 = (I2C_SLAVE_ADDRESS << 1); // HAL expects the 7-bit address pre-shifted into bits [7:1]
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; // clock stretching left enabled deliberately - flash operations (erase/write/verify, tens of microseconds to milliseconds) run inside the same handlers this interrupt calls, and a master expecting instant responses without stretching would see NACKs or garbled data during those windows otherwise
    HAL_I2C_Init(&hi2c1);

    __HAL_RCC_I2C1_CLK_ENABLE();
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
}

void I2C1_EV_IRQHandler(void) {
    HAL_I2C_EV_IRQHandler(&hi2c1);
}

void I2C1_ER_IRQHandler(void) {
    HAL_I2C_ER_IRQHandler(&hi2c1);
}

// -----------------------------------------------------------------------
// HAL_I2C "listen mode" callbacks - this project's chosen pattern for a
// slave that can't know a write transaction's length in advance (register
// writes here range from a 1-byte read-pointer-only write up to
// REG_HMAC_EXPECTED/REG_DATA's own 33-byte ceiling). Always request the
// buffer's own full size on every receive regardless of what the master
// actually intends to send - HAL_I2C_SlaveRxCpltCallback fires on the
// master's own STOP/repeated-START regardless of whether the requested
// count was fully reached, and XferCount's remaining-bytes value at that
// point is what tells this code how many bytes actually arrived.
// -----------------------------------------------------------------------
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode) {
    (void)AddrMatchCode;
    if (TransferDirection == I2C_DIRECTION_RECEIVE) {
        // Master is about to WRITE to us.
        HAL_I2C_Slave_Seq_Receive_IT(hi2c, rx_buffer, sizeof(rx_buffer), I2C_LAST_FRAME);
    } else {
        // Master is about to READ from us - serve whatever
        // pending_read_register currently holds (set by a prior 1-byte
        // write, or REG_STATUS by default if none has ever been set -
        // see this file's own top-of-function-group note).
        uint8_t len = 1;
        switch (pending_read_register) {
            case REG_STATUS:
                tx_buffer[0] = current_status;
                len = 1;
                break;
            case REG_PROGRESS:
                tx_buffer[0] = update_progress_percent;
                len = 1;
                break;
            case REG_QUERY_VERSION:
                BuildVersionQueryResponse(tx_buffer);
                len = 10;
                break;
            default:
                // An unrecognized pending register (protocol error on the
                // master's side, or genuinely nothing set yet) gets
                // REG_STATUS's own answer rather than transmitting
                // garbage - always something coherent to read back, never
                // uninitialized buffer contents.
                tx_buffer[0] = current_status;
                len = 1;
                break;
        }
        HAL_I2C_Slave_Seq_Transmit_IT(hi2c, tx_buffer, len, I2C_LAST_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    uint32_t bytes_received = sizeof(rx_buffer) - hi2c->XferCount;
    if (bytes_received == 0) return; // shouldn't happen (AddrCallback always requests >0), guarded anyway rather than risk reading rx_buffer[0] from a transaction that never actually sent anything

    uint8_t reg = rx_buffer[0];
    if (bytes_received == 1) {
        // Register-pointer-only write - the master is telling us what
        // it's about to read next, not writing data to reg.
        pending_read_register = reg;
    } else {
        switch (reg) {
            case REG_START_UPDATE:
                if (bytes_received == 9) HandleStartUpdate(&rx_buffer[1]);
                break;
            case REG_HMAC_EXPECTED:
                if (bytes_received == 33) HandleHmacExpected(&rx_buffer[1]);
                break;
            case REG_DATA:
                HandleData(&rx_buffer[1], bytes_received - 1);
                break;
            case REG_END_UPDATE:
                if (bytes_received == 9) HandleEndUpdate(&rx_buffer[1]);
                break;
            default:
                break; // unrecognized register - ignored rather than NACKed after the fact (I2C has no clean way to retroactively NACK a transaction already accepted at the address-match stage)
        }
    }
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    (void)hi2c; // nothing to do - the transmitted bytes are already what mattered; listening resumes via HAL_I2C_ListenCpltCallback below
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c) {
    // Fires once the full transaction (address match through STOP) is
    // done - listen mode disables itself when this fires, by HAL's own
    // design, so it's re-armed here on every single transaction rather
    // than being something enabled once at init and forgotten.
    HAL_I2C_EnableListen_IT(hi2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    // Bus error, arbitration lost, or similar - re-arm listening rather
    // than leaving this chip silently deaf to the link bus until the next
    // full power cycle. Doesn't attempt to salvage whatever transaction
    // was in flight (state machine functions like HandleData already
    // check update_in_progress/update_failed themselves, so a genuinely
    // corrupted partial write is caught on the next REG_END_UPDATE's own
    // size/CRC/HMAC checks rather than needing special handling here).
    HAL_I2C_EnableListen_IT(hi2c);
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_IWDG_Init();
    MX_I2C1_Init_Slave();

    if (ApplicationIsValid()) {
        JumpToApplication();
        // Only reachable if JumpToApplication's own sanity checks
        // rejected the app despite ApplicationIsValid() passing - falls
        // through to listening below rather than looping forever with
        // nothing listening on the link bus.
    }

    HAL_I2C_EnableListen_IT(&hi2c1);

    while (1) {
        HAL_IWDG_Refresh(&hiwdg);
        HAL_Delay(50);
    }
}
