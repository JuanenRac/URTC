# How the expansion slave decides to install an image

The bootloader of the main board verifies an Ed25519 signature record before it installs anything (`BOOTLOADER_AUTH.md` and `SIGNATURE_SEPARATION.md` in URTC-COMMON). The bootloader of the expansion slave (STM32F303CB, 18 KB region, 13.7 KB used) has **no room** for the verifier: it would need about 7 KB more. The application of the slave is not moved to make room, so every tool that knows `0x08005000` keeps working.

The split is therefore:

| Who | What it checks |
|---|---|
| The **master** (main board, application) | The Ed25519 record of the slave image, under a key of the trust table of the master's own bootloader. The SHA-256 of the bytes it relays. |
| The **slave bootloader** | Size, hardware id, CRC-32, the SHA-256 of what it received against the digest it was sent, the version rule, and **that the master confirmed this image in this session**. Without the confirmation it installs nothing. |

## The messages

Registers of the slave bootloader on the I2C link bus (address `0x42`), next to the existing ones:

| Register | Access | Bytes | Meaning |
|---|---|---|---|
| `0x02` HMAC_EXPECTED | write | 32 | The SHA-256 (no key) of the whole image. The name is historic: the slave no longer checks an HMAC and its bootloader holds no signing secret in the update path. |
| `0x09` CHALLENGE | read | 16 | The nonce of the update session in progress (zeros when none). Made at `START_UPDATE`. |
| `0x0A` CONFIRM | write | 32 | The master's confirmation. Accepted only in status `0x08` (AWAIT_CONFIRM), and used once. |

Status `0x08` AWAIT_CONFIRM: size, CRC-32, SHA-256 and the version rule passed; the slave waits ten seconds for the confirmation. Failure reason `0x06` NOT_CONFIRMED: no confirmation came, or it does not match. Reason `0x03` keeps meaning "the digest does not match".

The confirmation is the SHA-256 of a 76-byte message that both sides build with `src/common/urtc_slave_confirm.h`:

```
"URTC-SLV-CONF-V1" (16) | nonce (16) | SHA-256 of the image (32) | hardware id (4) | version major (2) | version minor field (2) | size (4)
```

The slave builds the message from **its own** values (its nonce, the hash of its backup slot, the hardware id, version and size it was told) and compares the hash with the 32 bytes it received.

On the CAN side one frame is new: `0x21A` `SLAVE_RECORD_CHUNK`, eleven frames of an index and seven bytes that carry the 76-byte record (key id, counter, signature), exactly as `0x7EC` does for the main board. The `0x212` frames now carry the SHA-256 instead of an HMAC. Nothing else changes for a tool.

## The sequence

1. `0x211` START: the master relays it, starts hashing and loads the trusted keys from the trust table of its bootloader (flash `0x08000200`). The slave erases its backup slot and makes a nonce.
2. `0x212` digest, `0x213` data: relayed. The master hashes every byte it relays.
3. `0x21A` record: kept by the master, not sent to the slave.
4. `0x214` END: the master checks that every byte arrived and was relayed, rebuilds the 28-byte header (slave hardware id, size, CRC, version), and verifies the record under a trusted key over that header and the SHA-256 of the relayed bytes. **Only then** it relays END to the slave.
5. The slave checks CRC-32, SHA-256 and the version rule and goes to AWAIT_CONFIRM. Nothing is written to its metadata and its main slot is not touched.
6. The master waits (one second at most) for AWAIT_CONFIRM, reads the nonce from `CHALLENGE`, and writes the confirmation to `CONFIRM`.
7. The slave reads its backup slot **again**, rebuilds the message, compares the tag and, if it matches, writes the pending-copy metadata, copies the image to the main slot, reads the copy back against the digest and resets. A wrong tag, an altered image or no tag fails the update at once (main slot untouched, status VERIFY_FAIL).

The sequence has no step the slave can skip: with no confirmation it waits ten seconds and fails closed.

## What is covered

Host tests (`tests/test_slaveboot_update.c`, `tests/test_slave_attest.c`, `tests/test_can_slavebridge.c`) cover: a valid confirmation, an absent one, a wrong tag, a confirmation sent twice, a replay of the confirmation of an earlier session, a confirmation for another image, version or size, an image altered after the checks, no signature record, a record of another key, a record for the main board's hardware id, a damaged record, data that did not reach the slave, a power cut while the slave waits, and a power cut after the confirmation with the backup slot changed while the chip was off.

## Threat model, honestly

* **The slave trusts the master through the bus.** The confirmation is a hash, not a MAC: there is no secret shared between the two chips (adding one would put a fleet secret back in the image). Anything that can write to the I2C link bus can read the nonce and compute the tag. The signature stops the host, the CAN bus and the Flasher from installing an unsigned image on the slave **through a master that follows the protocol**; it does not stop a device that sits on the link bus between the master and the slave. The link bus is internal to the tool head and not reachable from the CAN connector, which is why this was accepted. Protecting it against a physical attacker is a matter of the debug-port and read-out protection of both chips.
* **The master is the trust anchor for the slave.** A compromised master application can confirm anything. The master's own bootloader still verifies the master application, so changing the master application needs a signature too.
* **The nonce is freshness, not randomness.** The chip has no random source: the nonce is the hash of the unique id, the tick, a counter that grows while the chip is powered, the SysTick value and the previous nonce. It makes a confirmation recorded in one session useless in the next one, not unpredictable to someone who sees the bus.
* **No rollback counter on the slave.** The signed counter is checked by the master's bootloader against its own metadata; the slave keeps none and the master has no access to one. The slave applies its version rule (an older version is refused) and nothing else. An image with an older counter but a newer version is therefore not refused by the counter. Giving the slave a counter needs a place to keep it that survives a metadata page erase.
* **The boot-time check is a keyless SHA-256** of the installed application, kept in the metadata: it catches corruption of the flash, not an attacker who can write the flash. Protecting the boot pages against being rewritten through the debug port is a chip setting that has to be reviewed on a board.
* **Verification runs in the application of the master**, not in its bootloader, because the master's application is what relays the slave's update. The trust anchor is still the table of the bootloader.
* **Legacy tools:** a flasher that still sends an HMAC in `0x212` and no `0x21A` record can no longer update the slave: the digest does not match and no confirmation comes. This is deliberate (fail closed); the tools carry the record in their next versions.
* **The slave bootloader still contains the old HMAC key** as a public development constant and the HMAC routine; neither is used by the update path any more. They are removed together with the release tooling that still reads that array.

## Costs and what is not measured

Nothing here has run on a board. Not measured: the time the verification takes on the master (it feeds the watchdog while it runs, and the CAN receive FIFO can overflow meanwhile, which is why tools wait for the status after the last frame), its stack use, the size of the slave bootloader with the change (the host build only proves it compiles; the real size is the one the firmware build reports against the 18432-byte region) and the real I2C timing of the confirmation. They are listed as bench tests in `BENCH_TESTS.md`.
