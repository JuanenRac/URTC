<p align="center">
  <img src="images/URTC_BANNER.svg" alt="URTC banner" width="100%">
</p>

# 🚀 URTC — Universal Robot Tool Controller

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  🇩🇪 <b>Deutsch</b> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a> |
  <a href="README_por.md">🇵🇹 Português</a>
</p>

### Der Werkzeugkopf-Controller eines Roboterarms: eine STM32F303-Platine mit 25 Werkzeugprofilen über CAN, einem CAN-Bootloader und einem Erweiterungs-Slave

<p align="center">
  <img src="https://img.shields.io/badge/Lizenz-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Tests-38%20suites-2ea44f.svg" alt="Tests">
  <img src="https://img.shields.io/badge/Hardware-CERN%20OHL--S-orange.svg" alt="CERN OHL-S">
  <img src="https://img.shields.io/badge/Sprache-C-00599C.svg" alt="C">
  <img src="https://img.shields.io/badge/Plattform-STM32F303-003551.svg" alt="STM32">
  <img src="https://img.shields.io/badge/Bus-CAN-yellow.svg" alt="CAN">
</p>

---

**Status: 🚧 Aktiv in Entwicklung befindliches Projekt — Releases existieren, aber nichts lief je auf einer echten Platine.** Es gibt getaggte Releases (v0.3.x), jeweils mit den aus diesem Quellcode gebauten Firmware-Images, aber es existiert keine bestückte Platine, sodass die Firmware nie auf einer echten Platine lief. URTC befindet sich in kontinuierlicher, aktiver Entwicklung auf beiden Fronten gleichzeitig: Firmware (neue Werkzeugprofile, das Erweiterungs-Slave-Ökosystem, Protokolländerungen) und Hardware (Schaltplan und Stückliste werden noch finalisiert, es existiert noch keine bestückte Platine). Da sich beide Seiten zusammen weiterbewegen, ist das, was sich zu einem gegebenen Zeitpunkt in diesem Repository befindet, eine Momentaufnahme laufender Arbeit, kein stabiles, versioniertes Produkt — Dateinamen, Ordnerstruktur, Werkzeuganzahl und Dokumentation können sich alle noch ändern, während sich das Design festigt. Sobald sowohl Firmware als auch Hardware einen wirklich stabilen, auf echter Hardware verifizierten Zustand erreichen, wird ein richtiges **Release** getaggt, das alles zusammen bündelt (Firmware, Bootloader, PC-Tools, Hardware-Design-Dateien und Dokumentation) als kohärente, eingefrorene Momentaufnahme. Bis dahin behandeln Sie `main` als das aktiv bewegliche Ziel, das es ist.

---

> **⚠️ Sicherheitshinweis (Entwurfsstadium, noch keine Platine hat diese Firmware ausgeführt):** Diese Platine steuert eine **10W-Gravier-Laserdiode** und mehrere Heizstufen (T12-Lötkolben-Patrone, 3D-Drucker-Hotend). Der Bau und die Nutzung bedeuten den Umgang mit Ausrüstung, die bei Montage oder Betrieb ohne angemessene Sicherheitsmaßnahmen (Laserschutzbrille passend zur Wellenlänge der Diode, thermischer Schutz, eine zugängliche Stromabschaltung) **Verbrennungen, Brand oder Augenschäden** verursachen kann. Dies ist ein Hobbyisten-/Maker-Projekt, das im Ist-Zustand geteilt wird — Bau und Nutzung auf eigenes Risiko, und grundlegende Sicherheitspraxis sollte nicht übersprungen werden, nur weil die Firmware Watchdogs hat (Software-Watchdogs, die nie auf einer echten Platine liefen).

