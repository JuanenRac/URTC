// =============================================================================
// URTC Expansion Slave Application Firmware - Local PWM generation
// declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// TIM1 (this chip's own advanced control timer, 4 usable channels here)
// generating PWM right at the expansion board itself - see
// slave_common.h's own top-of-file note on why some tools want this
// generated locally rather than routed from the main board: timing-
// critical pulses (spot welder, ultrasonic transducer trigger, solder
// paste jetting) lose precision to cable length and connector routing
// the main board's own CONN_T12 path doesn't have to fight.
// =============================================================================
#ifndef SLAVE_PWM_H
#define SLAVE_PWM_H

#include <stdint.h>

void PWM_Init(void);
void PWM_Configure(uint8_t channel, uint16_t frequency_hz);
void PWM_Pulse(uint8_t channel, uint8_t duty_percent, uint8_t duration_ms); // duration_ms=0 means continuous, until PWM_Stop
void PWM_Stop(uint8_t channel);

// Stops every channel unconditionally - a no-op on any channel that isn't
// currently running. Used by slave_main.c's own link-comms-loss failsafe
// (see i2c_link_last_activity_tick in slave_i2c_link.h) so a continuous-mode
// pulse (duration_ms=0) doesn't keep driving its output forever if the main
// board goes silent, the one case PWM_Tick's own per-channel countdown can't
// catch on its own.
void PWM_StopAll(void);

// Called once per main loop iteration (see slave_main.c) - only does
// anything on channels with a nonzero pulse duration still counting
// down; continuous channels (duration_ms was 0) are untouched here and
// only ever stop via an explicit PWM_Stop.
void PWM_Tick(void);

#endif // SLAVE_PWM_H
