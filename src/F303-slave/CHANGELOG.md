# Changelog - Expansion Slave Application (`src/F303-slave/`, STM32F303CBT6)

Versioned entirely independently of the main board's own firmware/
bootloader - a separate chip, on a separate board, in a separate
source tree. The slave's own bootloader changelog lives at
[`boot/CHANGELOG.md`](boot/CHANGELOG.md); the main board's firmware
changelog lives at
[`../F303-master/CHANGELOG.md`](../F303-master/CHANGELOG.md).

| Version | Notes |
|---|---|
| **1.0** | Initial release. I2C1 link-bus protocol (this chip as slave) to the main board; I2C2 local sensor bus (this chip as master) driving an ADS1115 16-bit ADC and an MLX9064x thermal camera, built on Melexis's own official library rather than a hand-rolled register map; 4-channel local PWM generation (TIM1) for tools needing sub-millisecond pulse timing generated at the tool head itself. Later, still within this same version (no version bump, per this project's own rule that the slave's own application version only changes on explicit request): MLX90641 and MLX90642 support added alongside the original MLX90640 support - 2 genuinely separate Melexis libraries (`melexis_mlx90641/`, `melexis_mlx90642/`), not variants of the first one; MLX90641's own upstream form is this project's own first C++ code, built into an otherwise all-C firmware, while MLX90642 turned out genuinely simpler than the other two (no host-side calibration math at all - it reports temperature already calculated). A new register (`REG_MLX_SENSOR_VARIANT`) and matching main-board CAN command (`0x1A6`/`0x1A7`) let the host say which of the 3 MLX9064x family members is actually populated, relayed to this chip at boot since it has no persistent storage of its own. |