> **Stand von Sicherheit (Safety) und Security:** Dieses Ökosystem ist kein zertifiziertes Sicherheitssystem, und es existiert noch keine bestückte URTC-Platine. Die Bootloader prüfen Firmware-Images mit CRC32 und einem HMAC-SHA256 mit gemeinsamem Schlüssel, nicht mit einer asymmetrischen Signatur (geplant); die veröffentlichten Anwendungs-Images sind mit dem öffentlichen Entwicklungsschlüssel signiert (`signing_key_kind: dev`), sodass jeder ein vom Bootloader akzeptiertes Image erzeugen kann; an eine echte Platine wurde noch nichts gesendet. Lesen Sie die [Sicherheitsbasis](https://github.com/JuanenRac/URTC-DOCS/blob/main/docs/SECURITY_BASELINE.md) und das [Bedrohungsmodell](https://github.com/JuanenRac/URTC-DOCS/blob/main/docs/THREAT_MODEL.md), bevor Sie sich für die Sicherheit einer Person auf eine Aussage hier verlassen.

## 📋 Auf einen Blick

| | |
|---|---|
| Version | `0.4.2` |
| Reifegrad | Funktionsfähig |
| Technik | STM32F303 / bxCAN / 25 tool profiles |
| Ziel | Eine Mikrocontroller-Platine (die Firmware); wird auf einem PC gebaut |
| Auf echter Hardware | Noch nicht: für diese Version ist kein Lauf auf echter Hardware vermerkt |

**Springe zu:** [Überblick](#-überblick) · [Architektur](#-architektur) · [Funktionen](#-funktionen) · [Sicherheit](#-sicherheit-und-schutz) · [Schnittstellen](#-schnittstellen) · [Schnellstart](#-schnellstart) · [Tests](#-tests) · [Struktur](#-struktur-des-repositorys) · [Verwandte](#-verwandte-projekte)

<!-- urtc-section:status_firmware -->
## 🚦 Projektstatus

🟢 fertig und überprüft · 🟡 funktioniert, mit bekannten Grenzen · 🟠 halb fertig oder nicht überprüft · 🔴 nicht gemacht oder funktioniert nicht. Nichts ist grün, wenn es nie auf echter Hardware lief.

| Teil | Status | Was das Repository belegen kann |
| --- | :---: | --- |
| Software | 🟡 | Baut und besteht die Tests am PC; der Lauf an einem echten Werkzeugkopf steht noch aus. |
| Tests | 🟢 | Die Testsuite besteht bei jedem Push in der CI. |
| Echte Hardware | 🟠 | Noch kein Lauf an einer echten Platine: der Bankversuch steht noch aus. |
| Sicherheit | 🟡 | Releases werden mit Ed25519 signiert und vor der Nutzung geprüft; der Umgang mit den Schlüsseln wurde von niemand anderem geprüft. |
| Dokumentation | 🟡 | README in acht Sprachen und Referenzdokumente; die Übersetzungen wurden nicht von Muttersprachlern geprüft. |

<!-- /urtc-section:status_firmware -->

## 🎯 Überblick

Hallo zusammen! Ich wollte ein Projekt teilen, das ich entwickle: URTC (Universal Robot Tool Controller). Es handelt sich um eine monolithische, hochintegrierte Steuerplatine, die speziell dafür entwickelt wurde, die Fähigkeiten von Roboterarmen und Automatisierungs-Setups zu erweitern, wodurch sie perfekt zu Plattformen wie PAROL6 und Faze4 passt — zwei Open-Source-Roboterarme, entworfen und entwickelt von [Source-Robotics](https://source-robotics.com/) ([GitHub](https://github.com/Source-Robotics)).

**URTC ist ein unabhängiges, inoffizielles Projekt.** Es wird nicht von Source-Robotics entwickelt oder unterstützt — es ist ein kompatibler Werkzeugkopf-Controller, der gut mit PAROL6 und Faze4 zusammenarbeitet, und dieselbe CAN-basierte Architektur lässt sich auch für andere Roboterarm-Plattformen anpassen.

Hier ist die vollständige Aufschlüsselung dessen, was es ist, was es tut, und des Hardware-Ökosystems, das es derzeit verwaltet.

URTC ist eine All-in-One-, kompakte Steuerplatine, angetrieben von einem STM32-Mikrocontroller (STM32F303CCT6, LQFP48). Sie kommuniziert mit dem Haupt-Robotercontroller über CAN-Bus, was die Ausführung komplexer Aufgaben mit geringer Latenz direkt am Werkzeugkopf oder an der Achse ermöglichen soll (die Latenz wurde nie auf einer Platine gemessen, und harte Echtzeit wird nicht zugesichert). Sie verfügt über ein integriertes OLED-Display für sofortige Diagnose — animierter Boot-Splash, animierte Icons pro Werkzeug, Live-Telemetrie auf einem zweifarbigen Panel —, eine Einzelpixel-RGB-Status-LED plus einen adressierbaren RGB-LED-Ring für die Kamerabeleuchtung, einen 20-poligen Erweiterungsstecker für Zusatzplatinen, ein integriertes F-RAM, das die Sollwerte des aktiven Werkzeugs über einen Stromausfall hinweg erhält, sowie dedizierte Analog- und Hochstrom-Leistungsstufen.

## 🧩 Rolle in URTC

URTC ist die Platine und ihre Firmware: der Teil des Ökosystems, der am Werkzeugkopf des Roboters sitzt und die Arbeit erledigt, mit einem CAN-Bootloader, der es erlaubt, die Anwendung über dasselbe Kabel zu aktualisieren. Alles andere in der Familie ist um sie herum gebaut. URTC-COMMON schreibt ihr CAN-Protokoll einmal als Daten, URTC-FLASHER und URTC-TESTER aktualisieren und prüfen sie von einem PC aus (URTC-WEB-STUDIO tut beides im Browser), URTC-GATEWAY kann sich vor ihren Bus stellen, URTC-SMART-RACK bewahrt die Köpfe auf, URTC-VISION-TOOL ist ein Kopf, der prüft, URTC-UPDATER hält die Programme aktuell, und URTC-DOCS enthält die Architektur und das, was belegt ist. Sie ist für Roboterarme wie PAROL6 und Faze4 gedacht und ein unabhängiges, inoffizielles Projekt. Eine bestückte Platine gibt es noch nicht.

## 🏛️ Architektur

<p align="center">
  <img src="images/architecture.svg" alt="Architektur von URTC: wer es nutzt, was es enthält und wovon es abhängt" width="100%">
</p>

Wie die Teile dieses Repositorys zusammenpassen: wer es nutzt, was es enthält und wovon es abhängt. Das Bild wird aus `docs/architecture.json` gezeichnet und gegen den Code geprüft.

### Skalierbare Architektur & Werkzeugmatrix

Die Kernstärke von URTC ist seine extreme Vielseitigkeit. Statt für jeden anderen Job die Elektronik auszutauschen, verfügt die Platine über eine skalierbare Matrix-Architektur:

* **32-Adressen-Identifikationsschema:** Hardware und Kommunikationsprotokoll sind so konzipiert, dass sie bis zu 32 verschiedene Werkzeuge oder Endeffektoren direkt am Roboterkopf identifizieren können, über eine 5-Bit-Lötbrücken-ID-Matrix (ID0-ID4). Von diesen 32 Ablesungen entsprechen 31 direkt einem Werkzeugprofil; die 32. (alle 5 Jumper gesetzt, `11111`) ist stattdessen als "freie Konfiguration"-Adresse reserviert - siehe unten.
* **25 Plug-and-Play-Automatisierungsprofile:** Die Firmware verwaltet nativ 25 Werkzeugprofile - die Platine liest die physische Identität des Werkzeugkopfs und konfiguriert die Leistungsstufen, Sensoren und Logikumschaltung nahtlos, ohne dass ein vollständiges Neu-Flashen erforderlich ist. 6 weitere Adressen bleiben innerhalb des bestehenden Schemas für zukünftige Werkzeugprofile frei.
* **Freie Werkzeugkonfiguration:** Die reservierte `11111`-Jumper-Ablesung wählt kein festes Werkzeug - sie weist die Platine an, stattdessen in einem Register in ihrem eigenen persistenten F-RAM nachzuschlagen, welches Werkzeug zu verwenden ist, im Voraus über CAN gesetzt (via `URTC Flasher`). Nützlich für eine Platine, die auf ein anderes Werkzeug umprogrammiert werden muss, ohne die Jumper physisch umzulöten. Siehe `docs/EEPROM.TXT` Abschnitt 5 für den vollständigen Mechanismus.

### Flash-Speicherlayout (256K insgesamt, Golden-Image-/A-B-Update-Modell)

```
0x08000000 ┌─────────────────────────────────┐
           │  Bootloader (30K)                 │  Läuft bei jedem Boot immer zuerst.
           │                                   │  Lauscht kurz auf CAN, springt dann
           │                                   │  entweder zur Anwendung oder wartet
           │                                   │  auf ein Update. Steuert das OLED
           │                                   │  während eines Updates direkt an
           │                                   │  (siehe unten).
0x08007800 ├─────────────────────────────────┤
           │  Metadaten-Seite (2K)             │  Beschreibt, was sich gerade im
           │                                   │  Hauptslot befindet: HardwareID,
           │                                   │  Version, Größe, CRC32, und eine
           │                                   │  HMAC-SHA256-Signatur. Der
           │                                   │  Bootloader prüft alles davon,
           │                                   │  bevor er jemals zur Anwendung
           │                                   │  springt.
0x08008000 ├─────────────────────────────────┤
           │  Hauptslot (112K)                 │  Dies ist die Anwendungs-Firmware /
           │                                   │  URTC_MAIN_FIRMWARE_v<version>.* — die
           │                                   │  eigentliche Firmware, die
           │                                   │  Tag für Tag läuft, beschrieben
           │                                   │  überall sonst in diesem README.
           │                                   │  Wird von einem Update nie
           │                                   │  angetastet, bevor ein
           │                                   │  verifiziertes, als gut bekanntes
           │                                   │  Image bereit ist, es zu ersetzen.
0x08024000 ├─────────────────────────────────┤
           │  Backup-/Staging-Slot (112K)      │  Nur Rohspeicher, nie direkt
           │                                   │  ausgeführt. Jedes CAN-Update
           │                                   │  schreibt zuerst hierher.
0x08040000 └─────────────────────────────────┘
```

**Warum ein Backup-Slot.** Ein CAN-Update wird nie in den Slot geschrieben, der gerade läuft. Es geht zuerst in das Backup, wird dort vollständig verifiziert — Größe, CRC32, und eine HMAC-SHA256-Prüfung mit einem gemeinsamen symmetrischen Schlüssel, die zeigt, dass es von jemandem mit diesem Schlüssel erzeugt wurde (die veröffentlichten Images verwenden den öffentlichen Entwicklungsschlüssel), nicht nur, dass es intakt angekommen ist — und wird erst dann in den Hauptslot kopiert. Ein Stromausfall an jedem Punkt vor Beginn dieses Kopiervorgangs lässt die aktuell laufende Firmware vollständig unangetastet, sodass es kein Fenster gibt, in dem ein unterbrochener Download die Platine unbrauchbar machen könnte. Wenn der Stromausfall *während* des Kopiervorgangs selbst passiert, bemerkt es der Bootloader beim nächsten Boot (das Backup, während des Kopierens nie angetastet, ist noch vollständig intakt) und setzt das Kopieren von dort einfach fort, bis es gelingt.

## 🖼️ Screenshots

![URTC v1.0](images/URTC_BOARD.png)

*(In Arbeit — weitere Ansichten und eine bestückte Platine folgen bald.)*

## ✨ Funktionen

### Hardware-Flexibilität & Motorunterstützung

Um eine so große Vielfalt an Anwendungen zu bewältigen, ist die URTC-Hardware voll ausgestattet, um zu steuern:

* **NEMA-Schrittmotoren:** NEMA 8, 11, 14 und 17 laufen direkt über den integrierten TMC2209, ebenso wie NEMA 23 und 34 — bis zu **2,0A** bei jedem davon über die Treiberstufe der Hauptplatine. Für NEMA 23/34 bei ihrem vollen Nenndrehmoment unterstützt ein TMC5160 am Erweiterungsstecker (siehe unten) bis zu **10A**, stromskaliert durch die für diese Platine gewählten externen MOSFETs/den Messwiderstand — die integrierte 2,0A-Grenze gilt nicht mehr, sobald ein Motor zum Erweiterungstreiber gewechselt ist.
* **3-Phasen-BLDC-/Gimbal-Motoren** für hochpräzise Bewegung.
* **Motoren mit Hall-Sensoren und Tachometern** für Regelung mit geschlossenem Regelkreis.
* **Dedizierte Eingänge** für reflektive optische Näherungssensoren wie den TCRT5000, plus einen generischen Active-Low-Endstop-/Endschalter-Eingang, der von vier Werkzeugprofilen gemeinsam genutzt wird.

### Erweiterungsstecker

Ein 20-poliger Header, getrennt von den werkzeugspezifischen Steckern, für Zusatzplatinen, die mehr benötigen, als ein gegebenes Werkzeugprofil allein bietet — eine zusätzliche Schrittmotor-Achse (TMC2209 oder TMC5160), eine zweite Sensorplatine, dergleichen.

| Pins | Signal |
|---|---|
| 4 | 24V |
| 1 | 3,3V |
| 1 | 5V |
| 3 | GND |
| 2 | Bit-gebanktes I2C (SCL/SDA) — eigener Bus, getrennt vom Hardware-I2C2 des OLED/F-RAM |
| 3 | STEP/DIR/EN — universell für beide unten genannten Treiberchips |
| 4 | Bit-gebanktes SPI (CS/SCK/MISO/MOSI) — für die Konfigurations-/Diagnoseschnittstelle eines TMC5160, oder jeden anderen SPI-konfigurierbaren Chip |
| 1 | Universeller GPIO (EXTI-fähiger Interrupt-Eingang, falls ein zukünftiges Zusatzmodul eine schnelle Sensorreaktion benötigt, z. B. einen Endstop) |
| 1 | TMC5160 DIAG0 (Stall-/Fehler-Diagnoseleitung, abgefragt über `0x182`/`0x183`) |

Insgesamt 20 Pins.

**Zwei separate I2C-Busse mit Absicht:** Das OLED/F-RAM verwendet die einzige nutzbare Hardware-I2C-Peripherie dieses Chips (I2C2, an PA9/PA10); der Erweiterungsstecker erhält seinen eigenen, unabhängigen bit-gebankten I2C-Bus (PB10/PB11 - die einzigen anderen I2C-fähigen Pin-Paare dieses Chips waren bereits anderen Funktionen zugeteilt, sodass Bit-Banging der Weg war, diesem Stecker einen eigenen Bus ohne Hardware-Konflikt zu geben). Alles, was am Erweiterungsheader hängt — ein I2C-ADC/DAC, ein Port-Expander, was auch immer eine gegebene Zusatzplatine benötigt — teilt sich diesen bit-gebankten Bus mit jedem anderen erweiterungsseitigen I2C-Gerät, kann aber den Takt nicht dehnen oder anderweitig das eigene Timing des OLED auf seinem separaten Hardware-I2C2-Bus stören.

**Ein TMC2209 oder ein TMC5160, nicht notwendigerweise beide.** Beide Chips verwenden für die eigentliche Bewegung dieselbe STEP/DIR/EN-Schnittstelle, dieser Teil ist also universell. Wo sie sich unterscheiden, ist Konfiguration/Diagnose: Ein TMC2209 verwendet dafür sein eigenes Single-Wire-UART, während ein TMC5160 SPI verwendet — und da die beiden sich auf jeder gegebenen Erweiterungsplatine gegenseitig ausschließen, verdoppeln sich die 4 SPI-Pins auf natürliche Weise auch als Heimat für die einzelne UART-Leitung eines TMC2209, statt einen weiteren dedizierten Pin zu benötigen, den niemand gleichzeitig mit dem SPI-Bus nutzt. Der bit-gebankte SPI-Bus spricht genau das Protokoll, das ein TMC5160 erwartet (SPI-Modus 3, MSB zuerst, CS für die gesamte Transaktion niedrig gehalten — siehe `docs/CANBUS.TXT`s `0x180`/`0x181` für den generischen Byte-Passthrough-Befehl, der ihn ansteuert), statt dass diese Firmware das spezifische Registerlayout dieses Chips kennen müsste. Die DIAG0-Stall-/Fehlerleitung eines TMC5160 ist ebenfalls verdrahtet (`0x182`/`0x183`) — sie nutzt einen der beiden universellen GPIO-Pins wieder, die bereits genau für diese Art von schnellem interruptgesteuertem Eingang vorgesehen waren.

Vollständige Pin-für-Pin-Details — welcher MCU-Pin welches Signal trägt, und die Begründung hinter ein paar Layout-Einschränkungen, die das 48-Pin-Gehäuse dieses Chips mit sich bringt — befinden sich in `docs/PINOUT_CONNECTORS.TXT` und `src/F303-master/README.md`.

#### Die 6 Erweiterungsplatinen-Varianten

4 der 6 Erweiterungsplatinen-Varianten tragen einen Schrittmotor-Treiber — entweder einen TMC2209 (bis zu 2A/Spule, integrierte Leistungs-MOSFETs) oder einen TMC5160A (bis zu 10A+/Spule, benötigt 8 externe Leistungs-MOSFETs, die der Treiber selbst nicht enthält). Unabhängig von dieser Treiberwahl ist eine treibertragende Platine entweder **basic** (nur Treiber + Stecker, kein MCU — STEP/DIR/EN direkt von der Hauptplatine geroutet) oder **advanced** (fügt einen zweiten Mikrocontroller hinzu, STM32F303CBT6, plus 2 lokale Sensorchips — einen ADS1115 16-Bit-ADC und eine Wärmebildkamera der MLX9064x-Familie — sowie lokale PWM-Erzeugung für Werkzeuge, deren Timing direkt am Werkzeugkopf erzeugt werden muss statt über ein Kabel geroutet zu werden). 2×2-Kombinationen, plus 2 weitere reine Sensor-Basic-Platinen (ADS1115 oder MLX9064x, direkt mit dem eigenen STM32F303CC der Hauptplatine verdrahtet, kein Treiber und kein Slave-MCU) für ein Werkzeug, das nur einen dieser 2 Chips benötigt und nichts, was eine advanced-Platine sonst noch trägt — insgesamt 6 Platinen — siehe `BOM/BOM_EXPANSION_*.TXT` (6 Dateien), `docs/EXPANSION.TXT` und `docs/PINOUT_SLAVE.txt`.

Der eigene STM32F303CBT6 der advanced-Variante kommuniziert mit der Hauptplatine über den bestehenden bit-gebankten I2C-Bus des Erweiterungssteckers oben — Hauptplatine als Master, Slave-Chip antwortend als echter Hardware-I2C-Slave — und betreibt seinen eigenen zweiten, rein lokalen I2C-Bus für die 2 Sensorchips. Er hat seinen eigenen Bootloader und seine eigene Anwendungs-Firmware, aktualisiert auf dieselbe Weise wie die Hauptplatine (CAN-OTA von `URTC Flasher`), nur über diese I2C-Verbindung weitergeleitet, statt den Slave-Chip direkt zu erreichen. Siehe `src/F303-slave/README.md` und `src/F303-slave/boot/README.md` für die vollständigen technischen Details.

### Parameter-Persistenz

Ein integriertes FM24CL64B F-RAM (64Kbit, I2C) hält eine periodisch aktualisierte Momentaufnahme der Sollwerte des aktiven Werkzeugs und der globalen LED-/OLED-Einstellungen, sodass ein plötzlicher Stromausfall nicht "was hat diese Platine gerade getan" ebenso unbekannt zurücklässt, wie der Verlust selbst ungeplant war. Es teilt sich den Hardware-I2C2-Bus des OLED, statt einen eigenen zu bekommen — diese MCU hat nur eine nutzbare Hardware-I2C-Peripherie für diesen Zweck, bereits vom OLED beansprucht (siehe `src/F303-master/README.md` Abschnitt 6 für die vollständige Begründung).

**Wiederhergestellter Zustand ist abfragbar, wird aber nie automatisch auf etwas Gefährliches angewendet.** Beim Booten wird alles, was gespeichert wurde, über CAN lesbar (`0x190`/`0x191`) — aber ein Heizungs-Sollwert, eine Laserleistung oder ein Motorbefehl wird nie stillschweigend von selbst wieder scharfgeschaltet. Nur die sicheren, passiven Einstellungen (LED-Farben, OLED-Modus) werden direkt wiederhergestellt. Einen Sollwert nach tatsächlicher Überprüfung des Geschehens absichtlich erneut zu senden, bleibt die Entscheidung des Master-Controllers, nicht etwas, das diese Platine im Moment der Stromwiederkehr selbst entscheidet.

### Nativ automatisierter Werkzeugkatalog (25 Firmware-Profile)

Durch seine dynamische Umschaltlogik verwaltet die Firmware nativ die folgenden Werkzeugköpfe:

1. **Lötstation (T12):** Temperaturregelung mit Hysterese (Ein/Aus, kein PID) mittels direkter ADC-Rückmeldung zur Handhabung von Standard-T12-Lötspitzen, plus ein motorisierter Drahtvorschub, der Lötdraht in die Verbindung spult (teilt sich `CONN_MOT` und dessen Schrittmotor-Protokoll mit den reinen Bewegungswerkzeugen unten - tauscht den eigenen generischen Endstop-Eingang dieses Werkzeugs ein, um dafür Platz zu schaffen). [Jumper-/Verdrahtungskonfiguration →](images/TOOL_SOLDERING_IRON.png)
2. **SMT-Lötpasten-Dispenser:** millimetergenaue Vorschubregelung für präzise Lötpasten-Auftragung auf Leiterplatten. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_PASTE_DISPENSER.png)
3. **Thermalpaste-/Flüssigkeits-Dispenser:** Fließfähigkeitsmanagement für hochviskose Pasten oder flüssige Klebstoffe. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_LIQUID_DISPENSER.png)
4. **Intelligenter Elektro-Schraubendreher:** Rotations- und Stoppsteuerung über die Schrittmotor-Schnittstelle. Geplant: Stopp bei Drehmomentgrenzen (eine Drehmomentmessung existiert noch nicht). [Jumper-/Verdrahtungskonfiguration →](images/TOOL_SCREWDRIVER.png)
5. **Vakuum-/Pneumatik-Greifer:** Vakuumpumpensteuerung und Druckniveau-Ablesung für sichere Pick-and-Place-Operationen. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_VACUUM_PICKUP.png)
6. **Bohrer (BL4260):** PWM-Drehzahlregelung, Richtungsumschaltung und dynamisches elektrisches Bremsen mit Live-Drehzahlablesungen, auf einer eigenen dedizierten Freigabe-/Bremsleitung, unabhängig von der Freigabe des Schrittmotor-Werkzeugtreibers. Generischer Endstop-Eingang verfügbar. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_DRILL.png)
7. **Gimbal-Greifer:** hochempfindliche Handhabung mittels 3-Phasen-Brushless-Gimbal-Motoren. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_GRIPPER_GIMBAL.png)
8. **NEMA-Greifer:** robuste Klemmkraft, gesteuert über einen Hochleistungs-Schrittmotor. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_GRIPPER_NEMA.png)
9. **AOI-System (Automated Optical Inspection):** synchrone stroboskopische Steuerung des LED-Beleuchtungsarrays für die Aufnahme mit Machine-Vision-Kameras. Generischer Endstop-Eingang verfügbar. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_AOI_INSPECTION.png)
10. **Gravier-Laserdiode (10W optisch):** PWM-Strahlleistungsmodulation mit einem Software-CAN-Watchdog, der den Laserausgang sperrt, wenn die Host-Kommunikation verloren geht (gebaut, nie auf einer echten Platine ausgeführt und kein Ersatz für eine unabhängige Hardware-Verriegelung). Generischer Endstop-Eingang verfügbar. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_LASER_ENGRAVER.png)
11. **3D-Druck-Hotend:** Ein/Aus-Regelung mit Hysterese der Heizpatrone, NTC-Thermistor-Ablesung, Extrudersteuerung, und ein dedizierter, 25kHz-PWM-gesteuerter Schichtkühllüfter (4-Draht, Tachometer-Rückmeldung, eigener Kommunikations-Watchdog) — alles integriert in einem einzigen Block. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_3D_PRINTER.png)
12. **3D-Scan-Sonde:** ultraschneller Hardware-Interrupt-Eingang (EXTI) mit absoluter Priorität für die verzögerungsfreie Echtzeit-Oberflächendigitalisierung und Stoßerkennung. Deckt auch metrologisches Tastprüfen ab - derselbe Hardware-Pfad, eine andere physische Sonde am selben Werkzeugkopf. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_SCAN_PROBE.png)
13. **SMT-Pick-&-Place-Kopf:** Rotations-A-Achse für korrekte Pad-Ausrichtung, auf derselben Schrittmotor-Schnittstelle wie die Pasten-/Flüssigkeits-Dispenser und beide Greifer oben. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_SMT_PICKPLACE.png)
14. **Schwerlast-Elektromagnet:** Ein/Aus-Aufnahmesteuerung für ferromagnetische Teile, über den als generischen GPIO-Treiber umfunktionierten T12-Heizungsausgang. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_ELECTROMAGNET.png)
15. **Punktschweißkopf:** millisekundengenaue Schweißimpulse für Nickelstreifen von Batteriepacks, mit einem Oberflächenkontaktsensor, der den Impuls freischaltet. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_SPOT_WELDER.png)
16. **Konformbeschichtungs-Airbrush:** Schutzbeschichtungs-Sprühsteuerung für fertige Leiterplatten - das Sprühventil und sein eigener Sensor befinden sich auf der eigenen Hauptplatine des Roboters, außerhalb des eigenen Umfangs dieser Platine. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_CONFORMAL_COATING.png)
17. **Großformat-Vakuumgreifer:** Mehrfach-Saugnapf-Array für unbestückte FR4-Platinen, auf derselben Schrittmotor-Schnittstelle wie Werkzeug Nr. 13 oben. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_VACUUM_GRIPPER_LG.png)
18. **Funktionstestkopf:** Flying-Probe-Spannungs-/Durchgangsprüfung — grundlegende Ablesung über den integrierten ADC, erweiterte Ablesung über einen ADS1115 16-Bit-ADC auf einer **advanced**-Erweiterungsplatine. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_FLYING_PROBE.png)
19. **UV-Härtungskopf:** Hochleistungs-UV-LED-Treiber für sofortige Kleber-/Masken-Härtung. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_UV_CURING.png)
20. **Heißluft-Rework-Düse:** Heizelement, Turbinengebläse und Thermoelement-Rückmeldung zum Nachfließen fehlausgerichteter SMD-Teile - teilt sich den eigenen Wärmeregelkreis des Lötkolbens. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_HOTAIR_REWORK.png)
21. **Pneumatischer Press-Fit-Einsetzer:** Linearaktuatorsteuerung zum Eindrücken von Steckverbindern in Leiterplatten - der Aktuator und sein eigener Sensor befinden sich auf der eigenen Hauptplatine des Roboters, außerhalb des eigenen Umfangs dieser Platine. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_PRESSFIT_INSERTER.png)
22. **Kabelbaum-/Crimp-Aktuator:** Hochdrehmoment-Backe zum Abisolieren/Crimpen von Anschlüssen, angetrieben über den **eigenen Treiber einer Erweiterungsplatine** statt den der Hauptplatine. Die Firmware hat keine Drehmoment- oder Kraftmessung. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_CRIMPING_ACTUATOR.png)
23. **Erweiterte Leiterplatteninspektion:** Wärmebildgebung (Array der MLX9064x-Familie - alle 3 Familienmitglieder, MLX90640/MLX90641/MLX90642, heute unterstützt, entweder über den eigenen Slave-Chip einer **advanced**-Erweiterungsplatine oder eine **basic** MLX9064x-Erweiterungsplatine, direkt mit der Hauptplatine verdrahtet), um Kurzschlüsse anhand der Temperatursignatur zu erkennen, zusammen mit Ring-LED-Beleuchtung. Deckt auch Mikrospindel-Depaneling ab - derselbe Bohrer-Hardware-Pfad oben, ein anderes Bit für einen anderen Job. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_THERMAL_INSPECTION.png)
24. **Lötpasten-Jetting-Ventil:** piezoelektrisches Mikrotröpfchen-Dispensieren, sub-millisekundengenaue Impulspräzision, lokal auf einer **advanced**-Erweiterungsplatine erzeugt. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_PASTE_JETTING.png)
25. **Ultraschallschweißer / Verpackungsversiegler:** Hochfrequenz-Wandlerauslösung zum Schweißen von Kunststoffgehäusen. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_ULTRASONIC_WELDER.png)

