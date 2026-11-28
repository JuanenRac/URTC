# The silence of the master, announced to the tool heads

**Off as shipped.** The code is built only with `URTC_ESTOP_MASTER_ANNOUNCE=1` (compile only in CI); it has host tests and has never run on a board. The
official images do not contain it. It is not a certified safety function.

## What it does

With the `ESTOP_FOLLOW` switch on, a head goes back to its safe state when it stops hearing the master for its own silence (100 ms, or the 40 to 500 ms
stored in the head). With this option the master can announce its silence, and the head uses the **lower** of the two. A head is never more patient than
its own configuration.

## Frame

Standard identifier `0x002` (`URTC_ESTOP_ANNOUNCE_CAN_ID`), 8 data bytes:

| Byte | Meaning |
|---|---|
| 0 | `0x04` (announcement of the silence) |
| 1 | origin: `0x00`, the kinematic brain; any other value is ignored |
| 2-3 | silence in milliseconds, little endian; 0 gives the head its own value back |
| 4-5 | sequence, little endian, counted by the sender for this identifier |
| 6 | byte 2 XOR `0xFF` |
| 7 | byte 3 XOR `0xFF` |

## Rules

- Taken only while the heartbeat of the master (identifier `0x001`) is heard.
- The sequence must be newer than the last one taken; a stop (the head latched) forgets it, so a restarted master can count again.
- A value outside 40 to 500 ms, a wrong complement, length or origin: ignored. Nothing here ever stops a head or releases it.
- The value in use is what the query `0x1AF` with byte `03` answers. A restart of the head forgets the announced value.

## What it does not give

The origin byte is a claim, not a proof: the CAN bus has no sender authentication (`CAN_THREAT_MODEL.TXT`). A forged announcement can only make a head
stop sooner, which any node can already do with a stop frame.

## Pending

The sender: the controller board that forwards the stop to the tool head does not send or forward this identifier yet. The bench test on a real head.
