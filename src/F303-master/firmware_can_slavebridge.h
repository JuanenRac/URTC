// =============================================================================
// URTC Firmware - CAN-to-I2C bridge to the expansion slave chip
// (0x210-0x217 OTA relay, 0x220-0x221 application register access)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CAN_SLAVEBRIDGE_H
#define FIRMWARE_CAN_SLAVEBRIDGE_H

void Handle_CAN_SlaveBridge(void);
// Main loop: checks the signature record and confirms to the slave once the end frame of an update has been noted by the receive interrupt (see the .c).
void SlaveBridge_Service(void);
// The probe that says whether every actuator of the board is at zero (1) or one is active (0). While a tool is active the end of a slave update is refused
// (0x215 answers VERIFY_FAIL, 0x219 the reason ACTUATOR_ACTIVE): the check of the record would hold the safety ticks of the main loop for most of a second.
void SlaveBridge_SetActuatorProbe(uint8_t (*probe)(void));
// The switch ALLOW_UPDATE_ACTIVE_ACTUATOR: when the function answers 1 the refusal above does not apply and the end of the update is checked and forwarded even though a tool is active.
// Without the function, or while it answers 0 (the default of the switch), the refusal stands exactly as before.
void SlaveBridge_SetAllowActiveActuator(uint8_t (*allowed)(void));

#endif // FIRMWARE_CAN_SLAVEBRIDGE_H
