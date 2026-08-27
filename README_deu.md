<p align="center">
  <img src="images/URTC_LOGO.svg" alt="URTC Logo" width="100%">
</p>

# 🚀 URTC — Universal Robot Tool Controller (v0.2)

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  🇩🇪 <b>Deutsch</b> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>


<p align="left">
  <img src="https://img.shields.io/badge/Lizenz-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Hardware-CERN%20OHL--S-orange.svg" alt="CERN OHL-S">
  <img src="https://img.shields.io/badge/Sprache-C-00599C.svg" alt="C">
  <img src="https://img.shields.io/badge/Plattform-STM32F303-003551.svg" alt="STM32">
  <img src="https://img.shields.io/badge/Bus-CAN-yellow.svg" alt="CAN">
</p>


> **⚠️ Sicherheitshinweis:** Diese Platine steuert eine **10W-Gravier-Laserdiode** und mehrere Heizstufen (T12-Lötkolben-Patrone, 3D-Drucker-Hotend). Der Bau und die Nutzung bedeuten den Umgang mit Ausrüstung, die bei Montage oder Betrieb ohne angemessene Sicherheitsmaßnahmen (Laserschutzbrille passend zur Wellenlänge der Diode, thermischer Schutz, eine zugängliche Stromabschaltung) **Verbrennungen, Brand oder Augenschäden** verursachen kann. Dies ist ein Hobbyisten-/Maker-Projekt, das im Ist-Zustand geteilt wird — Bau und Nutzung auf eigenes Risiko, und grundlegende Sicherheitspraxis sollte nicht übersprungen werden, nur weil die Firmware Watchdogs hat.

