// =============================================================================
// URTC Expansion Slave Application Firmware - I2C1 link-bus protocol
// (application mode)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Same HAL "listen mode" pattern already verified working in this
// chip's own bootloader (slaveboot_main.c) - variable-length register
// writes handled the same way (request the buffer's own max size every
// time, compute actual bytes received from XferCount afterward), reused
// here rather than re-derived, since the earlier version of this exact
// approach already compiled, linked, and matched its own documented
// byte-counting logic. What's different here: application-mode registers
// (slave_common.h's own REG_APP_*/REG_MLX_*/REG_ADS_*/REG_PWM_*), and one
// new wrinkle the bootloader's own protocol never needed - the
// REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK "select chunk, then read"
// pattern needs a 1-byte payload alongside the register selector, not
// just the register number alone the way a plain read-pointer write
// works everywhere else in this protocol.
// =============================================================================
#include "stm32f3xx_hal.h"
#include "slave_common.h"
#include "slave_i2c_link.h"
#include "slave_i2c_sensors.h"
#include "slave_pwm.h"

static uint8_t rx_buffer[8]; // largest real write is 4 bytes (1 register + 3 payload, REG_PWM_CONFIGURE/REG_PWM_PULSE) - sized with margin
static uint8_t tx_buffer[32]; // largest real read is 32 bytes (REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK)
static uint8_t pending_read_register = 0xFF;
static uint8_t pending_mlx_chunk_index = 0;
volatile uint32_t i2c_link_last_activity_tick = 0;

