# Contributing to URTC Firmware 🚀

## Technology Stack
- **MCU**: STM32F303.
- **Language**: C (STM32 HAL).
- **Communication**: CAN 2.0B (bxCAN).

## Guidelines
1. **Tool Profiles**: Any new tool must be assigned a unique 5-bit ID and documented in `docs/CANBUS.TXT`.
2. **Thermal Safety**: Heaters must use the 250ms communication watchdog.
3. **Memory**: Persistence data must be written to F-RAM only when changed, to minimize I2C bus load.
4. **Bootloader**: Do not modify the bootloader entry window (~600ms) without testing `URTC Flasher` compatibility.