Hallo zusammen! Ich wollte ein Projekt teilen, das ich entwickle: URTC (Universal Robot Tool Controller). Es handelt sich um eine monolithische, hochintegrierte Steuerplatine, die speziell dafür entwickelt wurde, die Fähigkeiten von Roboterarmen und Automatisierungs-Setups zu erweitern, wodurch sie perfekt zu Plattformen wie PAROL6 und Faze4 passt — zwei Open-Source-Roboterarme, entworfen und entwickelt von [Source-Robotics](https://source-robotics.com/) ([GitHub](https://github.com/Source-Robotics)).

**URTC ist ein unabhängiges, inoffizielles Projekt.** Es wird nicht von Source-Robotics entwickelt oder unterstützt — es ist ein kompatibler Werkzeugkopf-Controller, der gut mit PAROL6 und Faze4 zusammenarbeitet, und dieselbe CAN-basierte Architektur lässt sich auch für andere Roboterarm-Plattformen anpassen.

Hier ist die vollständige Aufschlüsselung dessen, was es ist, was es tut, und des Hardware-Ökosystems, das es derzeit verwaltet.

**Status: 🚧 Aktiv in Entwicklung befindliches Projekt — noch kein Release.** URTC befindet sich in kontinuierlicher, aktiver Entwicklung auf beiden Fronten gleichzeitig: Firmware (neue Werkzeugprofile, das Erweiterungs-Slave-Ökosystem, Protokolländerungen) und Hardware (Schaltplan und Stückliste werden noch finalisiert, es existiert noch keine bestückte Platine). Da sich beide Seiten zusammen weiterbewegen, ist das, was sich zu einem gegebenen Zeitpunkt in diesem Repository befindet, eine Momentaufnahme laufender Arbeit, kein stabiles, versioniertes Produkt — Dateinamen, Ordnerstruktur, Werkzeuganzahl und Dokumentation können sich alle noch ändern, während sich das Design festigt. Sobald sowohl Firmware als auch Hardware einen wirklich stabilen, auf echter Hardware verifizierten Zustand erreichen, wird ein richtiges **Release** getaggt, das alles zusammen bündelt (Firmware, Bootloader, PC-Tools, Hardware-Design-Dateien und Dokumentation) als kohärente, eingefrorene Momentaufnahme. Bis dahin behandeln Sie `main` als das aktiv bewegliche Ziel, das es ist.

---

## ⚙️ Was ist URTC?

URTC ist eine All-in-One-, kompakte Steuerplatine, angetrieben von einem STM32-Mikrocontroller (STM32F303CCT6, LQFP48). Sie kommuniziert mit dem Haupt-Robotercontroller über CAN-Bus, was die Echtzeit-Ausführung komplexer Aufgaben mit geringer Latenz direkt am Werkzeugkopf oder an der Achse ermöglicht. Sie verfügt über ein integriertes OLED-Display für sofortige Diagnose — animierter Boot-Splash, animierte Icons pro Werkzeug, Live-Telemetrie auf einem zweifarbigen Panel —, eine Einzelpixel-RGB-Status-LED plus einen adressierbaren RGB-LED-Ring für die Kamerabeleuchtung, einen 20-poligen Erweiterungsstecker für Zusatzplatinen, ein integriertes F-RAM, das die Sollwerte des aktiven Werkzeugs über einen Stromausfall hinweg erhält, sowie dedizierte Analog- und Hochstrom-Leistungsstufen.

## 🛠️ Skalierbare Architektur & Werkzeugmatrix

Die Kernstärke von URTC ist seine extreme Vielseitigkeit. Statt für jeden anderen Job die Elektronik auszutauschen, verfügt die Platine über eine skalierbare Matrix-Architektur:

* **32-Adressen-Identifikationsschema:** Hardware und Kommunikationsprotokoll sind so konzipiert, dass sie bis zu 32 verschiedene Werkzeuge oder Endeffektoren direkt am Roboterkopf identifizieren können, über eine 5-Bit-Lötbrücken-ID-Matrix (ID0-ID4). Von diesen 32 Ablesungen entsprechen 31 direkt einem Werkzeugprofil; die 32. (alle 5 Jumper gesetzt, `11111`) ist stattdessen als "freie Konfiguration"-Adresse reserviert - siehe unten.
* **25 Plug-and-Play-Automatisierungsprofile:** Die Firmware verwaltet nativ 25 Werkzeugprofile - die Platine liest die physische Identität des Werkzeugkopfs und konfiguriert die Leistungsstufen, Sensoren und Logikumschaltung nahtlos, ohne dass ein vollständiges Neu-Flashen erforderlich ist. 6 weitere Adressen bleiben innerhalb des bestehenden Schemas für zukünftige Werkzeugprofile frei.
* **Freie Werkzeugkonfiguration:** Die reservierte `11111`-Jumper-Ablesung wählt kein festes Werkzeug - sie weist die Platine an, stattdessen in einem Register in ihrem eigenen persistenten F-RAM nachzuschlagen, welches Werkzeug zu verwenden ist, im Voraus über CAN gesetzt (via `URTC Flasher`). Nützlich für eine Platine, die auf ein anderes Werkzeug umprogrammiert werden muss, ohne die Jumper physisch umzulöten. Siehe `docs/EEPROM.TXT` Abschnitt 5 für den vollständigen Mechanismus.

## 🔌 Hardware-Flexibilität & Motorunterstützung

Um eine so große Vielfalt an Anwendungen zu bewältigen, ist die URTC-Hardware voll ausgestattet, um zu steuern:

* **NEMA-Schrittmotoren:** NEMA 8, 11, 14 und 17 laufen direkt über den integrierten TMC2209, ebenso wie NEMA 23 und 34 — bis zu **2,0A** bei jedem davon über die Treiberstufe der Hauptplatine. Für NEMA 23/34 bei ihrem vollen Nenndrehmoment unterstützt ein TMC5160 am Erweiterungsstecker (siehe unten) bis zu **10A**, stromskaliert durch die für diese Platine gewählten externen MOSFETs/den Messwiderstand — die integrierte 2,0A-Grenze gilt nicht mehr, sobald ein Motor zum Erweiterungstreiber gewechselt ist.
* **3-Phasen-BLDC-/Gimbal-Motoren** für hochpräzise Bewegung.
* **Motoren mit Hall-Sensoren und Tachometern** für Regelung mit geschlossenem Regelkreis.
* **Dedizierte Eingänge** für reflektive optische Näherungssensoren wie den TCRT5000, plus einen generischen Active-Low-Endstop-/Endschalter-Eingang, der von vier Werkzeugprofilen gemeinsam genutzt wird.

## 🧩 Erweiterungsstecker

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

### Die 6 Erweiterungsplatinen-Varianten

4 der 6 Erweiterungsplatinen-Varianten tragen einen Schrittmotor-Treiber — entweder einen TMC2209 (bis zu 2A/Spule, integrierte Leistungs-MOSFETs) oder einen TMC5160A (bis zu 10A+/Spule, benötigt 8 externe Leistungs-MOSFETs, die der Treiber selbst nicht enthält). Unabhängig von dieser Treiberwahl ist eine treibertragende Platine entweder **basic** (nur Treiber + Stecker, kein MCU — STEP/DIR/EN direkt von der Hauptplatine geroutet) oder **advanced** (fügt einen zweiten Mikrocontroller hinzu, STM32F303CBT6, plus 2 lokale Sensorchips — einen ADS1115 16-Bit-ADC und eine Wärmebildkamera der MLX9064x-Familie — sowie lokale PWM-Erzeugung für Werkzeuge, deren Timing direkt am Werkzeugkopf erzeugt werden muss statt über ein Kabel geroutet zu werden). 2×2-Kombinationen, plus 2 weitere reine Sensor-Basic-Platinen (ADS1115 oder MLX9064x, direkt mit dem eigenen STM32F303CC der Hauptplatine verdrahtet, kein Treiber und kein Slave-MCU) für ein Werkzeug, das nur einen dieser 2 Chips benötigt und nichts, was eine advanced-Platine sonst noch trägt — insgesamt 6 Platinen — siehe `BOM/BOM_EXPANSION_*.TXT` (6 Dateien), `docs/EXPANSION.TXT` und `docs/PINOUT_SLAVE.txt`.

Der eigene STM32F303CBT6 der advanced-Variante kommuniziert mit der Hauptplatine über den bestehenden bit-gebankten I2C-Bus des Erweiterungssteckers oben — Hauptplatine als Master, Slave-Chip antwortend als echter Hardware-I2C-Slave — und betreibt seinen eigenen zweiten, rein lokalen I2C-Bus für die 2 Sensorchips. Er hat seinen eigenen Bootloader und seine eigene Anwendungs-Firmware, aktualisiert auf dieselbe Weise wie die Hauptplatine (CAN-OTA von `URTC Flasher`), nur über diese I2C-Verbindung weitergeleitet, statt den Slave-Chip direkt zu erreichen. Siehe `src/F303-slave/README.md` und `src/F303-slave/boot/README.md` für die vollständigen technischen Details.

## 💾 Parameter-Persistenz

Ein integriertes FM24CL64B F-RAM (64Kbit, I2C) hält eine periodisch aktualisierte Momentaufnahme der Sollwerte des aktiven Werkzeugs und der globalen LED-/OLED-Einstellungen, sodass ein plötzlicher Stromausfall nicht "was hat diese Platine gerade getan" ebenso unbekannt zurücklässt, wie der Verlust selbst ungeplant war. Es teilt sich den Hardware-I2C2-Bus des OLED, statt einen eigenen zu bekommen — diese MCU hat nur eine nutzbare Hardware-I2C-Peripherie für diesen Zweck, bereits vom OLED beansprucht (siehe `src/F303-master/README.md` Abschnitt 6 für die vollständige Begründung).

**Wiederhergestellter Zustand ist abfragbar, wird aber nie automatisch auf etwas Gefährliches angewendet.** Beim Booten wird alles, was gespeichert wurde, über CAN lesbar (`0x190`/`0x191`) — aber ein Heizungs-Sollwert, eine Laserleistung oder ein Motorbefehl wird nie stillschweigend von selbst wieder scharfgeschaltet. Nur die sicheren, passiven Einstellungen (LED-Farben, OLED-Modus) werden direkt wiederhergestellt. Einen Sollwert nach tatsächlicher Überprüfung des Geschehens absichtlich erneut zu senden, bleibt die Entscheidung des Master-Controllers, nicht etwas, das diese Platine im Moment der Stromwiederkehr selbst entscheidet.

## 💼 Nativ automatisierter Werkzeugkatalog (25 Firmware-Profile)

Durch seine dynamische Umschaltlogik verwaltet die Firmware nativ die folgenden Werkzeugköpfe:

1. **Lötstation (T12):** präzise PID-Temperaturregelung mittels direkter ADC-Rückmeldung zur Handhabung von Standard-T12-Lötspitzen, plus ein motorisierter Drahtvorschub, der Lötdraht in die Verbindung spult (teilt sich `CONN_MOT` und dessen Schrittmotor-Protokoll mit den reinen Bewegungswerkzeugen unten - tauscht den eigenen generischen Endstop-Eingang dieses Werkzeugs ein, um dafür Platz zu schaffen). [Jumper-/Verdrahtungskonfiguration →](images/TOOL_SOLDERING_IRON.png)
2. **SMT-Lötpasten-Dispenser:** millimetergenaue Vorschubregelung für präzise Lötpasten-Auftragung auf Leiterplatten. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_PASTE_DISPENSER.png)
3. **Thermalpaste-/Flüssigkeits-Dispenser:** Fließfähigkeitsmanagement für hochviskose Pasten oder flüssige Klebstoffe. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_LIQUID_DISPENSER.png)
4. **Intelligenter Elektro-Schraubendreher:** Rotations- und Stoppsteuerung basierend auf Drehmomentgrenzen oder Endanschlägen. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_SCREWDRIVER.png)
5. **Vakuum-/Pneumatik-Greifer:** Vakuumpumpensteuerung und Druckniveau-Ablesung für sichere Pick-and-Place-Operationen. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_VACUUM_PICKUP.png)
6. **Bohrer (BL4260):** PWM-Drehzahlregelung, Richtungsumschaltung und dynamisches elektrisches Bremsen mit Echtzeit-Drehzahlablesungen, auf einer eigenen dedizierten Freigabe-/Bremsleitung, unabhängig von der Freigabe des Schrittmotor-Werkzeugtreibers. Generischer Endstop-Eingang verfügbar. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_DRILL.png)
7. **Gimbal-Greifer:** hochempfindliche Handhabung mittels 3-Phasen-Brushless-Gimbal-Motoren. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_GRIPPER_GIMBAL.png)
8. **NEMA-Greifer:** robuste Klemmkraft, gesteuert über einen Hochleistungs-Schrittmotor. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_GRIPPER_NEMA.png)
9. **AOI-System (Automated Optical Inspection):** synchrone stroboskopische Steuerung des LED-Beleuchtungsarrays für die Aufnahme mit Machine-Vision-Kameras. Generischer Endstop-Eingang verfügbar. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_AOI_INSPECTION.png)
10. **Gravier-Laserdiode (10W optisch):** PWM-Strahlleistungsmodulation mit einer Sicherheits-Hardware-Schleife (CAN-Watchdog), die sperrt, wenn die Host-Kommunikation verloren geht. Generischer Endstop-Eingang verfügbar. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_LASER_ENGRAVER.png)
11. **3D-Druck-Hotend:** PID-Regelung der Heizpatrone, NTC-Thermistor-Ablesung, Extrudersteuerung, und ein dedizierter, 25kHz-PWM-gesteuerter Schichtkühllüfter (4-Draht, Tachometer-Rückmeldung, eigener Kommunikations-Watchdog) — alles integriert in einem einzigen Block. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_3D_PRINTER.png)
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
22. **Kabelbaum-/Crimp-Aktuator:** Hochdrehmoment-Backe zum Abisolieren/Crimpen von Anschlüssen, angetrieben über den **eigenen Treiber einer Erweiterungsplatine** statt den der Hauptplatine. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_CRIMPING_ACTUATOR.png)
23. **Erweiterte Leiterplatteninspektion:** Wärmebildgebung (Array der MLX9064x-Familie - alle 3 Familienmitglieder, MLX90640/MLX90641/MLX90642, heute unterstützt, entweder über den eigenen Slave-Chip einer **advanced**-Erweiterungsplatine oder eine **basic** MLX9064x-Erweiterungsplatine, direkt mit der Hauptplatine verdrahtet), um Kurzschlüsse anhand der Temperatursignatur zu erkennen, zusammen mit Ring-LED-Beleuchtung. Deckt auch Mikrospindel-Depaneling ab - derselbe Bohrer-Hardware-Pfad oben, ein anderes Bit für einen anderen Job. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_THERMAL_INSPECTION.png)
24. **Lötpasten-Jetting-Ventil:** piezoelektrisches Mikrotröpfchen-Dispensieren, sub-millisekundengenaue Impulspräzision, lokal auf einer **advanced**-Erweiterungsplatine erzeugt. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_PASTE_JETTING.png)
25. **Ultraschallschweißer / Verpackungsversiegler:** Hochfrequenz-Wandlerauslösung zum Schweißen von Kunststoffgehäusen. [Jumper-/Verdrahtungskonfiguration →](images/TOOL_ULTRASONIC_WELDER.png)

