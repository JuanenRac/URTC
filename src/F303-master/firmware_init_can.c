// =============================================================================
// URTC Firmware - CAN peripheral initialization (1 Mbit/s by default, 500 kbit/s legacy; no filtering)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_init_can.h"
#include "can_bit_timing.h"

// 1 (default) = the bit timing is calculated from URTC_CAN_BITRATE_BPS, URTC_CAN_SAMPLE_PERMILLE and URTC_CAN_SJW_TQ (the specification of the bus) for the
// 32 MHz peripheral clock; an impossible setting falls back to the 16-quanta timing of the default rate. 0 = the fixed 500 kbit/s setting below (the legacy
// option written by hand, kept for the build that must not use the calculation).
#ifndef URTC_CAN_BT_FROM_CONFIG
#define URTC_CAN_BT_FROM_CONFIG 1
#endif

void MX_CAN_Init(void) {
    CAN_FilterTypeDef sFilterConfig;
    __HAL_RCC_CAN1_CLK_ENABLE();

    // CAN_RX/CAN_TX (PA11/PA12) alternate-function configuration. Without
    // this, the pins stay at their reset-default state and the bus signals
    // never actually reach the physical pins, regardless of how the CAN
    // peripheral itself is configured below.
    GPIO_InitTypeDef GPIO_CAN = {0};
    GPIO_CAN.Pin = CAN_RX_PIN | CAN_TX_PIN;
    GPIO_CAN.Mode = GPIO_MODE_AF_PP;
    GPIO_CAN.Pull = GPIO_PULLUP;
    GPIO_CAN.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_CAN.Alternate = GPIO_AF9_CAN;
    HAL_GPIO_Init(CAN_GPIO_PORT, &GPIO_CAN);

    hcan.Instance = CAN;
    // Bit timing for 500 Kbps: CAN sits on APB1 (32MHz on this chip - see
    // SystemClock_Config). Prescaler=4, 16 total time quanta (1 SYNC_SEG +
    // 13 BS1 + 2 BS2): 32,000,000 / 4 / 16 = 500,000 exactly, at the
    // standard 87.5% sample point. SJW=2TQ (up from 1TQ) for better
    // tolerance of clock drift - this tool head sits near a 260C hotend, a
    // T12 iron, and TMC driver heatsinks, any of which can shift a
    // crystal/resonator's frequency over time. SJW is physically bounded by
    // BS2, so getting a real 2TQ jump width meant widening BS2 to 2TQ too
    // (was 1TQ) rather than just changing this one field - baud rate and
    // sample point land in the exact same place either way, just with finer
    // time-quantum resolution now (16 total instead of 8).
#if URTC_CAN_BT_FROM_CONFIG
    {
        urtc_can_bt_t bt;
        if (urtc_can_bt_compute(32000000u, URTC_CAN_BITRATE_BPS, URTC_CAN_SAMPLE_PERMILLE, URTC_CAN_SJW_TQ, &bt) != URTC_CAN_BT_OK) {
            bt.prescaler = URTC_CAN_BT_FALLBACK_PRESCALER; bt.bs1 = 13; bt.bs2 = 2; bt.sjw = 2;   // an impossible setting falls back to the timing of the default rate
        }
        hcan.Init.Prescaler = bt.prescaler;
        hcan.Init.SyncJumpWidth = URTC_CAN_HAL_SJW(bt.sjw);
        hcan.Init.TimeSeg1 = URTC_CAN_HAL_BS1(bt.bs1);
        hcan.Init.TimeSeg2 = URTC_CAN_HAL_BS2(bt.bs2);
    }
    hcan.Init.Mode = CAN_MODE_NORMAL;
#else
    hcan.Init.Prescaler = 4;
    hcan.Init.Mode = CAN_MODE_NORMAL;
    hcan.Init.SyncJumpWidth = CAN_SJW_2TQ;
    hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
    hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
#endif
    hcan.Init.TimeTriggeredMode = DISABLE;
    hcan.Init.AutoBusOff = ENABLE;
    hcan.Init.AutoWakeUp = DISABLE;
    hcan.Init.AutoRetransmission = ENABLE;
    hcan.Init.ReceiveFifoLocked = DISABLE;
    hcan.Init.TransmitFifoPriority = ENABLE;
    HAL_CAN_Init(&hcan);

    // Open filters to capture every mapped setpoint ID range
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    HAL_CAN_ConfigFilter(&hcan, &sFilterConfig);

    HAL_CAN_Start(&hcan);
    HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
    // Error/bus-off notifications: AutoBusOff recovery is already enabled,
    // but without this, a bus fault (electrically plausible next to a BLDC
    // drill) would silence the transceiver with zero visibility that
    // anything had gone wrong. See HAL_CAN_ErrorCallback below.
    HAL_CAN_ActivateNotification(&hcan, CAN_IT_ERROR | CAN_IT_BUSOFF);

    // NVIC vector for CAN RX - without this, HAL_CAN_RxFifo0MsgPendingCallback
    // never runs: the hardware flags a pending message, but the CPU never
    // services the interrupt, and the board never responds to a command.
    HAL_NVIC_SetPriority(CAN_RX0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(CAN_RX0_IRQn);
    // Error/bus-off interrupts route through the separate CAN_SCE vector,
    // not CAN_RX0 - this is needed in addition to the notification above
    // for HAL_CAN_ErrorCallback to ever actually fire.
    HAL_NVIC_SetPriority(CAN_SCE_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(CAN_SCE_IRQn);
}
