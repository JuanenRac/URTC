// =============================================================================
// URTC Bootloader - clock config, CAN init, main loop, and interrupt handlers
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// This is the entry point of the partitioned build of the bootloader -
// same behavior as the single-file BOOTLOADER.C, split across several
// .c/.h files for readability. Both are maintained side by side.
// =============================================================================
#include "stm32f3xx_hal.h"
#include "bootloader_common.h"
#include "bootloader_crypto.h"
#include "bootloader_flash.h"
#include "bootloader_oled.h"
#include "bootloader_protocol.h"

CAN_HandleTypeDef hcan;
IWDG_HandleTypeDef hiwdg;
I2C_HandleTypeDef hi2c2;

void SystemClock_Config(void) {
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
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

// -----------------------------------------------------------------------
// CAN init - same bit timing as the main application (32MHz APB1, verified
// during that firmware's own port), same PA11/PA12 AF9 pin assignment,
// same wide-open filter (this bootloader only cares about its own 0x7Fx
// range and just checks the ID in software after receiving - narrowing the
// hardware filter isn't worth the risk of a bit-alignment mistake here any
// more than it was in the main application).
// -----------------------------------------------------------------------
static void MX_CAN_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    hcan.Instance = CAN;
    hcan.Init.Prescaler = 4;
    hcan.Init.Mode = CAN_MODE_NORMAL;
    hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
    hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan.Init.TimeTriggeredMode = DISABLE;
    hcan.Init.AutoBusOff = ENABLE;
    hcan.Init.AutoWakeUp = DISABLE;
    hcan.Init.AutoRetransmission = ENABLE;
    hcan.Init.ReceiveFifoLocked = DISABLE;
    hcan.Init.TransmitFifoPriority = DISABLE;
    HAL_CAN_Init(&hcan);

    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 0; // only meaningful on dual-CAN
    // parts (splitting filter banks between CAN1/CAN2) - this chip has a
    // single CAN peripheral, so this has no effect either way, but 0 is
    // clearer than a non-zero value implying dual-CAN awareness this
    // project doesn't need.
    HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);

    HAL_CAN_Start(&hcan);
}