*(Werkzeugkonfigurationsbilder existieren für die Werkzeuge 1-12; Bilder für die Werkzeuge 13-25 werden ergänzt, sobald die Hardware-Dokumentation aufholt — die obigen Dateinamen entsprechen der bereits in `images/` verwendeten Namenskonvention.)*

### Lokale OLED-Schnittstelle

Jeder Werkzeugkopf zeigt Live-, werkzeugspezifische Telemetrie auf einem 128×64-zweifarbigen OLED: einen animierten Boot-Splash beim Einschalten, einen blinkenden CAN-Aktivitätsindikator, eine Live-"Hero"-Ablesung im oberen Streifen (Temperatur, Drehzahl, Leistung — was auch immer für das aktive Werkzeug am wichtigsten ist), und ein kleines vier-Bilder-animiertes Icon pro Werkzeugprofil.

#### Das Modul

Beide physischen Varianten unten sind elektrisch dasselbe Panel (SSD1306- oder SSD1315-gesteuert — die Init-Sequenz der Firmware ist verifiziert kompatibel mit beiden, siehe `OLED_Init()` in `firmware_oled_driver.c`; der SSD1315 ist ein neuerer, direkt austauschbarer Ersatz-Controller, mit dem viele Module heute unter derselben "SSD1306"-Auflistung/Bedruckung ausgeliefert werden), **128×64**, und dieselbe zweifarbige "Gelb/Blau"-Aufteilung, bei der das physische LED-Material selbst in zwei feste Farbzonen unterteilt ist (dies ist nicht softwareauswählbar):