*(Werkzeugkonfigurationsbilder existieren für die Werkzeuge 1-12; Bilder für die Werkzeuge 13-25 werden ergänzt, sobald die Hardware-Dokumentation aufholt — die obigen Dateinamen entsprechen der bereits in `images/` verwendeten Namenskonvention.)*

## 🖥️ Lokale OLED-Schnittstelle

Jeder Werkzeugkopf zeigt Live-, werkzeugspezifische Telemetrie auf einem 128×64-zweifarbigen OLED: einen animierten Boot-Splash beim Einschalten, einen blinkenden CAN-Aktivitätsindikator, eine Live-"Hero"-Ablesung im oberen Streifen (Temperatur, Drehzahl, Leistung — was auch immer für das aktive Werkzeug am wichtigsten ist), und ein kleines vier-Bilder-animiertes Icon pro Werkzeugprofil.

### Das Modul

Beide physischen Varianten unten sind elektrisch dasselbe Panel (SSD1306- oder SSD1315-gesteuert — die Init-Sequenz der Firmware ist verifiziert kompatibel mit beiden, siehe `OLED_Init()` in `firmware_oled_driver.c`; der SSD1315 ist ein neuerer, direkt austauschbarer Ersatz-Controller, mit dem viele Module heute unter derselben "SSD1306"-Auflistung/Bedruckung ausgeliefert werden), **128×64**, und dieselbe zweifarbige "Gelb/Blau"-Aufteilung, bei der das physische LED-Material selbst in zwei feste Farbzonen unterteilt ist (dies ist nicht softwareauswählbar):

* **Obere 16 Pixel (Seiten 0-1): gelb.** URTC nutzt diesen Streifen für das, was am nützlichsten ist, um es auf einen Blick zu sehen, ohne genau zu lesen — den CAN-Aktivitätsindikator, Live-Hero-Ablesungen oder (auf dem Boot-Splash-/Ungültiges-Werkzeug-Bildschirm) kurzen Statustext.
* **Untere 48 Pixel (Seiten 2-7): blau.** Alles andere — Werkzeug-Icons, detaillierte Telemetrie, das animierte JuanenBOT-Gesicht auf dem Splash-Bildschirm, der große blinkende ERROR-Schriftzug.

Beide landen auf demselben I2C2-Bus und demselben `OLED_Init()` — die Firmware kann nicht erkennen, welches der beiden angeschlossen ist, und muss es auch nicht. Sie schließen sich auf einer gegebenen Platine gegenseitig aus (siehe `BOM/BOM.TXT`s Hinweis zu `CONN_OLED2` - der Name dieses Dokuments für das, was der Schaltplan `LCD1` nennt).

#### Option A — Direktmontage (`CONN_OLED2`, das tatsächlich auf der Platine bestückte Footprint)

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

#### Option B — Breakout-Modul (`CONN_OLED`, externe Alternative)

<img src="images/OLED_BREAKOUT_MODULE.jpg" width="220">

Dasselbe Panel, vormontiert auf einer kleinen Trägerplatine mit einem 4-poligen Header — nützlich, wenn Sie lieber ein handelsübliches Modul verdrahten als das nackte FPC-Panel zu beschaffen. Direkt an `CONN_OLED` angeschlossen, ohne dass eine Kreuzung nötig wäre — die eigene Pin-Reihenfolge des Moduls (`GND · VDD · SCK · SDA`) entspricht exakt der Pinbelegung von `CONN_OLED`, Pin für Pin:

| OLED-Modul-Pin | CONN_OLED-Pin | Signal |
|---|---|---|
| GND | 1 | Masse |
| VDD | 2 | +3,3V (Display-Logikversorgung) |
| SCK | 3 | SCL — PA9, Hardware-I2C2 |
| SDA | 4 | SDA — PA10, Hardware-I2C2 |

### Boot-Splash

<img src="ani/splash_boot.gif" width="480">


### Werkzeug-Icons (eines pro Profil, 4-Bilder-Animation)

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


### Warnung bei ungültiger Werkzeug-ID

Wenn die ID-Jumper zu keinem der 25 zugewiesenen Profile passen, blockiert die Platine jeden Aktuator und blinkt stattdessen dies:

<img src="ani/error_warning.gif" width="480">

Alle Animations-Quell-GIFs befinden sich in [`/ani`](ani/).

## 🔴🟢🔵 Digitale Status-LED

Getrennt vom OLED und dem 8-Pixel-Beleuchtungsring trägt `CONN_LED1` eine einzelne adressierbare RGB-LED (WS2812B-Familie, SPI/DMA-gesteuert), dediziert für den Status auf einen Blick.

**Standardmäßig automatisch, auf Wunsch vom Host überschreibbar.** Die Firmware färbt diese LED selbstständig, nach dreistufiger Priorität:

* 🔴 **Rot** — ein Hardware-Fehler ist aktiv (`system_error_flag`). Gewinnt immer, unabhängig von allem anderen, das gerade passiert.
* 🔵 **Blau** — die Platine funktioniert aktiv: ein CAN-Frame (beliebige ID) ist in den letzten 1,5 Sekunden angekommen.
* 🟢 **Grün** — im Leerlauf, wartet auf Befehle: kein CAN-Verkehr seit über 1,5 Sekunden.

Der Master kann dies jederzeit noch überschreiben, indem er die CAN-ID `0x100` (DLC 8) mit der Rot-, Grün- und Blau-Intensität als ersten drei Bytes sendet (0-255 je, volle 24-Bit-Farbe, nicht nur die drei automatischen). Eine vom Host gesendete Farbe hält 10 Sekunden, bevor auf das automatische Schema zurückgefallen wird — lang genug, um tatsächlich gesehen zu werden, kurz genug, dass die Platine nicht bei einer veralteten benutzerdefinierten Farbe stecken bleibt, wenn der Host sie nicht mehr aktualisiert. Erneutes Senden von `0x100` (ob dieselbe Farbe oder eine neue) erneuert dieses 10-Sekunden-Fenster, sodass ein Host, der die benutzerdefinierte Kontrolle behalten möchte, sie einfach periodisch weiter senden muss. Ein Hardware-Fehler unterbricht immer eine aktive Überschreibung — Rot hat Priorität vor jeder Farbe, die der Host gesetzt hatte.

Siehe `docs/CANBUS.TXT` (ID `0x100`) für das exakte Byte-Layout, das sich diese Nachricht auch mit der Ring-LED und der OLED-Nachtmodus-Steuerung teilt.

## 📸 Fotos

![URTC v1.0](images/URTC_BOARD.png)

*(In Arbeit — weitere Ansichten und eine bestückte Platine folgen bald.)*

## 🔧 Bauen & Flashen

Der Flash von URTC ist in zwei unabhängige Teile aufgeteilt, sodass die Platine über dasselbe CAN-Nabelschnurkabel neu geflasht werden kann, das sie bereits für alles andere verwendet — ohne dass nach der Ersteinrichtung jemals wieder physischer Zugriff auf den JTAG/SWD-Header nötig wäre.

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
           │                                   │  URTC_MAIN_FIRMWARE_v0.2.3.* — die
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

**Warum ein Backup-Slot.** Ein CAN-Update wird nie in den Slot geschrieben, der gerade läuft. Es geht zuerst in das Backup, wird dort vollständig verifiziert — Größe, CRC32, und eine HMAC-SHA256-Signatur, die beweist, dass es tatsächlich vom eigenen Build-Prozess dieses Projekts stammt, nicht nur, dass es intakt angekommen ist — und wird erst dann in den Hauptslot kopiert. Ein Stromausfall an jedem Punkt vor Beginn dieses Kopiervorgangs lässt die aktuell laufende Firmware vollständig unangetastet, sodass es kein Fenster gibt, in dem ein unterbrochener Download die Platine unbrauchbar machen könnte. Wenn der Stromausfall *während* des Kopiervorgangs selbst passiert, bemerkt es der Bootloader beim nächsten Boot (das Backup, während des Kopierens nie angetastet, ist noch vollständig intakt) und setzt das Kopieren von dort einfach fort, bis es gelingt.

### 0. Aus dem Quellcode kompilieren (optional — `firmware/` enthält bereits vorkompilierte Binärdateien)

Zwei Wege, von diesem Quellcode-Repository zu den 4 obigen Binärdateien zu gelangen:

