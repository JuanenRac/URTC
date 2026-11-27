<!-- GENERATED from spec/versions.json by tools/compat_table.py - do not edit by hand. -->

## Compatibility

Protocol version **2** (identifiers, layouts and magic payloads of `spec/can_protocol.json`).

| Project | Kind | Version | Shared code |
|---|---|---|---|
| Tool head firmware (`URTC`) | firmware | 0.5.2 (main firmware 0.5.2, main bootloader 0.5.4, slave firmware 0.3.3, slave bootloader 0.3.6) | 0.3.2 |
| Flasher (`URTC-FLASHER`) | program | 0.6.0 | 0.3.0 |
| Gateway in front of the bus (`URTC-GATEWAY`) | program | 0.1.1 | 0.3.1 |
| Tester (`URTC-TESTER`) | program | 0.5.9 | 0.3.0 |
| Web studio (`URTC-WEB-STUDIO`) | program | 0.5.4 | 0.3.0 |
| Updater (`URTC-UPDATER`) | program | 0.3.4 | 0.3.2 |
| Smart rack (`URTC-SMART-RACK`) | firmware | 0.2.9 | 0.2.6 |
| Vision tool (`URTC-VISION-TOOL`) | firmware | 0.2.3 | 0.2.6 |
| Documentation generator (`URTC-DOCS`) | program | 0.1.9 | - |
| Shared protocol and code (`URTC-COMMON`) | library | 0.3.2 | 0.3.2 |

Every project listed speaks the protocol version above; "Shared code" is the version of URTC-COMMON the project carries a checked copy of.