* **Obere 16 Pixel (Seiten 0-1): gelb.** URTC nutzt diesen Streifen für das, was am nützlichsten ist, um es auf einen Blick zu sehen, ohne genau zu lesen — den CAN-Aktivitätsindikator, Live-Hero-Ablesungen oder (auf dem Boot-Splash-/Ungültiges-Werkzeug-Bildschirm) kurzen Statustext.
* **Untere 48 Pixel (Seiten 2-7): blau.** Alles andere — Werkzeug-Icons, detaillierte Telemetrie, das animierte JuanenBOT-Gesicht auf dem Splash-Bildschirm, der große blinkende ERROR-Schriftzug.

Beide landen auf demselben I2C2-Bus und demselben `OLED_Init()` — die Firmware kann nicht erkennen, welches der beiden angeschlossen ist, und muss es auch nicht. Sie schließen sich auf einer gegebenen Platine gegenseitig aus (siehe `BOM/BOM.TXT`s Hinweis zu `CONN_OLED2` - der Name dieses Dokuments für das, was der Schaltplan `LCD1` nennt).

##### Option A — Direktmontage (`CONN_OLED2`, das tatsächlich auf der Platine bestückte Footprint)

<img src="images/OLED_DIRECT_MOUNT.jpg" width="220">

Ein nacktes Panel ohne separate Breakout-Platine — nur das Glas und sein 30-poliges FPC-Flachbandkabel, direkt in das `CONN_OLED2`-Footprint gelötet (`FPC30`, WiseChip UG-2864, der Name dieses Dokuments für das, was der Schaltplan `LCD1` nennt — siehe `BOM/BOM.TXT` und `URTC_NETLIST.TXT`). Von den 30 Pins ist nur eine Teilmenge tatsächlich verdrahtet — der Rest ist der parallele Schnittstellenbus des Panels (`D2`–`D7`, `RW`, `E/!RD`), unverbunden gelassen, da die Platine nur über I2C mit ihm spricht:

| CONN_OLED2 Pin(s) | Netz | Funktion |
|---|---|---|
| 1, 8, 29, 30 | GND / AGND | Masse |
| 9 | VDD | Logikversorgung (von `+3V3B`, der reinen OLED-Schiene — siehe BOM §1) |
| 28 | VCC | Panel-Versorgung |
| 2–5 | C2P/C2N/C1P/C1N | Ladungspumpen-Kondensatoren — `C26`/`C27` in der BOM |
| 26 | IREF | Referenzstrom-Einstellwiderstand |
| 27 | VCOMH | Interne Common-Voltage-Entkopplung |
| 10, 12 | BS0, BS2 | An GND gebunden |
| 11 | BS1 | An `+3V3B` gebunden |
| 18 | D0/SCK | I2C2 SCL — PA9 |
| 19 | D1/DIN/SDA | I2C2 SDA — PA10 |

`BS0`/`BS1`/`BS2` sind die eigene Schnittstellenauswahl-Brücke des Panels (hier GND/VCC/GND), in Hardware fest verdrahtet statt der MCU zugänglich gemacht — dies ist es, was den Controller überhaupt erst in den I2C-Modus versetzt, statt in den 8080-/6800-Parallelmodus, zu dem die anderen 22 FPC-Pins gehören.

##### Option B — Breakout-Modul (`CONN_OLED`, externe Alternative)

<img src="images/OLED_BREAKOUT_MODULE.jpg" width="220">

Dasselbe Panel, vormontiert auf einer kleinen Trägerplatine mit einem 4-poligen Header — nützlich, wenn Sie lieber ein handelsübliches Modul verdrahten als das nackte FPC-Panel zu beschaffen. Direkt an `CONN_OLED` angeschlossen, ohne dass eine Kreuzung nötig wäre — die eigene Pin-Reihenfolge des Moduls (`GND · VDD · SCK · SDA`) entspricht exakt der Pinbelegung von `CONN_OLED`, Pin für Pin:

| OLED-Modul-Pin | CONN_OLED-Pin | Signal |
|---|---|---|
| GND | 1 | Masse |
| VDD | 2 | +3,3V (Display-Logikversorgung) |
| SCK | 3 | SCL — PA9, Hardware-I2C2 |
| SDA | 4 | SDA — PA10, Hardware-I2C2 |

#### Boot-Splash

<img src="ani/splash_boot.gif" width="480">


#### Werkzeug-Icons (eines pro Profil, 4-Bilder-Animation)

<table>
<tr>
<td align="center"><img src="ani/00_soldering_iron.gif" width="80"><br>T12-Lötkolben</td>
<td align="center"><img src="ani/01_paste_dispenser.gif" width="80"><br>Pasten-Dispenser</td>
<td align="center"><img src="ani/02_liquid_dispenser.gif" width="80"><br>Flüssigkeits-Dispenser</td>
<td align="center"><img src="ani/03_screwdriver.gif" width="80"><br>Schraubendreher</td>
</tr>
<tr>
<td align="center"><img src="ani/04_vacuum_pickup.gif" width="80"><br>Vakuum-Aufnahme</td>
<td align="center"><img src="ani/05_drill.gif" width="80"><br>Bohrer (BL4260)</td>
<td align="center"><img src="ani/06_gripper_gimbal.gif" width="80"><br>Gimbal-Greifer</td>
<td align="center"><img src="ani/07_gripper_nema.gif" width="80"><br>NEMA-Greifer</td>
</tr>
<tr>
<td align="center"><img src="ani/08_aoi_inspection.gif" width="80"><br>AOI-Inspektion</td>
<td align="center"><img src="ani/09_laser_engraver.gif" width="80"><br>Laser-Gravierer</td>
<td align="center"><img src="ani/10_3d_printer.gif" width="80"><br>3D-Drucker-Hotend</td>
<td align="center"><img src="ani/11_scan_probe.gif" width="80"><br>3D-Scan-Sonde</td>
</tr>
<tr>
<td align="center"><img src="ani/12_smt_pickplace.gif" width="80"><br>SMT-Pick-&-Place</td>
<td align="center"><img src="ani/13_electromagnet.gif" width="80"><br>Elektromagnet</td>
<td align="center"><img src="ani/14_spot_welder.gif" width="80"><br>Punktschweißer</td>
<td align="center"><img src="ani/15_conformal_coating.gif" width="80"><br>Konformbeschichtung</td>
</tr>
<tr>
<td align="center"><img src="ani/16_vacuum_gripper_lg.gif" width="80"><br>Vakuumgreifer (LG)</td>
<td align="center"><img src="ani/17_flying_probe.gif" width="80"><br>Flying Probe</td>
<td align="center"><img src="ani/18_uv_curing.gif" width="80"><br>UV-Härtung</td>
<td align="center"><img src="ani/19_hotair_rework.gif" width="80"><br>Heißluft-Rework</td>
</tr>
<tr>
<td align="center"><img src="ani/20_pressfit_inserter.gif" width="80"><br>Press-Fit-Einsetzer</td>
<td align="center"><img src="ani/21_crimping_actuator.gif" width="80"><br>Crimp-Aktuator</td>
<td align="center"><img src="ani/22_thermal_inspection.gif" width="80"><br>Wärmebild-Inspektion</td>
<td align="center"><img src="ani/23_paste_jetting.gif" width="80"><br>Pasten-Jetting</td>
</tr>
<tr>
<td align="center"><img src="ani/24_ultrasonic_welder.gif" width="80"><br>Ultraschallschweißer</td>
</tr>
</table>