- **Automatisiert:** `build_firmware.sh` (Linux) oder `build_firmware.bat` (Windows), im Repository-Wurzelverzeichnis. Beide installieren die ARM-GNU-Toolchain, falls sie fehlt, holen den fixierten ST-HAL/CMSIS-Commit, und kompilieren, linken und `objcopy`en alle 4 Binärdateien (Hauptplatinen-Anwendung + Bootloader, Erweiterungs-Slave-Anwendung + Bootloader) direkt nach `firmware/`, und regenerieren dann `firmware/firmware_manifest.json`. Ohne Argumente ausgeführt für einen vollständigen Build, `--clean` um zuerst den lokalen `build/`-Cache zu leeren, oder `master`/`slave`, um nur das eigene Paar eines Chips zu bauen. `build_firmware.sh` läuft end-to-end gegen den echten Quellcode-Baum dieses Projekts; `build_firmware.bat` spiegelt dieselbe Logik für Windows - falls die beiden jemals nicht übereinstimmen, vertrauen Sie der Logik des `.sh`-Skripts als Referenz.
- **Manuell:** jeder Befehl, den eines der beiden Skripte ausführt, plus die Begründung hinter jeder Toolchain-/HAL-Wahl, wird Schritt für Schritt in `docs/COMPILE_STM32F303.TXT` erläutert — nützlich auf einem anderen Betriebssystem, mit einer anderen HAL/CMSIS-Quelle, oder einfach um genau zu sehen, was die Skripte automatisieren.

Nach jeder Firmware-Quellcode-Änderung (oder bevor Sie einer Versionserhöhung vertrauen), führen Sie **`check_version_consistency.sh`** vom Repository-Wurzelverzeichnis aus: Es liest die Track-A/E-Versionskonstanten (Hauptplatinen-Firmware, Erweiterungs-Slave-Anwendung) als Quelle der Wahrheit und prüft jede Stelle, die `VERSION_CHECKLIST.txt` für diesen Versions-Tag dokumentiert, und meldet jede Abweichung — es meldet nur, es behebt selbst nichts. `VERSION_CHECKLIST.txt` ist die vollständige Referenz für alle 5 unabhängigen Versionsstränge dieses Projekts (Haupt-Firmware, Hardware/PCB, Haupt-Bootloader, Erweiterungs-Slave-Anwendung, Erweiterungs-Slave-Bootloader) und genau, was beim Erhöhen jedes einzelnen davon angefasst werden muss.

### 1. Ersteinrichtung — erfordert JTAG/SWD (einmalig)

Der Bootloader kann nur über physische Programmierung auf den Chip gelangen — es gibt keinen Weg, eine Platine per CAN zu flashen, die noch keinen Bootloader hat. Dies ist ein einmaliger Schritt:

1. Öffnen Sie das Projekt in **STM32CubeIDE** (gebaut und getestet gegen das STM32F303CC-Ziel), oder verwenden Sie **STM32CubeProgrammer** direkt mit den unten kompilierten Ausgaben.
2. Flashen Sie **beide** Images über SWD (ST-Link) via den integrierten `STM_JTAG`-Header — jede `.hex`-Datei hat ihre Zieladresse eingebettet, sodass die meisten Tools (einschließlich STM32CubeProgrammer) beide in derselben Sitzung laden können:
   * `URTC_MAIN_BOOTLOADER_v0.3.2.hex` → `0x08000000`
   * `URTC_MAIN_FIRMWARE_v0.2.3.hex` → `0x08008000`
3. Setzen Sie die Werkzeugidentität über die ID-Lötbrücken, bevor Sie einschalten — die Platine liest sie beim Boot einmal, wie immer. Fünf Jumper (ID0-ID4), die den vollen 32-Adress-Raum abdecken (31 direkte Werkzeugadressen, plus die reservierte `11111`-Adresse für freie Konfiguration - siehe den Abschnitt Werkzeugmatrix oben).
4. Schalten Sie ein. Der Bootloader lauscht ~600ms, sieht nichts, und springt direkt in die Anwendung — von hier an verhält sich alles genau so, wie im Rest dieses READMEs beschrieben.

**Der JTAG-Header wird nie entfernt oder deaktiviert.** Er ist immer als Rückfalloption vorhanden — falls ein CAN-Update jemals schiefgeht, oder Sie es einfach bevorzugen, können Sie jederzeit eines der beiden Images über SWD neu flashen.

**Zwei integrierte Drucktaster, BOOT und RESET**, sind auch für die Wiederherstellung vorhanden — RESET ist ein gewöhnlicher Hardware-Reset (`NRST`), und BOOT zieht `BOOT0` hoch, was eine Chip-Ebenen-Entscheidung ist, die getroffen wird, *bevor* überhaupt irgendetwas in diesem Repository läuft: normalerweise (nicht gehalten) bootet der Chip aus dem Flash in den eigenen Bootloader dieses Projekts, wie oben beschrieben; beim Reset gehalten, bootet er stattdessen in STs eigenen werksseitigen System-Memory-Bootloader (USB-DFU/UART-Wiederherstellung, völlig getrennt von allem hier). Siehe `src/F303-master/README.md` Abschnitt 4a für die vollständigen technischen Details.

### 2. Nachfolgende Updates — über den CAN-Bus

Sobald der Bootloader vorhanden ist, benötigt die Aktualisierung der Anwendung überhaupt keinen physischen Zugriff mehr auf die Platine — schicken Sie einfach den neuen Firmware-Build über dieselbe Nabelschnur-CAN-Leitung, die bereits Befehle zum Werkzeugkopf trägt.

**Die Update-Sequenz:**

1. **Auslösen.** Der Master sendet `0x7F0` (DLC 4, Payload `B0 07 1D 5A`) an die *laufende Anwendung*. Sie schaltet sicher die Stromversorgung jedes inline geschalteten Aktuators ab — Motoren, Heizungen, Laser — und setzt den Chip zurück. Diese Magic-Payload-Anforderung bedeutet, dass ein korrupter oder fehlerhafter Frame nicht versehentlich einen Reset in den Update-Modus auslösen kann.
2. **Start.** Nach dem Reset lauscht der Bootloader. Der Master sendet `0x7F1` (DLC 8, Big-Endian Gesamt-Firmware-Größe + Big-Endian HardwareID). Ein für andere Hardware gebautes Image wird genau hier abgelehnt, bevor auch nur ein einziges Byte Flash angetastet wird. Der Bootloader löscht genau so viele Backup-Slot-Seiten, wie das neue Image benötigt, und antwortet mit einem Statusframe (`0x7F5`).
3. **Signatur.** Der Master sendet die erwartete HMAC-SHA256-Signatur als vier `0x7F7`-Frames (je 8 Bytes, in Reihenfolge) — berechnet über das Firmware-Image mit einem Schlüssel, der zwischen dem Bootloader und dem Tool, das den Build signiert, geteilt wird.
4. **Daten.** Der Master streamt die `.bin`-Datei als eine Sequenz von `0x7F2`-Frames (jeweils bis zu 8 Bytes rohe Firmware-Daten), Rücken an Rücken gesendet — CAN garantiert, dass Frames auf einem einzelnen Bus in der Reihenfolge ankommen, in der sie gesendet wurden, sodass keine Frame-Sequenznummer benötigt wird. Der Bootloader puffert eingehende Bytes in einer 2KB-Seite im RAM und schreibt sie einmal voll in den *Backup*-Slot, liest dabei jedes Halbwort zurück und vergleicht es mit dem, was geschrieben werden sollte, bevor die Seite als fertig betrachtet wird, und sendet nach jedem verifizierten Schreibvorgang eine `0x7F3`-Bestätigung (mit dem Seitenindex). Eine vernünftige Master-Implementierung wartet auf das ACK jeder Seite, bevor sie die Daten der nächsten Seite sendet, um den Empfangspuffer des Bootloaders nicht zu überlaufen.
5. **Ende & Verifizierung.** Sobald jedes Byte gesendet wurde, sendet der Master `0x7F4` (DLC 8, Big-Endian CRC32 + Version Major/Minor). Der Bootloader prüft die Größe des Backup-Slots, berechnet dessen CRC32 und HMAC-SHA256 und vergleicht beide mit dem, was der Master deklariert hat. Nur wenn alles übereinstimmt, fährt er fort, das Backup Seite für Seite in den Hauptslot zu kopieren, mit derselben Rücklese-Verifizierung wie oben. Sobald dieser Kopiervorgang abgeschlossen und bestätigt ist, speichert er die neuen Metadaten und setzt sich in die aktualisierte Anwendung zurück. Bei jeder Abweichung — Größe, CRC32, HMAC oder HardwareID — wird der Hauptslot überhaupt nicht angetastet, und der Bootloader kehrt einfach zum Lauschen auf einen neuen Versuch zurück.