// Tracks the ~1s heartbeat cadence in main()'s own loops below - purely
// local to this file, not shared with any module.
static uint32_t last_heartbeat_tick = 0 - 1000; // forces an immediate heartbeat on the first check, rather than waiting a full 1000ms

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_CAN_Init();
    OLED_Init_Boot(); // for the update progress display

    // NVIC_SystemReset() does not stop the IWDG - it's "independent"
    // specifically so a reset from something else can't silence it. The
    // application always starts one during normal operation, so a
    // 0x7F0-triggered reset into this bootloader inherits an
    // already-ticking ~800ms countdown. Same config as the app
    // (Prescaler=32, Reload=999, ~800ms @ ~40kHz LSI) so this is a no-op
    // if it's already running with these values, and a fresh start if this
    // is a first boot with no application having run yet.
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 999;
    HAL_IWDG_Init(&hiwdg);

    uint8_t app_valid = ApplicationIsValid();

    // Listening window: ~600ms. Long enough for a master that wants to
    // trigger an update to get a frame in reliably, short enough not to be
    // a noticeable boot delay for normal operation.
    uint32_t listen_start = HAL_GetTick();
    uint8_t enter_update_mode = 0;

    while ((HAL_GetTick() - listen_start) < 600) {
        HAL_IWDG_Refresh(&hiwdg);
        CAN_RxHeaderTypeDef rxHeader;
        uint8_t rxData[8];
        if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0) {
            if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK) {
                // A Remote Transmission Request frame's DLC field states a
                // requested length without the frame actually carrying that
                // many (or any) data bytes - rxData for one would be
                // whatever was left over from before, not real payload.
                // Every command below expects real data, so RTR (and
                // anything not standard 11-bit ID, which this protocol
                // never uses) is rejected before any of them run.
                if (rxHeader.RTR != CAN_RTR_DATA || rxHeader.IDE != CAN_ID_STD) {
                    continue;
                }
                if (rxHeader.StdId == CAN_ID_START_UPDATE && rxHeader.DLC == 8) {
                    enter_update_mode = 1;
                    HandleStartUpdate(rxData);
                    break;
                } else if (rxHeader.StdId == CAN_ID_QUERY_VERSION) {
                    HandleVersionQuery();
                } else if (rxHeader.StdId == CAN_ID_QUERY_ERROR_COUNTERS) {
                    HandleErrorCounterQuery();
                } else if (rxHeader.StdId == CAN_ID_READBACK) {
                    HandleReadbackStart();
                }
            }
        }
        if (HAL_GetTick() - last_heartbeat_tick >= 1000) {
            last_heartbeat_tick = HAL_GetTick();
            CAN_SendHeartbeat(STATUS_LISTENING, 0xFF);
        }
    }

    if (!enter_update_mode && app_valid) {
        JumpToApplication();
        // if we get here, JumpToApplication's sanity check rejected the
        // image despite passing every stored check (shouldn't happen, but
        // fall through to the listening loop below rather than doing
        // nothing forever)
    }

    // Either an update was requested, or there's no valid application to
    // jump to - stay here and listen indefinitely.
    CAN_SendStatus(STATUS_LISTENING);
    while (1) {
        HAL_IWDG_Refresh(&hiwdg);
        CAN_RxHeaderTypeDef rxHeader;
        uint8_t rxData[8];
        if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0) {
            if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK) {
                // Same reasoning as the initial listening window above.
                if (rxHeader.RTR != CAN_RTR_DATA || rxHeader.IDE != CAN_ID_STD) {
                    continue;
                }
                if (rxHeader.StdId == CAN_ID_START_UPDATE && rxHeader.DLC == 8) {
                    HandleStartUpdate(rxData);
                } else if (rxHeader.StdId == CAN_ID_HMAC_CHUNK && rxHeader.DLC == 8) {
                    HandleHmacChunk(rxData);
                } else if (rxHeader.StdId == CAN_ID_DATA) {
                    HandleData(rxData, rxHeader.DLC);
                } else if (rxHeader.StdId == CAN_ID_END_UPDATE && rxHeader.DLC == 8) {
                    HandleEndUpdate(rxData);
                } else if (rxHeader.StdId == CAN_ID_QUERY_VERSION) {
                    HandleVersionQuery();
                } else if (rxHeader.StdId == CAN_ID_QUERY_ERROR_COUNTERS) {
                    HandleErrorCounterQuery();
                } else if (rxHeader.StdId == CAN_ID_AUTHORIZE_DOWNGRADE && rxHeader.DLC == 4) {
                    HandleAuthorizeDowngrade(rxData);
                } else if (rxHeader.StdId == CAN_ID_READBACK) {
                    HandleReadbackStart();
                }
            }
        }

        if (HAL_GetTick() - last_heartbeat_tick >= 1000) {
            last_heartbeat_tick = HAL_GetTick();
            uint8_t pct = 0xFF;
            uint8_t hb_status = STATUS_LISTENING;
            if (update_in_progress && !update_failed && update_total_size > 0) {
                pct = (uint8_t)(((uint64_t)update_bytes_received * 100) / update_total_size);
                hb_status = STATUS_RECEIVING;
            }
            CAN_SendHeartbeat(hb_status, pct);
        }

        // Without this, an update interrupted mid-transfer (CAN cable
        // unplugged, master crashed, etc.) left the bootloader waiting for
        // more data forever - the backup slot flash was already erased by
        // HandleStartUpdate, so there was nothing to fall back to, and the
        // only way out was a manual power cycle. 10s of silence during an
        // active transfer now aborts back to a clean listening state on its
        // own - the main slot was never touched by any of this, so nothing
        // is at risk, and the master can just retry the whole update.
        if (update_in_progress && !update_failed &&
            (HAL_GetTick() - update_last_activity_tick > 10000)) {
            update_in_progress = 0;
            update_failed = 0;
            OLED_ClearAll_Boot();
            OLED_PrintStr2x_Boot(2, 4, "ERROR");
            CAN_SendStatus(STATUS_LISTENING);
        }
    }
}

void HardFault_Handler(void) {
    // Same reasoning as the main application's handler: force every pin on
    // GPIOA/GPIOB to a safe, disconnected state. The bootloader doesn't
    // drive any actuators itself, but if it's running at all something
    // has already gone wrong with the application, so erring toward
    // "everything off" here too costs nothing.
    GPIOA->MODER = 0xEBFFFFFF;
    GPIOB->MODER = 0xFFFFFFFF;
    while (1) { }
}

// NOTE: this bootloader polls HAL_CAN_GetRxFifoFillLevel() directly in its
// main loop rather than using interrupt-driven reception - the hardware
// FIFO fills autonomously regardless of interrupt configuration, and a
// bootloader has nothing else competing for CPU time the way the main
// application does, so polling is simpler here with no real downside. No
// CAN_RX0_IRQHandler is defined for the same reason - it would never fire,
// since neither HAL_NVIC_EnableIRQ nor HAL_CAN_ActivateNotification are
// called for this peripheral.
//
// HAL_GetTick() depends on SysTick actually incrementing it - without this
// handler, every HAL_Delay() call here (including the listening-window
// timeout check and the status-frame settling delay before
// NVIC_SystemReset()) would spin forever instead of ever completing.
void SysTick_Handler(void) {
    HAL_IncTick();
}