#### Warnung bei ungültiger Werkzeug-ID

Wenn die ID-Jumper zu keinem der 25 zugewiesenen Profile passen, blockiert die Platine jeden Aktuator und blinkt stattdessen dies:

<img src="ani/error_warning.gif" width="480">

Alle Animations-Quell-GIFs befinden sich in [`/ani`](ani/).

### Digitale Status-LED

Getrennt vom OLED und dem 8-Pixel-Beleuchtungsring trägt `CONN_LED1` eine einzelne adressierbare RGB-LED (WS2812B-Familie, SPI/DMA-gesteuert), dediziert für den Status auf einen Blick.

**Standardmäßig automatisch, auf Wunsch vom Host überschreibbar.** Die Firmware färbt diese LED selbstständig, nach dreistufiger Priorität:

* 🔴 **Rot** — ein Hardware-Fehler ist aktiv (`system_error_flag`). Gewinnt immer, unabhängig von allem anderen, das gerade passiert.
* 🔵 **Blau** — die Platine funktioniert aktiv: ein CAN-Frame (beliebige ID) ist in den letzten 1,5 Sekunden angekommen.
* 🟢 **Grün** — im Leerlauf, wartet auf Befehle: kein CAN-Verkehr seit über 1,5 Sekunden.

Der Master kann dies jederzeit noch überschreiben, indem er die CAN-ID `0x100` (DLC 8) mit der Rot-, Grün- und Blau-Intensität als ersten drei Bytes sendet (0-255 je, volle 24-Bit-Farbe, nicht nur die drei automatischen). Eine vom Host gesendete Farbe hält 10 Sekunden, bevor auf das automatische Schema zurückgefallen wird — lang genug, um tatsächlich gesehen zu werden, kurz genug, dass die Platine nicht bei einer veralteten benutzerdefinierten Farbe stecken bleibt, wenn der Host sie nicht mehr aktualisiert. Erneutes Senden von `0x100` (ob dieselbe Farbe oder eine neue) erneuert dieses 10-Sekunden-Fenster, sodass ein Host, der die benutzerdefinierte Kontrolle behalten möchte, sie einfach periodisch weiter senden muss. Ein Hardware-Fehler unterbricht immer eine aktive Überschreibung — Rot hat Priorität vor jeder Farbe, die der Host gesetzt hatte.

Siehe `docs/CANBUS.TXT` (ID `0x100`) für das exakte Byte-Layout, das sich diese Nachricht auch mit der Ring-LED und der OLED-Nachtmodus-Steuerung teilt.

## 🔒 Sicherheit und Schutz

- **Schutz vor thermischem Durchgehen** (`thermal_guard.c`, seit Anwendung 0.3.2) für Lötkolben / Heißluft und 3D-Hotend: ein MAXTEMP auf die *gemessene* Temperatur (449 °C T12, 310 °C Hotend), eine Aufheizüberwachung (die Temperatur muss bei eingeschalteter Heizung wirklich steigen - erkennt einen losen Thermistor oder eine lose Spitze) und eine Halteprüfung (nach Erreichen des Sollwerts kein längeres Absinken bei laufender Heizung). Ein Fehler schaltet die Heizung ab, verriegelt den Platinenfehler und meldet die Ursache in Byte 4 von `0x111`. Auf dem Host mit simulierter Heizung getestet (`tests/test_thermal_guard.c`). Die Grenzwerte wurden nie an einem echten Heizelement gemessen, und nichts davon lief je auf einer Platine.
- **Host-Tests der Firmware-Logik rund um die Hardware** (`tools/run_host_tests.py`, 38 Suiten): Neben Bootloader, Thermal-Guard und Dispatcher werden die echten CAN-Handler der 13 Werkzeuge (`firmware_can_*.c`), die Heizungsregelkreise (`firmware_control_thermal.c`: ADC-Abbildung, unmögliche Messwerte, Sollwert-Obergrenzen, Hysterese, hängende Heizung, die Guard-Fehler), der Fehlerrekorder (`firmware_blackbox.c`, Byte für Byte an die Szenarien gebunden, die URTC-COMMON aus seinem eigenen Modell schreibt, samt CAN-Auslesung), die Werkzeugerkennung über die Jumper, die Befehle nach dem Fehler-Gate und die Wärmebildkamera-Befehle gegen eine Fake-HAL (`tests/host_hal/`) kompiliert und unter Address- und UndefinedBehavior-Sanitizern sowie `gcc -fanalyzer` ausgeführt; `--coverage-floor` (`URTC_COVERAGE_FLOOR` im CI) bindet sie an einen Anteil ausgeführter Zeilen (95 Prozent der Zeilen aller 78 Firmware-Quellen, darunter jetzt der Start und die Hauptschleife der Anwendung, der OLED-Treiber und seine Bildschirme, die Status-LED, die Interrupt-Handler, die Busse des Erweiterungssteckers, die drei Wärmebildkameras, der Erweiterungs-Slave und die Hauptprogramme beider Bootloader; die Schwelle im CI liegt bei 90). Nichts davon lief auf einer Platine.
- **Diagnose:** Byte 5 von `0x111` meldet die Ursache des letzten Resets (bit0 = der Hardware-Watchdog hat ausgelöst); `0x1AA`/`0x1AB` liefern im F-RAM gespeicherte Lebensdauerzähler - thermische Fehler, Code des letzten Fehlers und gesamte Heizungs-Einschaltzeit (seit 0.3.3); die Black-Box-Samples zeichnen den thermischen Fehlercode auf.
- **Firmware-Signatur:** der HMAC-Schlüssel der Bootloader wird beim Build aus einer privaten Datei eingefügt (`URTC_SIGNING_KEY_FILE`); ohne sie verwendet der Build den *öffentlichen Entwicklungsschlüssel* und weist darauf hin, und `URTC_RELEASE=1` lehnt ihn ab. Jedes Image in `firmware/` kommt mit `<bin>.sha256` und `<bin>.manifest.json` (inklusive der vorberechneten Signatur), daher enthalten URTC Flasher und URTC Web Studio keinen Schlüssel. Siehe [`docs/FIRMWARE_SIGNING.TXT`](docs/FIRMWARE_SIGNING.TXT). HMAC ist ein gemeinsamer symmetrischer Schlüssel: Die veröffentlichten Anwendungs-Images sind mit dem öffentlichen Entwicklungsschlüssel signiert, sodass jeder ein Image signieren kann, das eine mit diesem Schlüssel gebaute Platine akzeptiert; eine asymmetrische Signatur (Ed25519) ist geplant. Jede Datei eines offiziellen Releases trägt außerdem eine getrennte Ed25519-Release-Signatur (`<Datei>.sig`, öffentlicher Schlüssel in [`docs/RELEASE_SIGNING.md`](docs/RELEASE_SIGNING.md)); der Flasher und das Web Studio prüfen sie, wenn sie vorhanden ist.
- **CAN-Bus:** er authentifiziert den Absender nicht - jeder Knoten kann die Werkzeuge steuern. Installationen mit Laser, Heizung oder Schweißgerät **müssen** eine vom MCU unabhängige Hardware-Verriegelung und einen geschlossenen Bus haben: [`docs/CAN_THREAT_MODEL.TXT`](docs/CAN_THREAT_MODEL.TXT).
- **Mehrere Werkzeugköpfe an einem CAN-Bus (standardmäßig aus):** ein dauerhafter Schalter (CAN `0x1AC` setzt ihn, `0x1AD` liest ihn, der F-RAM-Eintrag steht in [`docs/EEPROM.TXT`](docs/EEPROM.TXT)) aktiviert Knotenadressen aus den fünf ID-Jumpern, die Erkennung und eine Auswahlregel: Nur der ausgewählte Kopf befolgt Befehle, ein nicht ausgewählter Kopf betätigt nie etwas. Bei ausgeschaltetem Schalter, dem Standard und auch dem, was ein leerer F-RAM ergibt, verhält sich die Platine genau wie ein einzelner Kopf, und Frames und Kennungen bleiben unverändert. In der Firmware umgesetzt und nur auf dem Rechner getestet; auf Platinen nicht erprobt. Entwurf und Frames stehen in URTC-COMMON (`docs/MULTINODE.md`) und im Anhang von [`docs/CANBUS.TXT`](docs/CANBUS.TXT).
- **Was der Bootloader beantwortet, und die Patch-Nummer des Slaves:** das Zurücklesen (`0x7FE`) und die Freigabe eines Downgrades (`0x7FD`) werden nur in einer Bootloader-Sitzung beantwortet, die die Anwendung durch Annahme von `0x7F0` gestartet hat (oder auf einer Platine ohne gültige Anwendung); nach dem Einschalten werden sie ignoriert, und eine Build-Option schaltet das ab. Der Erweiterungs-Slave sendet seine Patch-Nummer im zehnten Byte seines Versionsregisters, und der Master gibt sie in `0x222` weiter (ein Byte 0 bedeutet nicht gesendet, wie bei einem älteren Slave), gefolgt vom niederwertigen Byte der Minor-Nummer in einem dritten Byte, sodass ein Programm die ganze Version anzeigen kann. Im Quelltext und auf dem Host getestet; noch kein Firmware-Abbild enthält es.

## 🔌 Schnittstellen

Sobald der Bootloader vorhanden ist, benötigt die Aktualisierung der Anwendung überhaupt keinen physischen Zugriff mehr auf die Platine — schicken Sie einfach den neuen Firmware-Build über dieselbe Nabelschnur-CAN-Leitung, die bereits Befehle zum Werkzeugkopf trägt.

