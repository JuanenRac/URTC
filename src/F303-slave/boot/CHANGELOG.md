# Changelog - Expansion Slave Bootloader (`src/F303-slave/boot/`, STM32F303CBT6)

Versioned entirely independently of the main board's own firmware/
bootloader - a separate chip, on a separate board, in a separate
source tree. The slave's own application changelog lives at
[`../CHANGELOG.md`](../CHANGELOG.md); the main board's bootloader
changelog lives at
[`../../F303-master/boot/CHANGELOG.md`](../../F303-master/boot/CHANGELOG.md).

| Version | Notes |
|---|---|
| **1.0.1** | Same `hardware_id`/`size` validation-ordering fix as the main board's own bootloader v1.1.2 (see [`../../F303-master/boot/CHANGELOG.md`](../../F303-master/boot/CHANGELOG.md)) applied here too - inherited from the same original design, not introduced by the port. A 10s update-inactivity timeout now reverts a stalled transfer back to `STATUS_LISTENING`, matching the main board's own bootloader. `VERIFY_FAIL_REASON_*` (already defined, previously never assigned to anything) is now set alongside `STATUS_VERIFY_FAIL` and exposed through a new register, `REG_VERIFY_FAIL_REASON` (`0x07`), so a host can tell which specific check failed instead of only seeing the generic `STATUS_VERIFY_FAIL`. `update_progress_percent` now resets to its documented `0xFF` ("not updating") sentinel on an erase failure during `HandleStartUpdate`, instead of holding whatever percentage a prior update left behind. `HandleData`'s and `HandleEndUpdate`'s own page-flush-failure paths now clear `update_in_progress` alongside `update_failed`. |
| **1.0.0** | Initial release. Same A/B golden-image update model as the main board's own bootloader, scaled to this chip's 128KB flash, relayed over the I2C link rather than CAN directly - this chip has no CAN peripheral of its own. |