void I2CLink_Init(void) {
    HAL_I2C_EnableListen_IT(&hi2c1);
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode) {
    (void)AddrMatchCode;
    if (TransferDirection == I2C_DIRECTION_RECEIVE) {
        HAL_I2C_Slave_Seq_Receive_IT(hi2c, rx_buffer, sizeof(rx_buffer), I2C_LAST_FRAME);
    } else {
        uint8_t len = 1;
        switch (pending_read_register) {
            case REG_APP_STATUS:
                tx_buffer[0] = APP_STATUS_IDLE; // this chip has no long-running blocking operation on the application side the way STATUS_COPYING is on the bootloader side - every REG_* handler below returns well within one I2C transaction, so there's currently never a moment this would read APP_STATUS_BUSY; kept as its own register anyway for forward compatibility rather than hardcoding APP_STATUS_IDLE assumptions into callers
                len = 1;
                break;
            case REG_APP_VERSION:
                tx_buffer[0] = 0x00; // 0=application answering, mirrors the bootloader's own 1=bootloader convention on this same byte
                tx_buffer[1] = (uint8_t)(THIS_HARDWARE_ID >> 24);
                tx_buffer[2] = (uint8_t)(THIS_HARDWARE_ID >> 16);
                tx_buffer[3] = (uint8_t)(THIS_HARDWARE_ID >> 8);
                tx_buffer[4] = (uint8_t)(THIS_HARDWARE_ID);
                tx_buffer[5] = (uint8_t)(FIRMWARE_VERSION_MAJOR >> 8);
                tx_buffer[6] = (uint8_t)(FIRMWARE_VERSION_MAJOR);
                tx_buffer[7] = (uint8_t)(FIRMWARE_VERSION_MINOR >> 8);
                tx_buffer[8] = (uint8_t)(FIRMWARE_VERSION_MINOR);
                tx_buffer[9] = 0; // reserved, matches the bootloader's own 10-byte response length for a consistent read size regardless of who's answering
                len = 10;
                break;
            case REG_MLX_CAPTURE_STATUS:
                tx_buffer[0] = MLX_GetCaptureStatus();
                len = 1;
                break;
            case REG_MLX_RAW_CHUNK:
                MLX_GetRawChunk(pending_mlx_chunk_index, tx_buffer);
                len = 32;
                break;
            case REG_MLX_CALIBRATED_CHUNK:
                MLX_GetCalibratedChunk(pending_mlx_chunk_index, tx_buffer);
                len = 32;
                break;
            case REG_MLX_SENSOR_VARIANT:
                tx_buffer[0] = mlx_sensor_variant;
                len = 1;
                break;
            case REG_ADS_READ: {
                int16_t result = ADS1115_ReadResult();
                tx_buffer[0] = (uint8_t)(((uint16_t)result) >> 8);
                tx_buffer[1] = (uint8_t)(((uint16_t)result) & 0xFF);
                len = 2;
                break;
            }
            default:
                tx_buffer[0] = APP_STATUS_IDLE;
                len = 1;
                break;
        }
        HAL_I2C_Slave_Seq_Transmit_IT(hi2c, tx_buffer, len, I2C_LAST_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    uint32_t bytes_received = sizeof(rx_buffer) - hi2c->XferCount;
    if (bytes_received == 0) return;
    i2c_link_last_activity_tick = HAL_GetTick();

    uint8_t reg = rx_buffer[0];

    // REG_MLX_RAW_CHUNK/REG_MLX_CALIBRATED_CHUNK are the one case in this
    // protocol where a "read setup" write carries a payload byte (the
    // chunk index) alongside the register selector, rather than being a
    // bare 1-byte register-only write the way every other read-pointer
    // selection in this protocol works - handled first and explicitly so
    // the generic 1-byte-vs-more-bytes branch below doesn't need to know
    // about this one exception.
    if (bytes_received == 2 && (reg == REG_MLX_RAW_CHUNK || reg == REG_MLX_CALIBRATED_CHUNK)) {
        pending_read_register = reg;
        pending_mlx_chunk_index = rx_buffer[1];
        return;
    }

    // REG_MLX_SENSOR_VARIANT: also a 2-byte write (register + the new
    // variant value), but a plain configuration write rather than a
    // read-pointer selection - stored directly, no capture side effect.
    if (bytes_received == 2 && reg == REG_MLX_SENSOR_VARIANT) {
        mlx_sensor_variant = rx_buffer[1];
        return;
    }

    if (bytes_received == 1) {
        pending_read_register = reg;
        if (reg == REG_MLX_TRIGGER_CAPTURE) {
            MLX_TriggerCapture();
        } else if (reg == REG_ADS_TRIGGER) {
            ADS1115_TriggerConversion();
        }
        return;
    }

    switch (reg) {
        case REG_ENTER_BOOTLOADER:
            if (bytes_received == 5 && rx_buffer[1] == 0xB0 && rx_buffer[2] == 0x07 &&
                rx_buffer[3] == 0x1D && rx_buffer[4] == 0x5A) {
                // Same shutdown-then-reset pattern as the main board's own
                // 0x7F0 handler - stop any active local PWM output before
                // resetting, since a continuous-mode pulse (duration_ms=0)
                // would otherwise keep driving its GPIO right up until the
                // reset actually takes effect.
                PWM_StopAll();
                // A plain cycle-counted busy-wait, not HAL_Delay(5) - this
                // callback runs inside the I2C1 event ISR (priority 1,0 per
                // slave_main.c), and HAL_Delay only advances by way of the
                // SysTick ISR, which HAL_Init leaves at its default lowest
                // priority and therefore can never preempt an
                // already-running I2C1 ISR to actually increment the tick.
                // HAL_Delay(5) here would never return, and the chip would
                // only recover ~0.8s later via the IWDG forcing a reset
                // instead of this intended clean one. ~320000 iterations
                // at 64MHz (see slave_main.c's own SystemClock_Config)
                // matches this project's existing settle-loop convention
                // (firmware_control_thermal.c's own ~640-iteration/10us
                // busy-wait on the main board), scaled up to ~5ms.
                for (volatile uint32_t settle = 0; settle < 320000UL; settle++);
                NVIC_SystemReset();
                // never reached
            }
            break;
        case REG_ADS_CONFIGURE:
            if (bytes_received == 3) {
                uint16_t cfg = ((uint16_t)rx_buffer[1] << 8) | rx_buffer[2];
                ADS1115_Configure(cfg);
            }
            break;
        case REG_PWM_CONFIGURE:
            if (bytes_received == 4) {
                uint8_t channel = rx_buffer[1];
                uint16_t freq = ((uint16_t)rx_buffer[2] << 8) | rx_buffer[3];
                PWM_Configure(channel, freq);
            }
            break;
        case REG_PWM_PULSE:
            if (bytes_received == 4) {
                uint8_t channel = rx_buffer[1];
                uint8_t duty = rx_buffer[2];
                uint8_t duration = rx_buffer[3];
                PWM_Pulse(channel, duty, duration);
            }
            break;
        case REG_PWM_STOP:
            if (bytes_received == 2) {
                PWM_Stop(rx_buffer[1]);
            }
            break;
        default:
            break;
    }
}

void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    (void)hi2c;
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c) {
    HAL_I2C_EnableListen_IT(hi2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    HAL_I2C_EnableListen_IT(hi2c);
}