**Die Update-Sequenz:**

1. **Auslösen.** Der Master sendet `0x7F0` (DLC 4, Payload `B0 07 1D 5A`) an die *laufende Anwendung*. Sie schaltet sicher die Stromversorgung jedes inline geschalteten Aktuators ab — Motoren, Heizungen, Laser — und setzt den Chip zurück. Diese Magic-Payload-Anforderung bedeutet, dass ein korrupter oder fehlerhafter Frame nicht versehentlich einen Reset in den Update-Modus auslösen kann.
2. **Start.** Nach dem Reset lauscht der Bootloader. Der Master sendet `0x7F1` (DLC 8, Big-Endian Gesamt-Firmware-Größe + Big-Endian HardwareID). Ein für andere Hardware gebautes Image wird genau hier abgelehnt, bevor auch nur ein einziges Byte Flash angetastet wird. Der Bootloader löscht genau so viele Backup-Slot-Seiten, wie das neue Image benötigt, und antwortet mit einem Statusframe (`0x7F5`).
3. **Signatur.** Der Master sendet die erwartete HMAC-SHA256-Signatur als vier `0x7F7`-Frames (je 8 Bytes, in Reihenfolge) — berechnet über das Firmware-Image mit einem Schlüssel, der zwischen dem Bootloader und dem Tool, das den Build signiert, geteilt wird. Das ist ein gemeinsamer symmetrischer Schlüssel; die Prüfung belegt also nur, dass das Image von jemandem mit diesem Schlüssel stammt, nicht von wem; die veröffentlichten Images verwenden den öffentlichen Entwicklungsschlüssel (siehe `docs/FIRMWARE_SIGNING.TXT`).
4. **Daten.** Der Master streamt die `.bin`-Datei als eine Sequenz von `0x7F2`-Frames (jeweils bis zu 8 Bytes rohe Firmware-Daten), Rücken an Rücken gesendet — CAN garantiert, dass Frames auf einem einzelnen Bus in der Reihenfolge ankommen, in der sie gesendet wurden, sodass keine Frame-Sequenznummer benötigt wird. Der Bootloader puffert eingehende Bytes in einer 2KB-Seite im RAM und schreibt sie einmal voll in den *Backup*-Slot, liest dabei jedes Halbwort zurück und vergleicht es mit dem, was geschrieben werden sollte, bevor die Seite als fertig betrachtet wird, und sendet nach jedem verifizierten Schreibvorgang eine `0x7F3`-Bestätigung (mit dem Seitenindex). Eine vernünftige Master-Implementierung wartet auf das ACK jeder Seite, bevor sie die Daten der nächsten Seite sendet, um den Empfangspuffer des Bootloaders nicht zu überlaufen.
5. **Ende & Verifizierung.** Sobald jedes Byte gesendet wurde, sendet der Master `0x7F4` (DLC 8, Big-Endian CRC32 + Version Major/Minor). Der Bootloader prüft die Größe des Backup-Slots, berechnet dessen CRC32 und HMAC-SHA256 und vergleicht beide mit dem, was der Master deklariert hat. Nur wenn alles übereinstimmt, fährt er fort, das Backup Seite für Seite in den Hauptslot zu kopieren, mit derselben Rücklese-Verifizierung wie oben. Sobald dieser Kopiervorgang abgeschlossen und bestätigt ist, speichert er die neuen Metadaten und setzt sich in die aktualisierte Anwendung zurück. Bei jeder Abweichung — Größe, CRC32, HMAC oder HardwareID — wird der Hauptslot überhaupt nicht angetastet, und der Bootloader kehrt einfach zum Lauschen auf einen neuen Versuch zurück.

**Statusframes (`0x7F5`, DLC 1):** `0x01` lauschend, `0x02` löschend, `0x03` empfangend, `0x06` verifizierend, `0x07` kopiert Backup in Haupt, `0x04` verifiziert OK (kurz vor dem Sprung), `0x05` Verifizierung fehlgeschlagen, `0xFF` Fehler.

**Heartbeat (`0x7F6`, DLC 2, etwa alle 1s während des Lauschens oder Aktualisierens):** Statusbyte + Fortschrittsprozent (0-100, oder `0xFF`, wo ein Prozentsatz nicht zutrifft). Erlaubt dem Master, zu unterscheiden zwischen "Knoten ist am Leben, hat aber noch nicht mit dem Lauschen begonnen" und "Knoten reagiert überhaupt nicht" - nützlich für automatisierte Inbetriebnahme und um einen hängenden Bootloader zu erkennen, ohne auf ein Timeout zu warten.

**Fortschrittsanzeige auf dem Bildschirm.** Der Bootloader steuert das OLED während eines Updates direkt an — niemand muss raten, ob etwas passiert. Er zeigt "UPDATING" plus einen Live-Fortschrittsbalken und Prozentsatz, während Seiten geschrieben oder kopiert werden, "FLASH OK" für einen Moment, bevor er sich in die neue Firmware zurücksetzt, und "ERROR", falls ein Seitenschreibvorgang fehlschlägt, die Übertragung länger als 10 Sekunden stockt, oder die Verifizierung eine Abweichung meldet.

**⚠️ Testen Sie dies auf der Werkbank, bevor Sie es im Feld einsetzen.** Das obige Protokoll kompiliert und linkt sauber, und die Logik wurde sorgfältig durchdacht, aber ein Bootloader ist genau die Art von Firmware, bei der "kompiliert korrekt" weit entfernt ist von "vertrauenswürdig auf Hardware" — das echte Flash-Programmier-Timing, das CAN-Verhalten über eine mehrere-tausend-Frames-Übertragung, und die Übergabe vom Bootloader zur Anwendung müssen alle auf einer echten Platine verifiziert werden (idealerweise mit JTAG als Rückfalloption zur Hand), bevor man sich hierauf für ein unbeaufsichtigtes Update mit angeschlossenen echten Aktuatoren verlässt.

## 🚀 Schnellstart

Der Flash von URTC ist in zwei unabhängige Teile aufgeteilt, sodass die Platine über dasselbe CAN-Nabelschnurkabel neu geflasht werden kann, das sie bereits für alles andere verwendet — ohne dass nach der Ersteinrichtung jemals wieder physischer Zugriff auf den JTAG/SWD-Header nötig wäre.

### Ersteinrichtung — erfordert JTAG/SWD (einmalig)

Der Bootloader kann nur über physische Programmierung auf den Chip gelangen — es gibt keinen Weg, eine Platine per CAN zu flashen, die noch keinen Bootloader hat. Dies ist ein einmaliger Schritt:

1. Öffnen Sie das Projekt in **STM32CubeIDE** (gebaut und getestet gegen das STM32F303CC-Ziel), oder verwenden Sie **STM32CubeProgrammer** direkt mit den unten kompilierten Ausgaben.
2. Flashen Sie **beide** Images über SWD (ST-Link) via den integrierten `STM_JTAG`-Header — jede `.hex`-Datei hat ihre Zieladresse eingebettet, sodass die meisten Tools (einschließlich STM32CubeProgrammer) beide in derselben Sitzung laden können:
   * `URTC_MAIN_BOOTLOADER_v<version>.hex` → `0x08000000`
   * `URTC_MAIN_FIRMWARE_v<version>.hex` → `0x08008000`
3. Setzen Sie die Werkzeugidentität über die ID-Lötbrücken, bevor Sie einschalten — die Platine liest sie beim Boot einmal, wie immer. Fünf Jumper (ID0-ID4), die den vollen 32-Adress-Raum abdecken (31 direkte Werkzeugadressen, plus die reservierte `11111`-Adresse für freie Konfiguration - siehe den Abschnitt Werkzeugmatrix oben).
4. Schalten Sie ein. Der Bootloader lauscht ~600ms, sieht nichts, und springt direkt in die Anwendung — von hier an verhält sich alles genau so, wie im Rest dieses READMEs beschrieben. Das Fenster beginnt, nachdem der Bootloader die gespeicherte Anwendung geprüft hat, und ein Werkzeug, das aktualisieren will, muss darin seinen Start-Frame (0x7F1) senden; der Zuhörstatus (0x7F5 = 0x01) wird gesendet, wenn das Fenster ohne Sprung endet, nicht an seinem Beginn.

**Der JTAG-Header wird nie entfernt oder deaktiviert.** Er ist immer als Rückfalloption vorhanden — falls ein CAN-Update jemals schiefgeht, oder Sie es einfach bevorzugen, können Sie jederzeit eines der beiden Images über SWD neu flashen.

**Zwei integrierte Drucktaster, BOOT und RESET**, sind auch für die Wiederherstellung vorhanden — RESET ist ein gewöhnlicher Hardware-Reset (`NRST`), und BOOT zieht `BOOT0` hoch, was eine Chip-Ebenen-Entscheidung ist, die getroffen wird, *bevor* überhaupt irgendetwas in diesem Repository läuft: normalerweise (nicht gehalten) bootet der Chip aus dem Flash in den eigenen Bootloader dieses Projekts, wie oben beschrieben; beim Reset gehalten, bootet er stattdessen in STs eigenen werksseitigen System-Memory-Bootloader (USB-DFU/UART-Wiederherstellung, völlig getrennt von allem hier). Siehe `src/F303-master/README.md` Abschnitt 4a für die vollständigen technischen Details.

### PC-Tools

Zwei eigenständige, plattformübergreifende (Windows/Linux) GUI-Tools unterstützen diese Platine - **URTC Flasher** (CAN-OTA- und Full-Chip-SWD/JTAG-Updates, sowohl für diese Platine als auch, bei einer Advanced-Erweiterungsvariante, deren eigenen Erweiterungs-Slave-Chip) und **URTC Tester** (ein Live-CAN-Bus-Exerciser, der zeigt, welches Werkzeugprofil gerade gejumpert ist). Beide lebten früher in diesem Repository unter `tools/`; jedes ist nun sein eigenes unabhängiges Projekt, mit eigenem README, eigener Lizenz und eigenen Übersetzungen:

