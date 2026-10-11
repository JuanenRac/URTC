# Security Policy 🔒 (URTC)

## Safety & Security

**URTC drives high-power actuators (Laser, Heaters). Security is Safety.**

Any flaw that allows:
- Overriding thermal watchdogs.
- Bypassing the laser interlock.
- Hijacking the CAN bootloader to flash unsigned firmware.

Must be reported **privately and immediately** to `electrohobby3d@gmail.com`.

## Design limits (not vulnerabilities)

- The CAN bus has no sender authentication: any node on it can command the
  tools, including arming the laser. Installations must meet the
  requirements in [`docs/CAN_THREAT_MODEL.TXT`](docs/CAN_THREAT_MODEL.TXT) -
  above all a hardware interlock independent of the MCU and a closed bus.
- Boards built with the public development signing key accept firmware
  signed by anyone - release boards are built with a private key, see
  [`docs/FIRMWARE_SIGNING.TXT`](docs/FIRMWARE_SIGNING.TXT).