**Statusframes (`0x7F5`, DLC 1):** `0x01` lauschend, `0x02` löschend, `0x03` empfangend, `0x06` verifizierend, `0x07` kopiert Backup in Haupt, `0x04` verifiziert OK (kurz vor dem Sprung), `0x05` Verifizierung fehlgeschlagen, `0xFF` Fehler.

**Heartbeat (`0x7F6`, DLC 2, etwa alle 1s während des Lauschens oder Aktualisierens):** Statusbyte + Fortschrittsprozent (0-100, oder `0xFF`, wo ein Prozentsatz nicht zutrifft). Erlaubt dem Master, zu unterscheiden zwischen "Knoten ist am Leben, hat aber noch nicht mit dem Lauschen begonnen" und "Knoten reagiert überhaupt nicht" - nützlich für automatisierte Inbetriebnahme und um einen hängenden Bootloader zu erkennen, ohne auf ein Timeout zu warten.

**Fortschrittsanzeige auf dem Bildschirm.** Der Bootloader steuert das OLED während eines Updates direkt an — niemand muss raten, ob etwas passiert. Er zeigt "UPDATING" plus einen Live-Fortschrittsbalken und Prozentsatz, während Seiten geschrieben oder kopiert werden, "FLASH OK" für einen Moment, bevor er sich in die neue Firmware zurücksetzt, und "ERROR", falls ein Seitenschreibvorgang fehlschlägt, die Übertragung länger als 10 Sekunden stockt, oder die Verifizierung eine Abweichung meldet.

**⚠️ Testen Sie dies auf der Werkbank, bevor Sie es im Feld einsetzen.** Das obige Protokoll kompiliert und linkt sauber, und die Logik wurde sorgfältig durchdacht, aber ein Bootloader ist genau die Art von Firmware, bei der "kompiliert korrekt" weit entfernt ist von "vertrauenswürdig auf Hardware" — das echte Flash-Programmier-Timing, das CAN-Verhalten über eine mehrere-tausend-Frames-Übertragung, und die Übergabe vom Bootloader zur Anwendung müssen alle auf einer echten Platine verifiziert werden (idealerweise mit JTAG als Rückfalloption zur Hand), bevor man sich hierauf für ein unbeaufsichtigtes Update mit angeschlossenen echten Aktuatoren verlässt.

### PC-Tools

Zwei eigenständige, plattformübergreifende (Windows/Linux) GUI-Tools unterstützen diese Platine - **URTC Flasher** (CAN-OTA- und Full-Chip-SWD/JTAG-Updates, sowohl für diese Platine als auch, bei einer Advanced-Erweiterungsvariante, deren eigenen Erweiterungs-Slave-Chip) und **URTC Tester** (ein Live-CAN-Bus-Exerciser, der zeigt, welches Werkzeugprofil gerade gejumpert ist). Beide lebten früher in diesem Repository unter `tools/`; jedes ist nun sein eigenes unabhängiges Projekt, mit eigenem README, eigener Lizenz und eigenen Übersetzungen:

- [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER)
- [URTC Tester](https://github.com/JuanenRac/URTC-TESTER)

Eine webbasierte Alternative, die ähnlichen Boden abdeckt (Live-Überwachung, CAN-Analyse, OTA-Flashen, Wärmebild-Inspektion), ohne dass etwas lokal installiert werden muss, existiert ebenfalls: [URTC Web Studio](https://github.com/JuanenRac/URTC-WEB-STUDIO).

## 📋 Änderungsprotokoll

Firmware und Bootloader werden unabhängig voneinander versioniert und veröffentlicht - das Flashen eines neuen Bootloaders impliziert keine neue Anwendungsversion und umgekehrt, sodass jedes seine eigene Historie in seiner eigenen Datei erhält, statt einer einzigen kombinierten Versionsnummer, die implizieren würde, dass sie sich immer zusammen bewegen:

- Firmware (`src/F303-master/`): [`src/F303-master/CHANGELOG.md`](src/F303-master/CHANGELOG.md)
- Bootloader (`src/F303-master/boot/`): [`src/F303-master/boot/CHANGELOG.md`](src/F303-master/boot/CHANGELOG.md)
- Erweiterungs-Slave-Anwendung (`src/F303-slave/`, STM32F303CBT6): [`src/F303-slave/CHANGELOG.md`](src/F303-slave/CHANGELOG.md)
- Erweiterungs-Slave-Bootloader (`src/F303-slave/boot/`): [`src/F303-slave/boot/CHANGELOG.md`](src/F303-slave/boot/CHANGELOG.md)

**Versionierungsrichtlinie:** Alle 4 Komponenten (2 Anwendungsfirmwares, 2 Bootloader - `FIRMWARE_VERSION_MAJOR`/`MINOR`/`PATCH` und `BOOTLOADER_VERSION_MAJOR`/`MINOR`/`PATCH`) sind **inkrementell** - jeder reale Build erhöht den eigenen `PATCH` dieser Komponente automatisch um 1 (`bump_version.py` im Repository-Root, aufgerufen von `build_firmware.sh`/`.bat` unmittelbar vor dem Kompilieren jeder Komponente), mit Übertrag auf `MINOR` (dann `MAJOR`), sobald `PATCH` 9 überschreiten würde - dieselbe Zehnerbasis-Regel wie bei einem echten Kilometerzähler, z. B. `0.1.7` → `0.1.8` → `0.1.9` → `0.2.0`, niemals `0.1.10`. Jeder Bootloader führt außerdem eine eigene Kopie der `FIRMWARE_VERSION_*` der zugehörigen Anwendung, die durch denselben Bump automatisch synchronisiert wird. Siehe [`CHANGELOG.md`](CHANGELOG.md) im Repository-Root für den aktuellen Stand aller 4 Komponenten auf einen Blick, und [`VERSION_CHECKLIST.txt`](VERSION_CHECKLIST.txt) für die vollständige Mechanik je Track.

## 🔍 Aktueller Status

**Firmware (`src/F303-master/`):** funktionsvollständig für alle 25 Werkzeugprofile — thermische PID-Regelung, werkzeugspezifische Telemetrie, Kommunikations-Watchdogs, Stall-/Fehlererkennung, und die eigene Live-Diagnose des OLED, zusammen mit einem Abfragepaar für das aktive Werkzeug (`0x110`/`0x111`), einem generischen SPI-Passthrough (`0x180`/`0x181`) für den Erweiterungsstecker, einem integrierten F-RAM, das Sollwerte über einen Stromausfall hinweg erhält (`0x190`/`0x191`), dem `11111`-Jumper-Mechanismus für freie Werkzeugkonfiguration (`0x1A2`/`0x1A3`), Peripherietyp- + Geräteseriennummer-Meldung (`0x1A4`/`0x1A5`) zur Unterscheidung mehrerer ansonsten identischer Platinen auf einem gemeinsamen Bus, und einer CAN-zu-I2C-Bridge (`0x210`-`0x221`), die den Erweiterungs-Slave-Chip auf advanced-Erweiterungsplatinen erreicht. Unabhängig vom Bootloader versioniert (siehe das Änderungsprotokoll unten).

**Bootloader (`src/F303-master/boot/`):** funktionsvollständiges Golden-Image-A/B-Update-System — HMAC-SHA256-signierte OTA-Updates über CAN, ein Backup-Slot, der garantiert, dass ein fehlgeschlagenes Update die Platine nie unbrauchbar macht, und eine eigene Versionsmeldung (`0x7FA`), unabhängig von der Anwendung. Kompiliert und linkt sauber; siehe den Werkbank-Test-Vorbehalt oben, bevor Sie sich unbeaufsichtigt mit angeschlossenen echten Aktuatoren darauf verlassen.

**PC-Tools:** sowohl [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER) (CAN-OTA-Updates + Full-Chip-SWD/JTAG-Programmierung) als auch [URTC Tester](https://github.com/JuanenRac/URTC-TESTER) (Live-Werkzeug-Steuerungs-/Telemetrie-Exerciser pro Werkzeug) sind funktionsvollständig für das, was sie erreichen sollen, jedes nun sein eigenes unabhängiges Projekt mit eigenem README, das die Einrichtung und jede Bedienung im Detail abdeckt.

**Hardware:** Schaltplan und Stückliste werden noch finalisiert; es existiert noch keine bestückte Platine, um irgendetwas Obiges gegen echte Silizium zu validieren. Alles Obige kompiliert, linkt, und wurde sorgfältig durchdacht, aber "kompiliert korrekt" und "auf Hardware verifiziert" sind zwei verschiedene Behauptungen — siehe den Sicherheitshinweis am Anfang dieses READMEs, und behandeln Sie eine erste Inbetriebnahme mit der Vorsicht, die jede neue Platine verdient.

Falls jemand in der Community an benutzerdefinierten Endeffektoren, intelligenten Werkzeugwechslern, oder erweiterter Werkzeugintegration für PAROL6, Faze4, oder eine andere Roboterarm-Plattform arbeitet, würde ich mich freuen, zu chatten, Ideen auszutauschen, oder tiefer in die CAN-Befehle einzutauchen!

## 📂 Repository-Struktur

```
/
├── 3D/
│   ├── RACK/                    Platinen-Montagerahmen, 2 Varianten (x1, x3) - jeweils in
│   │                            .stl/.3mf/.amf/.scad
│   ├── REVOLVER/                Platzhalter - leer, Inhalt noch nicht begonnen
│   └── TOOLS/
│       └── PAROL6/              Pro-Werkzeug 3D-druckbare Teile für den PAROL6-Roboterarm -
│                                ein Unterordner pro Werkzeug (0.Universal parts, dann 1-12
│                                entsprechend der Werkzeugkatalog-Nummerierung oben), jeweils in
│                                .stl/.3mf/.amf/.scad wo befüllt; mehrere
│                                (4, 6-12) sind noch leere Platzhalter
├── ani/                          27 GIFs: eine 4-Bilder-Animation pro Werkzeugprofil (00-24,
│                                 entsprechend der eigenen numerischen ID jedes Werkzeugs), der Boot-Splash
│                                 (splash_boot.gif), und die Ungültige-ID-Warnung
│                                 (error_warning.gif) - alle direkt aus der eigenen
│                                 Firmware-Quelle dieses Projekts dekodiert (die eigenen
│                                 ToolIcons[]/SplashFace[]/ErrorText[]-Tabellen von firmware_render.c),
│                                 nicht separat handgezeichnet, sodass sie immer mit dem
│                                 übereinstimmen, was das echte OLED tatsächlich zeigt
├── BOM/
│   ├── BOM.TXT                  Vollständige Stückliste der PCB-Platine
│   ├── BOM_EXPANSION_BASIC_TMC2209.TXT     Erweiterungsplatine, basic + TMC2209
│   ├── BOM_EXPANSION_BASIC_TMC5160A.TXT    Erweiterungsplatine, basic + TMC5160A
│   ├── BOM_EXPANSION_ADVANCED_TMC2209.TXT  Erweiterungsplatine, advanced + TMC2209
│   ├── BOM_EXPANSION_ADVANCED_TMC5160A.TXT Erweiterungsplatine, advanced + TMC5160A
│   ├── BOM_EXPANSION_BASIC_ADS1115.TXT     Erweiterungsplatine, basic + ADS1115 (nur Sensor, kein Treiber/MCU)
│   └── BOM_EXPANSION_BASIC_MLX9064X.TXT    Erweiterungsplatine, basic + MLX9064x (nur Sensor, kein Treiber/MCU)
├── docs/
│   ├── CANBUS.TXT               CAN-Bus-Protokollreferenz (alle Befehls-/Telemetrie-IDs)
│   ├── ECOVIA.TXT               Werkzeugidentifikationsmatrix und Pin-Mutationslogik
│   ├── TOOLS.TXT                Übergeordneter Katalog aller 25 Werkzeuge - was jedes tut und
│   │                            welche Peripheriegeräte es verwendet, keine Pin-Ebenen-Details
│   ├── PINOUT.TXT               Vollständige MCU-Pinbelegung, Block für Block
│   ├── PINOUT_CONNECTORS.TXT    Physische Steckerpinbelegungen (CONN_DRILL, CONN_SEN, usw.)
│   ├── EXPANSION.TXT            CONN_EXPANSION-Stecker und die Zusatzplatinen-Varianten
│   ├── PINOUT_SLAVE.txt         Vollständige Pinbelegung für den Erweiterungs-Slave-Chip (nur advanced-Varianten)
│   ├── EEPROM.TXT               Vollständige F-RAM-Registerkarte (jede persistierte Einstellung, Byte-Offsets)
│   ├── COMPILE_STM32F303.TXT    Von-Grund-auf-Bauanleitung für alle 4 Firmware-Binärdateien -
│   │                            Toolchain, ST-HAL/CMSIS-Einrichtung, exakte Kompilier-/Link-Befehle;
│   │                            build_firmware.sh/.bat im Repository-Wurzelverzeichnis automatisieren diesen
│   │                            genau selben Prozess end-to-end
│   ├── datasheet/               2 Komponenten-Datenblätter, die nicht bereits unter
│   │                            PCB/datasheet/ abgedeckt sind (CFM_40.pdf, EFB0424VHD-CP0.pdf)
│   └── tool_image_generator/    Toolkit, das images/TOOL_*.png generiert (siehe unten) -
│                                render_engine.py + tool_data.py + generate_all.py, und
│                                PROCEDURE.TXT, das erklärt, wie man das Bild eines neuen Werkzeugs hinzufügt
│                                oder ein bestehendes neu generiert
├── src/
│   ├── F303-master/
│   │   ├── STM32F303CC_main.c    Einstiegspunkt - globale Definitionen und main()
│   │   ├── firmware_*.c/.h       ~85 weitere Dateien, eine pro Subsystem (OLED, LEDs, werkzeugspezifische
│   │   │                         CAN-Handler, Initialisierung, Persistenz, usw.), einschließlich
│   │   │                         firmware_ads1115.c (direkter ADS1115-Treiber, Basic+ADS1115-
│   │   │                         Platine) - siehe das eigene README.md dieses Ordners für die vollständige
│   │   │                         Datei-für-Datei-Tabelle
│   │   ├── melexis_mlx90640/     Melexis' eigene offizielle MLX90640-Bibliothek (Apache-2.0,
│   │   │                         reines C) plus der eigene Direktverbindungstreiber dieser Platine
│   │   │                         darauf, für die Basic+MLX9064x-Erweiterungsplatine
│   │   ├── melexis_mlx90641/     Dieselbe Idee, MLX90641-Bibliothek (Apache-2.0, C++ - siehe das eigene
│   │   │                         README.md dieses Ordners Abschnitt 8a dafür, warum diese eine
│   │   │                         Bibliothek C++ ist in einem ansonsten reinen C-Projekt)
│   │   ├── melexis_mlx90642/     Dieselbe Idee, MLX90642-Bibliothek (Apache-2.0, reines C) - siehe
│   │   │                         Abschnitt 8a dafür, warum der eigene Treiber dieses Sensors wirklich
│   │   │                         einfacher ist als die anderen 2
│   │   ├── STM32F303CCTx_APP.ld  Linker-Skript für die Anwendung (112K Hauptslot bei 0x08008000)
│   │   ├── README.md             Technische Referenz: Hardware-Plattform, das ID-Jumper-
│   │   │                         Werkzeugauswahlsystem, werkzeugspezifische Peripherieverdrahtung - siehe
│   │   │                         CANBUS.TXT für das Draht-Ebenen-Protokoll, dessen Warum dies erklärt
│   │   └── boot/
│   │       ├── bootloader_main.c  Einstiegspunkt für den Bootloader
│   │       ├── bootloader_*.c/.h  9 weitere Dateien (gemeinsame Typen/Konstanten, Kryptografie,
│   │       │                      Flash/Metadaten, OLED, CAN-Protokoll)
│   │       ├── STM32F303CCTx_BOOTLOADER.ld  Linker-Skript für den Bootloader (30K-Region bei 0x08000000)
│   │       └── README.md          Dieselbe technische Referenzrolle wie die der Anwendung, für den Bootloader
│   └── F303-slave/               Begleitchip (STM32F303CBT6) nur auf den 2 ADVANCED-Erweiterungs-
│       │                         platinen-Varianten - siehe den Abschnitt Erweiterungsstecker
│       │                         oben. Eigenes Bootloader-/Anwendungspaar, eigenes I2C-basiertes
│       │                         (nicht CAN) Update-Protokoll, eigene unabhängige Versionierung.
│       ├── slave_main.c          Einstiegspunkt
│       ├── slave_*.c/.h          7 weitere Dateien (gemeinsame Typen/Konstanten, I2C-Verbindungsprotokoll,
│       │                         lokaler Sensorbus, lokales PWM)
│       ├── STM32F303CBTx_SLAVEAPP.ld  Linker-Skript (54K Hauptslot bei 0x08005000)
│       ├── README.md             Technische Referenz: warum dieser Chip existiert, der lokale
│       │                         ADS1115/MLX9064x-Sensorbus, lokales PWM, das I2C-Verbindungs-
│       │                         protokoll zur Hauptplatine
│       ├── melexis_mlx90640/     Melexis' eigene offizielle MLX90640-Bibliothek (Apache-2.0,
│       │                         reines C, unverändert, eigene Lizenzdatei) - als eigene
│       │                         separate Kompilierungseinheit gehalten, absichtlich nie in
│       │                         den eigenen Quellcode dieses Projekts eingegliedert, da Apache-2.0
│       │                         verlangt, dass der eigene Urheberrechtshinweis dieses Codes intakt bleibt
│       ├── melexis_mlx90641/     Melexis' eigene offizielle MLX90641-Bibliothek (Apache-2.0, C++ -
│       │                         eine wirklich separate Bibliothek von MLX90640's eigener, keine
│       │                         Variante davon - siehe das eigene README.md dieses Ordners Abschnitt 3
│       │                         dafür, warum sie C++ ist und wie der Build damit umgeht)
│       ├── melexis_mlx90642/     Melexis' eigene offizielle MLX90642-Bibliothek (Apache-2.0, reines
│       │                         C) - wirklich einfachere Transportschnittstelle als die der anderen
│       │                         2 Sensoren, siehe README.md Abschnitt 3 dafür, warum
│       └── boot/
│           ├── slaveboot_main.c   Einstiegspunkt für den Bootloader
│           ├── slaveboot_*.c/.h   7 weitere Dateien (Kryptografie, Flash/Metadaten, Protokoll)
│           ├── STM32F303CBTx_SLAVEBOOT.ld  Linker-Skript (18K-Region bei 0x08000000)
│           └── README.md          Dieselbe technische Referenzrolle wie die der Anwendung
├── firmware/
│   ├── URTC_MAIN_BOOTLOADER_v0.3.2.bin  Bootloader kompiliert, flashen auf 0x08000000
│   ├── URTC_MAIN_BOOTLOADER_v0.3.2.elf  Bootloader kompiliert, flashen auf 0x08000000
│   ├── URTC_MAIN_BOOTLOADER_v0.3.2.hex  Bootloader kompiliert, flashen auf 0x08000000 (Adresse eingebettet)
│   ├── URTC_MAIN_FIRMWARE_v0.2.3.bin    Anwendungs-bin kompiliert, flashen auf 0x08008000
│   ├── URTC_MAIN_FIRMWARE_v0.2.3.elf    Anwendungs-elf kompiliert, flashen auf 0x08008000
│   ├── URTC_MAIN_FIRMWARE_v0.2.3.hex    Anwendungs-HEX kompiliert, flashen auf 0x08008000 (Adresse eingebettet)
│   ├── URTC_SLAVE_BOOTLOADER_v0.1.5.{bin,elf,hex}  Eigener Bootloader des Erweiterungs-Slave, flashen auf 0x08000000
│   │                             auf dem STM32F303CBT6 (nur advanced-Erweiterungsplatinen)
│   ├── URTC_SLAVE_FIRMWARE_v0.1.2.{bin,elf,hex}  Eigene Anwendung des Erweiterungs-Slave, flashen auf 0x08005000
│   └── firmware_manifest.json    Maschinenlesbarer Index aller 4 obigen Komponenten - Version,
│                                 Flash-Adresse, und die eigene Größe/CRC32 jeder Datei, damit ein
│                                 externes Tool prüfen kann, was hier ist und was neuer ist als
│                                 das, was es aktuell hat. Automatisch regeneriert von
│                                 generate_manifest.py (aufgerufen vom eigenen letzten Schritt von
│                                 build_firmware.sh/.bat) - nie von Hand bearbeitet.
├── images/
│   ├── OLED_DIRECT_MOUNT.jpg     LCD1/CONN_OLED2 - nacktes 30-Pin-FPC-Panel, Direktmontage-Option
│   ├── OLED_BREAKOUT_MODULE.jpg  CONN_OLED - externes I2C-Breakout-Modul, alternative Option
│   ├── URTC_LOGO.svg             Allgemeines Projektlogo, oben in diesem README eingebettet
│   ├── URTC_BOARD.png           Platinenfoto
│   ├── URTC_SCHEMATIC.png       Platinenschaltplan
│   ├── URTC_PCB_TOP.png         Platinen-OBERseite-Lage (wenn hinzugefügt)
│   ├── URTC_PCB_BOTTOM.png      Platinen-UNTERseite-Lage (wenn hinzugefügt)
│   └── TOOL_*.png               Pro-Werkzeug Jumper-/Verdrahtungs-Referenzdiagramm, eines pro Profil
│                                (alle 25 vorhanden - siehe den eigenen Link jedes Werkzeugs im Werkzeugkatalog oben)
├── PCB/
│   ├── URTC_V1.0.sch            Eagle-Schaltplan (wenn hinzugefügt)
│   ├── URTC_V1.0.brd            Eagle-Board-Layout (wenn hinzugefügt)
│   ├── URTC_V1.0_JLCPCB.ZIP     Gerber-, BOM- und CPL-Dateien (wenn hinzugefügt)
│   ├── URTC_BOM.TXT             Eagle-exportierte Roh-Stückliste (Ground-Truth-Export - siehe
│   │                            BOM/BOM.TXT für die eigene kuratierte, organisierte Version dieses Projekts)
│   ├── datasheet/               Datenblätter aller in der Platine verwendeten Teile
│   └── *_PARLIST/PINLIST/NETLIST.TXT   Eagle-exportierte Netzlisten (Ground Truth für Pin-Zuordnung)
├── VERSION_CHECKLIST.txt        Mechanische Checkliste zum korrekten Erhöhen jeder der 4 eigenen
│                                unabhängigen Versionsnummern dieses Projekts
├── check_version_consistency.sh  Automatisierte Versions-/Dateikonsistenzprüfungen - vor dem
│                                Vertrauen in die eigenen Behauptungen von VERSION_CHECKLIST.txt auszuführen
├── build_firmware.sh            Installiert die Toolchain, holt STs eigenes HAL/CMSIS, und
│                                kompiliert alle 4 Firmware-Binärdateien end-to-end (Linux)
├── build_firmware.bat           Dasselbe, für Windows - siehe docs/COMPILE_STM32F303.TXT für
│                                den vollständigen manuellen Prozess, den jedes Skript automatisiert
├── generate_manifest.py         Regeneriert firmware/firmware_manifest.json - automatisch
│                                aufgerufen als letzter Schritt eines vollständigen
│                                build_firmware.sh/.bat-Laufs, oder eigenständig jederzeit, wenn das
│                                Manifest aufholen muss, ohne einen vollständigen Rebuild
├── LICENSE
├── README.md                    Diese Datei
└── README_spa.md / README_ita.md / README_fra.md / README_deu.md / README_zho.md / README_jpn.md  <- Übersetzungen
```

Hardware-Design-Dateien (Eagle-Schaltplan/Board/Netzlisten) werden hinzugefügt, sobald sich das Layout stabilisiert.

## 🔗 Verwandte Projekte

Dieses Projekt ist Teil eines größeren Robotik-Ökosystems desselben Autors (JuanenRac / Electro Hobby 3D), das viele Projekte über Firmware, Hardware und Software hinweg umfasst. Gut zu wissen, da eine Anfrage tatsächlich eines dieser Projekte betreffen könnte, statt dieses Repositorys.

**Direkt mit diesem Projekt verwandt**
- **[URTC-SMART-RACK](https://github.com/JuanenRac/URTC-SMART-RACK)** — teilt sich dasselbe Werkzeug-Ökosystem und denselben CAN-Bus mit dieser Firmware.
- **[URTC-VISION-TOOL](https://github.com/JuanenRac/URTC-VISION-TOOL)** — teilt sich dasselbe Werkzeug-Ökosystem und denselben CAN-Bus mit dieser Firmware.
- **[HYDRA-UMC-DETECTION-HEF](https://github.com/JuanenRac/HYDRA-UMC-DETECTION-HEF)** — liefert die visuelle Erkennung hinter den eigenen Werkzeugen dieser Firmware.

**Rest des Ökosystems**

💠 Kern-Ökosystem
- [HYDRA-UMC](https://github.com/JuanenRac/HYDRA-UMC)
- [HYDRA-UMC-SERVER](https://github.com/JuanenRac/HYDRA-UMC-SERVER)
- [HYDRA-UMC-STUDIO](https://github.com/JuanenRac/HYDRA-UMC-STUDIO)
- [HYDRA-UMC-SUITE](https://github.com/JuanenRac/HYDRA-UMC-SUITE)
- [HYDRA-UMC-DSI](https://github.com/JuanenRac/HYDRA-UMC-DSI)
- [HYDRA-UMC-ANDROID-CONTROL](https://github.com/JuanenRac/HYDRA-UMC-ANDROID-CONTROL)
- [HYDRA-UMC-IOS-CONTROL](https://github.com/JuanenRac/HYDRA-UMC-IOS-CONTROL)
- [HYDRA-UMC-EDITOR-URDF](https://github.com/JuanenRac/HYDRA-UMC-EDITOR-URDF)
- [URTC-FLASHER](https://github.com/JuanenRac/URTC-FLASHER)
- [URTC-TESTER](https://github.com/JuanenRac/URTC-TESTER)
- [URTC-WEB-STUDIO](https://github.com/JuanenRac/URTC-WEB-STUDIO)

👁️ Vision-KI-Knoten (Hailo-8)
- [HYDRA-UMC-VISION-NODE](https://github.com/JuanenRac/HYDRA-UMC-VISION-NODE)
- [HYDRA-UMC-VISION-STREAMER](https://github.com/JuanenRac/HYDRA-UMC-VISION-STREAMER)
- [HYDRA-UMC-SAFETY-ZONES](https://github.com/JuanenRac/HYDRA-UMC-SAFETY-ZONES)
- [HYDRA-UMC-VISUAL-SERVOING-API](https://github.com/JuanenRac/HYDRA-UMC-VISUAL-SERVOING-API)

🧠 Kognitiver KI-Knoten (Hailo-10)
- [HYDRA-UMC-COGNITIVE-NODE](https://github.com/JuanenRac/HYDRA-UMC-COGNITIVE-NODE)
- [HYDRA-UMC-VLA-ENGINE](https://github.com/JuanenRac/HYDRA-UMC-VLA-ENGINE)
- [HYDRA-UMC-VOICE-UI](https://github.com/JuanenRac/HYDRA-UMC-VOICE-UI)
- [HYDRA-UMC-SEMANTIC-PLANNER](https://github.com/JuanenRac/HYDRA-UMC-SEMANTIC-PLANNER)
- [HYDRA-UMC-DOCS-QA](https://github.com/JuanenRac/HYDRA-UMC-DOCS-QA)

🐝 Orchestrierung & Schwarm
- [HYDRA-UMC-ORCHESTRATOR](https://github.com/JuanenRac/HYDRA-UMC-ORCHESTRATOR)
- [HYDRA-UMC-SWARM-SYNC](https://github.com/JuanenRac/HYDRA-UMC-SWARM-SYNC)
- [HYDRA-UMC-PATH-PLANNER-3D](https://github.com/JuanenRac/HYDRA-UMC-PATH-PLANNER-3D)
- [HYDRA-UMC-JOB-DISPATCHER](https://github.com/JuanenRac/HYDRA-UMC-JOB-DISPATCHER)
- [HYDRA-UMC-NODE-HEALING](https://github.com/JuanenRac/HYDRA-UMC-NODE-HEALING)

🎮 Digitaler Zwilling & Simulation
- [HYDRA-UMC-TWIN](https://github.com/JuanenRac/HYDRA-UMC-TWIN)
- [HYDRA-UMC-PHYSICS-REPLICA](https://github.com/JuanenRac/HYDRA-UMC-PHYSICS-REPLICA)
- [HYDRA-UMC-HIL-BRIDGE](https://github.com/JuanenRac/HYDRA-UMC-HIL-BRIDGE)
- [HYDRA-UMC-SYNTHETIC-DATA-GEN](https://github.com/JuanenRac/HYDRA-UMC-SYNTHETIC-DATA-GEN)

📊 Daten & Analytik
- [HYDRA-UMC-DATALAKE](https://github.com/JuanenRac/HYDRA-UMC-DATALAKE)
- [HYDRA-UMC-TELEMETRY-COLLECTOR](https://github.com/JuanenRac/HYDRA-UMC-TELEMETRY-COLLECTOR)
- [HYDRA-UMC-ANOMALY-DETECTOR](https://github.com/JuanenRac/HYDRA-UMC-ANOMALY-DETECTOR)
- [HYDRA-UMC-PRODUCTION-REPORTS](https://github.com/JuanenRac/HYDRA-UMC-PRODUCTION-REPORTS)

🏭 Industrielles Gateway
- [HYDRA-UMC-GATEWAY-INDUSTRIAL](https://github.com/JuanenRac/HYDRA-UMC-GATEWAY-INDUSTRIAL)
- [HYDRA-UMC-OPCUA-SERVER](https://github.com/JuanenRac/HYDRA-UMC-OPCUA-SERVER)
- [HYDRA-UMC-MQTT-BROKER](https://github.com/JuanenRac/HYDRA-UMC-MQTT-BROKER)
- [HYDRA-UMC-MTCONNECT-ADAPTER](https://github.com/JuanenRac/HYDRA-UMC-MTCONNECT-ADAPTER)

🛠️ Ergänzende Werkzeuge
- [HYDRA-UMC-WATCH](https://github.com/JuanenRac/HYDRA-UMC-WATCH)
- [HYDRA-UMC-TOOL-CLI](https://github.com/JuanenRac/HYDRA-UMC-TOOL-CLI)
- [HYDRA-UMC-DASHBOARD-AI](https://github.com/JuanenRac/HYDRA-UMC-DASHBOARD-AI)

## 👤 Autor

**JuanenRac** (Electro Hobby 3D)
📧 electrohobby3d@gmail.com
📺 [youtube.com/@electrohobby3d](https://youtube.com/@electrohobby3d)

## 📜 Lizenz und Urheberrechtshinweise

URTC ist (c) 2026 JuanenRac (Electro Hobby 3D). Dieser Hinweis muss in jeder Verbreitung dieses Projekts oder abgeleiteter Werke enthalten sein.

Da dieses Projekt aus mehreren verschiedenen Arten von Inhalten besteht, werden einzelne Teile unter verschiedenen Lizenzen zur Verfügung gestellt - jede passend zu dem, was sie tatsächlich abdeckt, statt eine einzige Lizenz für alles zu erzwingen:

1. Die **Firmware** unter `./firmware` (sowohl Anwendung als auch CAN-Bootloader) ist verfügbar unter der **GNU General Public License v3.0 (GPL-3.0)**. Vollständiger Text unter https://www.gnu.org/licenses/gpl-3.0.html.

2. Die **Hardware-Designs** (Eagle-Schaltplan-/Board-Dateien, Gerber, und die 3D-druckbaren Teile unter `./PCB` und `./3D`) sind verfügbar unter der **CERN Open Hardware Licence v2 - Strongly Reciprocal (CERN-OHL-S v2)**. Vollständiger Text unter https://cern-ohl.web.cern.ch/.

3. Die **Dokumentation** (dieses README und dessen eigene Übersetzungen - `README_spa.md`, `README_ita.md`, `README_fra.md`, `README_deu.md`, `README_zho.md`, `README_jpn.md` - plus die Referenzdateien unter `./docs`) ist verfügbar unter **Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)**. Vollständiger Text unter https://creativecommons.org/licenses/by-sa/4.0/.

Wenn Sie auf diesem Projekt aufbauen, denken Sie an die Lizenzaufteilung: Code-Änderungen an der Firmware sollten GPL-3.0 bleiben, Hardware-Modifikationen sollten CERN-OHL-S bleiben, und Dokumentationsableitungen sollten CC BY-SA bleiben - jede mit Namensnennung zurück zu diesem Projekt.

Dieses Repository deckt nur die eigene Firmware und Hardware der URTC-Platine ab - die PC-Tools (URTC Flasher, URTC Tester), die früher hier lebten, sind nun unabhängige Projekte mit eigener Lizenzierung, siehe "PC-Tools" oben.