- [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER)
- [URTC Tester](https://github.com/JuanenRac/URTC-TESTER)

Eine webbasierte Alternative, die ähnlichen Boden abdeckt (Live-Überwachung, CAN-Analyse, OTA-Flashen, Wärmebild-Inspektion), ohne dass etwas lokal installiert werden muss, existiert ebenfalls: [URTC Web Studio](https://github.com/JuanenRac/URTC-WEB-STUDIO).

<!-- urtc-section:compatibility -->
### Voraussetzungen und Kompatibilität

Welche Versionen der URTC-Programme zusammenarbeiten und welche Protokollversion jede spricht, steht in der Kompatibilitätstabelle, die URTC-COMMON erzeugt: [docs/COMPATIBILITY.md](https://github.com/JuanenRac/URTC/blob/main/docs/COMPATIBILITY.md).
<!-- /urtc-section:compatibility -->

## 🧪 Tests

Die Firmware-Logik rund um die Hardware wird auf einem Computer getestet: `python tools/run_host_tests.py` baut und führt 38 C-Testsuiten (`tests/test_*.c`) mit dem Host-Compiler, einem Schein-HAL und den Sanitizern AddressSanitizer und UBSan mit `-fanalyzer` aus: die Bootloader, den Thermoschutz, den Dispatcher, die echten CAN-Handler der 13 Werkzeuge, die Heizungsregelkreise und die Blackbox, unter anderem. Die CI baut und linkt außerdem die vier Binärdateien und schlägt unter einer Abdeckungsschwelle der ausgeführten Zeilen der Firmware-Quellen fehl. Nichts davon lief auf einer Platine: Es gibt keine bestückte Platine, und die Host-Tests können nicht zeigen, dass Zeitverhalten, Treiber oder Leistungsstufen funktionieren.

## 🏗️ Build und Veröffentlichung

### Aus dem Quellcode kompilieren (optional — die vorkompilierten Binärdateien sind Assets jedes GitHub-Releases, keine Dateien im Repository)

Zwei Wege, von diesem Quellcode-Repository zu den 4 obigen Binärdateien zu gelangen:

- **Automatisiert:** `build_firmware.sh` (Linux) oder `build_firmware.bat` (Windows), im Repository-Wurzelverzeichnis. Beide installieren die ARM-GNU-Toolchain, falls sie fehlt, holen den fixierten ST-HAL/CMSIS-Commit, und kompilieren, linken und `objcopy`en alle 4 Binärdateien (Hauptplatinen-Anwendung + Bootloader, Erweiterungs-Slave-Anwendung + Bootloader) direkt nach `firmware/`, und regenerieren dann `firmware/firmware_manifest.json`. Ohne Argumente ausgeführt für einen vollständigen Build, `--clean` um zuerst den lokalen `build/`-Cache zu leeren, oder `master`/`slave`, um nur das eigene Paar eines Chips zu bauen. `build_firmware.sh` läuft end-to-end gegen den echten Quellcode-Baum dieses Projekts; `build_firmware.bat` spiegelt dieselbe Logik für Windows - falls die beiden jemals nicht übereinstimmen, vertrauen Sie der Logik des `.sh`-Skripts als Referenz.
- **Manuell:** jeder Befehl, den eines der beiden Skripte ausführt, plus die Begründung hinter jeder Toolchain-/HAL-Wahl, wird Schritt für Schritt in `docs/COMPILE_STM32F303.TXT` erläutert — nützlich auf einem anderen Betriebssystem, mit einer anderen HAL/CMSIS-Quelle, oder einfach um genau zu sehen, was die Skripte automatisieren.
- **Release-Assets:** Die Firmware-Images (`.bin`, `.hex`, `.elf` und ihre Prüfsummen- und Manifestdateien) werden nicht in git versioniert. `build_firmware.sh`/`.bat` schreiben sie nach `firmware/` (von git ignoriert; `build/firmware` im CI-Modus, `URTC_CI=1`), und `python tools/publish_release_assets.py` prüft den Satz und gibt den genauen `gh release create`-Befehl mit den anzuhängenden Dateien aus. Sie werden nur als Assets des GitHub-Releases der jeweiligen Version veröffentlicht. Wer was liest: [`docs/FIRMWARE_DISTRIBUTION.md`](docs/FIRMWARE_DISTRIBUTION.md).

Nach jeder Firmware-Quellcode-Änderung (oder bevor Sie einer Versionserhöhung vertrauen), führen Sie **`check_version_consistency.sh`** vom Repository-Wurzelverzeichnis aus: Es liest die Track-A/E-Versionskonstanten (Hauptplatinen-Firmware, Erweiterungs-Slave-Anwendung) als Quelle der Wahrheit und prüft jede Stelle, die `VERSION_CHECKLIST.txt` für diesen Versions-Tag dokumentiert, und meldet jede Abweichung — es meldet nur, es behebt selbst nichts. `VERSION_CHECKLIST.txt` ist die vollständige Referenz für alle 5 unabhängigen Versionsstränge dieses Projekts (Haupt-Firmware, Hardware/PCB, Haupt-Bootloader, Erweiterungs-Slave-Anwendung, Erweiterungs-Slave-Bootloader) und genau, was beim Erhöhen jedes einzelnen davon angefasst werden muss.

Firmware und Bootloader werden unabhängig voneinander versioniert und veröffentlicht - das Flashen eines neuen Bootloaders impliziert keine neue Anwendungsversion und umgekehrt, sodass jedes seine eigene Historie in seiner eigenen Datei erhält, statt einer einzigen kombinierten Versionsnummer, die implizieren würde, dass sie sich immer zusammen bewegen:

- Firmware (`src/F303-master/`): [`src/F303-master/CHANGELOG.md`](src/F303-master/CHANGELOG.md)
- Bootloader (`src/F303-master/boot/`): [`src/F303-master/boot/CHANGELOG.md`](src/F303-master/boot/CHANGELOG.md)
- Erweiterungs-Slave-Anwendung (`src/F303-slave/`, STM32F303CBT6): [`src/F303-slave/CHANGELOG.md`](src/F303-slave/CHANGELOG.md)
- Erweiterungs-Slave-Bootloader (`src/F303-slave/boot/`): [`src/F303-slave/boot/CHANGELOG.md`](src/F303-slave/boot/CHANGELOG.md)

**Versionen:** Die vier Komponenten (zwei Anwendungs-Firmwares und zwei Bootloader: `FIRMWARE_VERSION_MAJOR`/`MINOR`/`PATCH` und `BOOTLOADER_VERSION_MAJOR`/`MINOR`/`PATCH`) werden unabhängig versioniert. Die Version jeder Komponente wird nur durch den ausdrücklichen Schritt `python bump.py` erhöht (der `bump_version.py` im Wurzelverzeichnis verwendet); das Kompilieren ändert nie eine Version. Jeder Bootloader führt zudem eine eigene Kopie der `FIRMWARE_VERSION_*` der zugehörigen Anwendung, die derselbe Schritt automatisch synchron hält. Siehe [`CHANGELOG.md`](CHANGELOG.md) im Wurzelverzeichnis für die aktuelle Version aller 4 Komponenten auf einen Blick und [`VERSION_CHECKLIST.txt`](VERSION_CHECKLIST.txt) für die Mechanik je Linie.

## 📂 Struktur des Repositorys

<p align="center">
  <img src="images/repository-structure.svg" alt="Struktur des Repositorys URTC" width="100%">
</p>

<details>
<summary>Textversion des Baums</summary>

```text
URTC/
├── src/                                               die vier Firmware-Komponenten und was sie teilen
├── PCB/                                               Schaltplan und Platinenentwurf
├── BOM/                                               Stücklisten der Platine und der sechs Erweiterungsplatinen
├── 3D/                                                3D-Modelle
├── ani/                                               die Animationen des OLED
├── keys/                                              nur öffentliche Schlüssel, nie ein privater
├── docs/                                              CAN-Protokoll, Pinbelegungen, Signierung, Bedrohungsmodell, Datenblätter
├── tests/                                             38 C-Host-Testsuiten
├── tools/                                             Host-Testläufer, Release-Werkzeuge, gemeinsame Prüfungen
├── images/                                            Banner und Bilder dieses READMEs
├── .github/                                           CI-Workflow sowie Vorlagen für Issues und Pull Requests
├── build-test.sh build-test.bat                       Build- und Testskripte, die im Repository nichts ändern
├── test.sh test.bat                                   führen die Tests aus
├── urtc.project.json                                  Manifest: Version, Rolle, Reifegrad und Build-Befehl
├── CHANGELOG.md                                       was sich in jeder Version geändert hat
├── SECURITY.md SUPPORT.md LICENSE                     Sicherheitsrichtlinie, Support und Lizenz
├── CONTRIBUTING.md CODE_OF_CONDUCT.md                 wie man mitwirkt, und der Verhaltenskodex
├── .gitattributes .gitignore                          Git-Einstellungen
├── build_firmware.sh build_firmware.bat               baut die vier Binärdateien
├── bump.py bump_version.py bump_manifest_version.py   Skripte, die die Version erhöhen
├── check_version_consistency.sh                       prüft jede Stelle, die eine Version nennt
├── check_shared_drivers.py                            prüft die Kopien der geteilten Treiber
├── firmware_signing.py                                signiert die Images
├── generate_manifest.py                               schreibt das Manifest eines Images
├── VERSION_CHECKLIST.txt                              die Mechanik der vier Versionen
└── LICENSE.md ruff.toml                               Hardware-Lizenz und Lint-Einstellungen
```

</details>

Die kommentierte Liste jeder Quelldatei, eine Zeile je Datei, steht in [docs/SOURCE_MAP.md](docs/SOURCE_MAP.md).

## 🗺️ Roadmap und bekannte Grenzen

**Firmware (`src/F303-master/`):** funktionsvollständig für alle 25 Werkzeugprofile — thermische Ein/Aus-Regelung mit Hysterese, werkzeugspezifische Telemetrie, Kommunikations-Watchdogs, Stall-/Fehlererkennung, und die eigene Live-Diagnose des OLED, zusammen mit einem Abfragepaar für das aktive Werkzeug (`0x110`/`0x111`), einem generischen SPI-Passthrough (`0x180`/`0x181`) für den Erweiterungsstecker, einem integrierten F-RAM, das Sollwerte über einen Stromausfall hinweg erhält (`0x190`/`0x191`), dem `11111`-Jumper-Mechanismus für freie Werkzeugkonfiguration (`0x1A2`/`0x1A3`), Peripherietyp- + Geräteseriennummer-Meldung (`0x1A4`/`0x1A5`) zur Unterscheidung mehrerer ansonsten identischer Platinen auf einem gemeinsamen Bus, und einer CAN-zu-I2C-Bridge (`0x210`-`0x221`), die den Erweiterungs-Slave-Chip auf advanced-Erweiterungsplatinen erreicht. Unabhängig vom Bootloader versioniert (siehe das Änderungsprotokoll unten). Nichts davon lief je auf einer echten Platine, und der thermische Schutz ist nur auf dem Host mit einem simulierten Heizelement getestet.

**Bootloader (`src/F303-master/boot/`):** funktionsvollständiges Golden-Image-A/B-Update-System — OTA-Updates über CAN, geprüft mit CRC32 und einem HMAC-SHA256 mit gemeinsamem Schlüssel (keine asymmetrische Signatur), ein Backup-Slot, der garantiert, dass ein fehlgeschlagenes Update die Platine nie unbrauchbar macht, und eine eigene Versionsmeldung (`0x7FA`), unabhängig von der Anwendung. Kompiliert und linkt sauber; siehe den Werkbank-Test-Vorbehalt oben, bevor Sie sich unbeaufsichtigt mit angeschlossenen echten Aktuatoren darauf verlassen.

**PC-Tools:** sowohl [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER) (CAN-OTA-Updates + Full-Chip-SWD/JTAG-Programmierung) als auch [URTC Tester](https://github.com/JuanenRac/URTC-TESTER) (Live-Werkzeug-Steuerungs-/Telemetrie-Exerciser pro Werkzeug) sind funktionsvollständig für das, was sie erreichen sollen, jedes nun sein eigenes unabhängiges Projekt mit eigenem README, das die Einrichtung und jede Bedienung im Detail abdeckt.

**Hardware:** Schaltplan und Stückliste werden noch finalisiert; es existiert noch keine bestückte Platine, um irgendetwas Obiges gegen echte Silizium zu validieren. Alles Obige kompiliert, linkt, und wurde sorgfältig durchdacht, aber "kompiliert korrekt" und "auf Hardware verifiziert" sind zwei verschiedene Behauptungen — siehe den Sicherheitshinweis am Anfang dieses READMEs, und behandeln Sie eine erste Inbetriebnahme mit der Vorsicht, die jede neue Platine verdient.

Falls jemand in der Community an benutzerdefinierten Endeffektoren, intelligenten Werkzeugwechslern, oder erweiterter Werkzeugintegration für PAROL6, Faze4, oder eine andere Roboterarm-Plattform arbeitet, würde ich mich freuen, zu chatten, Ideen auszutauschen, oder tiefer in die CAN-Befehle einzutauchen!

## 🌍 Sprachen

README und Texte gibt es auf Englisch, Spanisch, Französisch, Italienisch, Deutsch, Chinesisch, Japanisch und europäischem Portugiesisch.

## 🔗 Verwandte Projekte

URTC besteht aus unabhängigen Repositories mit jeweils eigener Version, eigenen Tests und eigenem README. Das ist die Familie, nach Gruppen:

* **URTC Tool Platform:**
  * **URTC** (dieses Repository) - Firmware der Werkzeugkopf-Platine (25 Werkzeugprofile, CAN-Bootloader) und die Referenz des CAN-Protokolls.
  * [URTC-COMMON](https://github.com/JuanenRac/URTC-COMMON) - das CAN-Protokoll einmal als Daten geschrieben (Spezifikation, erzeugte Tabellen, Konformitätsvektoren) und der Code, den die Programme teilen.
  * [URTC-FLASHER](https://github.com/JuanenRac/URTC-FLASHER) - Desktop-Werkzeug, das die Platinen-Firmware über CAN oder über SWD/JTAG aktualisiert.
  * [URTC-GATEWAY](https://github.com/JuanenRac/URTC-GATEWAY) - Gateway vor dem CAN-Bus: lässt nur gelistete Kennungen durch, begrenzt ihre Rate und verlangt einen Sitzungsschlüssel.
  * [URTC-SMART-RACK](https://github.com/JuanenRac/URTC-SMART-RACK) - hardwareunabhängige C-Logik und Befehlsprotokoll für eine Werkzeugablage mit Vorheizen und Lebenszyklusverfolgung.
  * [URTC-TESTER](https://github.com/JuanenRac/URTC-TESTER) - Desktop-Live-Tester für den CAN-Bus, der die Bedienelemente und die Telemetrie des von einer Platine gemeldeten Werkzeugprofils zeigt.
  * [URTC-VISION-TOOL](https://github.com/JuanenRac/URTC-VISION-TOOL) - Sensorrahmen-Protokoll in C und ein Python-Thermo-/RGB-Begleiter für einen Inspektions-Werkzeugkopf.
  * [URTC-WEB-STUDIO](https://github.com/JuanenRac/URTC-WEB-STUDIO) - Browser-App, die die Platine über Web Serial und SLCAN flasht und testet, ohne Installation.
* **Ecosystem Operations:**
  * [URTC-DEVTOOLS](https://github.com/JuanenRac/URTC-DEVTOOLS) - die gemeinsamen Repository-Werkzeuge der Familie als versionierter Satz, mit einer Sperrdatei, die eine bearbeitete von einer veralteten Kopie unterscheidet.
  * [URTC-DOCS](https://github.com/JuanenRac/URTC-DOCS) - Architektur, Schnittstellen, Sicherheitsbasis und erzeugter Katalog des Ökosystems sowie die Werkzeuge, die alle Repositories vergleichen.
  * [URTC-UPDATER](https://github.com/JuanenRac/URTC-UPDATER) - Desktop-Werkzeug, das die Repositories der URTC-Familie erkennt, installiert und aktualisiert, jeweils eine ausdrückliche Aktion.

**Schwesterprojekte (unabhängig, mit derselben Basis und denselben Werkzeugen):**

* **[HYDRA-UMC-DOCS](https://github.com/JuanenRac/HYDRA-UMC-DOCS)** - Schwesterprojekt: das Dokumentationszentrum von HYDRA-UMC, einem unabhängigen Ökosystem, das Basis und Werkzeuge mit diesem teilt
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Schwesterprojekt: das Dokumentationszentrum von A.R.M.O.R., dem Sicherheits-Ökosystem, ebenfalls unabhängig und auf derselben Basis
* **[JuanenRac](https://github.com/JuanenRac/JuanenRac)** - das Profil und das Status-Dashboard aller Ökosysteme

## 📚 Dokumentation

- **[CONTRIBUTING.md](CONTRIBUTING.md)** — Technologie-Stack und Coding-Richtlinien für einen Pull Request.
- **[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)** — die in dieser Community erwarteten Verhaltensstandards.
- **[SECURITY.md](SECURITY.md)** — wie man eine Schwachstelle meldet, und die echten Sicherheitsschwerpunkte dieses Projekts.
- **[SUPPORT.md](SUPPORT.md)** — wo man Fragen stellt und Fehler meldet.
- **[LICENSE.md](LICENSE.md)** — die eigene Lizenz dieses Projekts.

## 🤝 Mitwirken und Community

* Führe vor einem Pull Request `URTC_CI=1 python tools/ci_validate.py` aus: es muss bestehen. Siehe [CONTRIBUTING.md](CONTRIBUTING.md) und den [Verhaltenskodex](CODE_OF_CONDUCT.md).
* Das README gibt es in acht Sprachen: Eine Textänderung wird in allen acht Dateien vorgenommen, und die mit anderen Repositorys geteilten Abschnitte schreibt `tools/readme_sections.py` von URTC-COMMON.
* Beschreibe nichts als auf einer Platine funktionierend, was nur gebaut wurde.
* Melde eine Schwachstelle privat: [SECURITY.md](SECURITY.md). Fragen, Ideen und Meldungen: electrohobby3d@gmail.com ([SUPPORT.md](SUPPORT.md)).

## 👤 AUTOR

**JuanenRac** (Electro Hobby 3D)
📧 electrohobby3d@gmail.com
📺 [youtube.com/@electrohobby3d](https://youtube.com/@electrohobby3d)

## 📜 LIZENZ

URTC ist (c) 2026 JuanenRac (Electro Hobby 3D). Dieser Hinweis muss in jeder Verbreitung dieses Projekts oder abgeleiteter Werke enthalten sein.

Da dieses Projekt aus mehreren verschiedenen Arten von Inhalten besteht, werden einzelne Teile unter verschiedenen Lizenzen zur Verfügung gestellt - jede passend zu dem, was sie tatsächlich abdeckt, statt eine einzige Lizenz für alles zu erzwingen:

1. Die **Firmware** unter `./firmware` (sowohl Anwendung als auch CAN-Bootloader) ist verfügbar unter der **GNU General Public License v3.0 (GPL-3.0-or-later)**. Vollständiger Text unter https://www.gnu.org/licenses/gpl-3.0.html.

2. Die **Hardware-Designs** (Eagle-Schaltplan-/Board-Dateien, Gerber, und die 3D-druckbaren Teile unter `./PCB` und `./3D`) sind verfügbar unter der **CERN Open Hardware Licence v2 - Strongly Reciprocal (CERN-OHL-S v2)**. Vollständiger Text unter https://cern-ohl.web.cern.ch/.

3. Die **Dokumentation** (dieses README und dessen eigene Übersetzungen - `README_spa.md`, `README_ita.md`, `README_fra.md`, `README_deu.md`, `README_zho.md`, `README_jpn.md` - plus die Referenzdateien unter `./docs`) ist verfügbar unter **Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)**. Vollständiger Text unter https://creativecommons.org/licenses/by-sa/4.0/.

Wenn Sie auf diesem Projekt aufbauen, denken Sie an die Lizenzaufteilung: Code-Änderungen an der Firmware sollten GPL-3.0-or-later bleiben, Hardware-Modifikationen sollten CERN-OHL-S bleiben, und Dokumentationsableitungen sollten CC BY-SA bleiben - jede mit Namensnennung zurück zu diesem Projekt.

Dieses Repository deckt nur die eigene Firmware und Hardware der URTC-Platine ab - die PC-Tools (URTC Flasher, URTC Tester), die früher hier lebten, sind nun unabhängige Projekte mit eigener Lizenzierung, siehe "PC-Tools" oben.
