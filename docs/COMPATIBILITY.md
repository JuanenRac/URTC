<!-- GENERATED from spec/versions.json by tools/compat_table.py - do not edit by hand. -->

## Compatibility

Protocol version **1** (identifiers, layouts and magic payloads of `spec/can_protocol.json`).

| Project | Kind | Version | Shared code |
|---|---|---|---|
| Tool head firmware (`URTC`) | firmware | 0.4.0 (main firmware 0.4.0, main bootloader 0.4.2, slave firmware 0.2.4, slave bootloader 0.2.6) | 0.0.7 |
| Flasher (`URTC-FLASHER`) | program | 0.4.0 | 0.0.7 |
| Gateway in front of the bus (`URTC-GATEWAY`) | program | 0.0.1 | 0.0.7 |
| Tester (`URTC-TESTER`) | program | 0.4.1 | 0.0.7 |
| Web studio (`URTC-WEB-STUDIO`) | program | 0.3.6 | 0.0.7 |
| Updater (`URTC-UPDATER`) | program | 0.1.7 | 0.0.7 |
| Smart rack (`URTC-SMART-RACK`) | firmware | 0.2.0 | 0.0.7 |
| Vision tool (`URTC-VISION-TOOL`) | firmware | 0.1.4 | 0.0.7 |
| Documentation generator (`URTC-DOCS`) | program | 0.1.0 | - |
| Shared protocol and code (`URTC-COMMON`) | library | 0.0.7 | 0.0.7 |

Every project listed speaks the protocol version above; "Shared code" is the version of URTC-COMMON the project carries a checked copy of.
