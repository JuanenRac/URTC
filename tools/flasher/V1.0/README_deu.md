<p align="center">
  <img src="/images/URTC_LOGO_FLASHER.svg" alt="URTC Flasher Logo" width="100%">
</p>

# URTC Flasher (Windows / Linux)

**Version:** 1.0 (die Version dieses Tools selbst - angezeigt im
Fenster-Banner und in der Titelleiste, getrennt verfolgt von der
Firmware-Version der URTC-Platine, die es schreibt)

**Autor:** JuanenRac (Electro Hobby 3D) &lt;electrohobby3d@gmail.com&gt;

Lizenz: **GPL-3.0**, dieselbe wie die URTC-Firmware selbst — siehe
`LICENSE` im Wurzelverzeichnis des Repositorys. Dies deckt
`urtc_flasher.py` und jedes daraus erstellte Binary ab.

Ein kleines plattformübergreifendes GUI-Tool zum Aktualisieren der
URTC-Platinen-Firmware über den CAN-Bus. Es implementiert genau das
Bootloader-Protokoll aus `docs/CANBUS.TXT`: die HardwareID-Prüfung, die
HMAC-SHA256-Signierung, den Golden-Image-Backup-Slot-Update-Ablauf, den
Live-Fortschritt über die Heartbeat-Nachrichten des Bootloaders, und
eine Versionsabfrage (identifiziert, ob sich die Anwendung *oder* der
Bootloader dieser Platine über CAN meldet), damit Sie sehen können, was
aktuell installiert ist, bevor Sie entscheiden, was geflasht werden
soll.

Zwei Wege, mit der Platine zu sprechen, beide sprechen darunter dasselbe
Protokoll:

- **Seriell / SLCAN** — funktioniert unter Windows und Linux. Benötigt
  einen USB-CAN-Adapter mit SLCAN-Firmware, verbunden als virtueller
  serieller Port.
- **SocketCAN** — **nur Linux**, und nur in der Benutzeroberfläche des
  Tools unter Linux angezeigt. Spricht direkt mit einer
  Kernel-Netzwerkschnittstelle `can0`/`slcan0`. Wenn Ihr Adapter
  bereits `gs_usb`/candleLight-Firmware ausführt (die meisten
  CANable-Platinen tun dies ab Werk), benötigt dieser Weg
  **überhaupt kein Reflashen des Adapters** — der native Treiber von
  Linux übernimmt dies direkt.

**Status:** die CRC32- und HMAC-SHA256-Berechnung in diesem Tool wurde
Byte für Byte gegen die eigene C-Implementierung des Bootloaders
verifiziert, und das SocketCAN-Frame-Packing wurde gegen das
`struct can_frame`-Layout von Linux mit einem Hin-und-Rück-Pack/Unpack-
Test verifiziert. Was auf keiner der 2 Plattformen getestet wurde, ist
eine echte Platine auf echter Hardware - behandeln Sie einen ersten
echten Flash-Versuch mit derselben Vorsicht, die Sie jedem neuen Tool
entgegenbringen würden, das mit einem Bootloader spricht: halten Sie
JTAG als Rückfalloption bereit.

## 1. Bringen Sie Ihren Adapter zum CAN-Sprechen

Was Sie davon brauchen, hängt von Ihrer Plattform und dem verwendeten
Transport ab:

**Linux, SocketCAN-Weg (empfohlen, wenn Ihr Adapter dies
unterstützt):**
Nichts am Adapter selbst zu flashen. Aktivieren Sie die Schnittstelle
einmal pro Start (oder fügen Sie sie zu Ihrer Netzwerkkonfiguration
hinzu, damit sie bestehen bleibt):
```
sudo modprobe can vcan gs_usb   # gs_usb deckt die meisten Platinen der CANable-Familie ab
sudo ip link set can0 type can bitrate 500000
sudo ip link set can0 up
```
Wenn sich Ihr Adapter unter einem anderen Namen als `can0` anmeldet,
prüfen Sie `ip link show` (oder `dmesg` direkt nach dem Anschließen)
für den tatsächlichen Namen. Manche Adapter benötigen `slcand` statt
eines nativen Treibers - wenn `ip link show` nach dem Anschließen
überhaupt keine CAN-Schnittstelle zeigt, ist dies wahrscheinlich Ihr
Fall; siehe die Dokumentation Ihres Adapters für den `slcand`-Aufruf,
der eine `slcan0`-Schnittstelle erstellt, die Sie dann auf dieselbe
Weise wie oben aktivieren.

**Windows, oder Linux über den Seriell/SLCAN-Weg:**
Eine CANable Pro v2 wird standardmäßig mit **candleLight**-Firmware
ausgeliefert, die mit dem Host über das `gs_usb`-Protokoll spricht -
dasselbe, das der `gs_usb`-Treiber von SocketCAN unter Linux nativ
erwartet (siehe oben). Dieses Protokoll präsentiert sich **nicht** als
serieller Port, was dieser Weg benötigt. Um stattdessen Seriell/SLCAN zu
verwenden (unter Windows erforderlich; unter Linux optional):

1. Laden Sie SLCAN-kompatible Firmware für Ihren Adapter herunter
   (suchen Sie nach "canable slcan firmware" — es gibt einige gepflegte
   Forks; verwenden Sie den, auf den die Dokumentation Ihres eigenen
   Adapters verweist).
2. Versetzen Sie den Adapter in den DFU/Bootloader-Modus (normalerweise
   ein BOOT-Knopf, der beim Einschalten gehalten wird, oder ein
   Jumper - prüfen Sie die Dokumentation Ihres Adapters).
3. Flashen Sie die SLCAN-Firmware mit dem Flash-Tool des Herstellers
   Ihres Adapters oder `dfu-util`.
4. Wieder verbinden - es sollte sich nun als serieller Port anmelden:
   ein COM-Port unter Windows, oder im Stil von
   `/dev/ttyACM0`/`/dev/ttyUSB0` unter Linux.

Wenn Ihr Adapter bereits SLCAN-Firmware ausführt, springen Sie direkt zu
Schritt 2 unten.

Eine empfangene SLCAN-Zeile, deren tatsächliche Länge nicht mit dem
übereinstimmt, was ihr eigenes deklariertes DLC impliziert, wird als
fehlerhaft behandelt und übersprungen, statt anhand ihrer ersten N
Hex-Zeichen unabhängig davon analysiert zu werden, was folgt - gut zu
wissen, wenn Sie gegen einen verrauschten oder nicht-standardmäßigen
Adapter debuggen.

## 2. Installation und Ausführung

**Windows:**
```
python -m pip install -r requirements.txt
python urtc_flasher.py
```
Oder erstellen Sie eine eigenständige `.exe` mit `build_exe.bat` (siehe
diese Datei).

**Linux:**
```
python3 -m pip install -r requirements.txt
python3 urtc_flasher.py
```
Oder erstellen Sie ein eigenständiges Binary mit `./build_exe.sh`
(zuerst `chmod +x`).

Beide Skripte übergeben `--noconfirm` an PyInstaller, sodass ein
Neubau über einem bereits existierenden `dist/URTC_Flasher` dieses
direkt ersetzt, statt auf eine "ersetzen?"-Eingabeaufforderung zu
warten, die in der Ausgabe eines Skripts leicht übersehen wird.

### Menüleiste

- **Datei** - Protokolle speichern (das Protokoll auf dem Bildschirm als
  Klartext; für ein vollständigeres Paket mit Systemdiagnose und der
  aktuell ausgewählten Firmware-Datei siehe stattdessen "Diagnose"
  weiter unten), und Beenden.
- **Sprache** - zwischen den 5 verfügbaren Sprachen wechseln (siehe
  "Sprache" weiter unten, wie Übersetzungen funktionieren).
- **Hilfe** - Readme (öffnet diese Datei in einem schreibgeschützten
  Betrachterfenster; übernimmt automatisch eine übersetzte Version,
  sobald eine für die aktuelle Sprache existiert), URTC GitHub (öffnet
  das Repository des Projekts in Ihrem Browser), Lizenz (die
  GPL-3.0-Lizenz dieses Tools, gelesen aus der eigenen `LICENSE`-Datei
  des Repositorys), und Über (Version und Autor).

**Beim Start** wird das Banner 5 Sekunden lang zentriert auf dem
Bildschirm angezeigt, bevor das Hauptfenster erscheint - es ist nicht
Teil des Hauptfensters selbst (deshalb ist das Fenster für alles, was es
tatsächlich tut, recht kompakt). Das Fenster-/Taskleisten-Symbol ist ein
kleines eigenständiges Design (`assets/urtc_icon.png`/`.ico`), nicht das
verkleinerte Banner - die vollständige Banner-Grafik hält sich bei
16-32px nicht gut.

**Sprache**: Englisch als Standard.
Wird über das Menü **Sprache** (in der Menüleiste oben im Fenster)
gewechselt statt über ein Dropdown im Hauptfenster - speichert sofort in
`urtc_config.json` (dieselbe Datei, die für die technischen
Hardware-Überschreibungen verwendet wird — die Spracheinstellung lebt
einfach neben diesen), angewendet beim nächsten Start. Übersetzungen
leben in reinen Textdateien unter `language/` (`english.lng`,
`spanish.lng`, `italian.lng`, `french.lng`, `german.lng`) als einfache
`SCHLÜSSEL=Wert`-Paare, eines pro Zeile - Zeilen, die mit `#` beginnen,
und leere Zeilen werden ignoriert, und ein wörtliches `\n` innerhalb
eines Werts wird zu einem echten Zeilenumbruch (verwendet von der
Handvoll mehrzeiliger Dialognachrichten). Direkt editierbar, wenn eine
Übersetzung korrigiert werden muss, oder als Ausgangspunkt für eine
andere Sprache (fügen Sie `language/<name>.lng` hinzu, fügen Sie
`("<name>", "Eigener Name")` zu `AVAILABLE_LANGUAGES` nahe dem Anfang
von `flasher_config.py` hinzu, und setzen Sie `"language": "<name>"` in
`urtc_config.json`). Ein fehlender Schlüssel aus einer Sprachdatei fällt
darauf zurück, den Namen dieses Schlüssels selbst anzuzeigen, statt
abzustürzen, und eine fehlende oder unlesbare Sprachdatei (fehlerhafte
Bearbeitung, falscher Dateiname) fällt für die gesamte Oberfläche auf
Englisch zurück - so oder so bleibt das Tool nutzbar, während die
Unstimmigkeit behoben wird.

Tkinter (das GUI-Toolkit) wird unter Windows mit Python ausgeliefert,
aber auf Distributionen der Debian/Ubuntu-Familie ist es ein separates
Betriebssystempaket:
```
sudo apt install python3-tk
```
(Fedora: `sudo dnf install python3-tkinter`. Arch: `sudo pacman -S
tk`.) `build_exe.sh` prüft dies selbst und teilt Ihnen mit, wenn es
fehlt, statt auf halbem Weg zu scheitern.

**Serielle Berechtigungen unter Linux:** wenn Sie den Seriell/SLCAN-Weg
verwenden und die Verbindung mit "Permission denied" fehlschlägt, muss
Ihr Benutzer in der Gruppe sein, die die seriellen Geräte besitzt
(`dialout` unter Debian/Ubuntu; variiert bei anderen Distributionen):
```
sudo usermod -a -G dialout $USER
```
Melden Sie sich ab und wieder an (die Gruppenmitgliedschaft wird beim
Login gelesen), dann versuchen Sie es erneut. Das Tool erkennt diesen
spezifischen Fehler und zeigt dieselbe Lösung in einem Dialog, aber es
lohnt sich, dies vorher zu wissen. SocketCAN hat dieses spezielle
Problem nicht — der Zugriff auf eine Schnittstelle vom Typ `can0` wird
nicht von der `dialout`-Gruppe kontrolliert — aber das Aktivieren der
Schnittstelle überhaupt (Schritt 1 oben) benötigt trotzdem `sudo`, da
dies eine Änderung der Netzwerkgerätekonfiguration ist.

Die Verwendung von `python -m pip`/`python3 -m pip` statt eines bloßen
`pip` umgeht ein häufiges Problem auf beiden Plattformen: das eigene
Wrapper-Skript von `pip` ist nicht immer im PATH, selbst direkt nach
einer erfolgreichen Installation, während `-m pip` das installierte
Modul direkt findet.

## 3. Wohin die Firmware-Dateien gehören

Dieses Tool erwartet einen `firmware/`-Ordner **innerhalb von
`tools/flasher/V1.0/`**, direkt neben `urtc_flasher.py`:

```
tools/flasher/V1.0/
├── assets/
│   ├── URTC_LOGO_FLASHER.svg      <- Banner-Quelle (Vektor)
│   └── urtc_banner.png            <- oben im Fenster angezeigt, aus dem .svg oben gerendert
├── firmware/
│   ├── URTC_v1_0_F303CC.bin      <- neue .bin-Dateien hier ablegen
│   └── URTC_v1_0_F303CC_old.bin  <- altere Versionen konnen auch hier aufbewahrt werden
├── logs/                          <- automatisch erstellt, eine Datei pro Sitzung
├── urtc_config.json               <- optional, nicht standardmaessig enthalten (siehe "Den HMAC-Schluessel aendern" unten)
├── urtc_flasher.py                <- Einstiegspunkt: CLI-Argumente, Splash-Screen, Hauptfenster-Einrichtung
├── flasher_config.py              <- Konfigurationsdatei-E/A, Sprachladen, Protokollkonstanten
├── flasher_transports.py          <- SLCAN, SocketCAN, MockCAN
├── flasher_swd_tools.py           <- STM32CubeProgrammer / pyOCD Wrapper
├── flasher_validation.py          <- Firmware-Dateivalidierung (.bin/.hex/.elf)
├── flasher_protocol.py            <- die CAN-OTA-Zustandsmaschine selbst
├── flasher_gui.py                 <- das Hauptfenster (FlasherGUI) und seine Menuleiste
├── requirements.txt
├── build_exe.bat                  <- eigenstandiger Build fuer Windows
├── build_exe.sh                   <- eigenstandiger Build fuer Linux
└── README.md
```

Dieses Tool ist aus Gründen der Lesbarkeit in die obigen Module nach
Zuständigkeit organisiert - es gibt keinen funktionalen Unterschied
zwischen separaten Dateien und einer großen Datei, und es gibt keine
monolithische Form, die synchron gehalten werden muss, wie es bei der
Firmware der Fall ist (dies ist ein PC-Tool, nichts, das auf die
Platine geflasht wird, also gibt es immer nur diese eine Form).

`assets/urtc_banner.png` ist optional - wenn sie fehlt, startet das
Tool einfach ohne Banner, statt zu scheitern. Sie wird über die native
PNG-Unterstützung von tkinter geladen (Tk 8.6+, das jede aktuelle
Python-Version mitbringt), nicht Pillow, sodass keine neue Abhängigkeit
hinzugefügt wird. Sowohl `build_exe.bat` als auch `build_exe.sh`
bündeln `assets/` bereits über PyInstallers `--add-data` in das
eigenständige Executable, sodass dies auf dieselbe Weise funktioniert,
egal ob Sie aus dem Quellcode oder aus einem erstellten Binary
ausführen.

Dies ist beabsichtigt: `firmware/` innerhalb von
`tools/flasher/V1.0/` statt an der Wurzel des Repositorys zu halten
bedeutet, dass der gesamte `tools/flasher/V1.0/`-Ordner in sich
geschlossen ist. Wenn Sie nur eine Platine flashen wollen — auf einem
Werkstatt-PC, von einem USB-Stick, wo auch immer — können Sie
`tools/flasher/V1.0/` allein kopieren, ohne sonst etwas aus dem
Repository, und es funktioniert trotzdem.

**Sie können mehr als eine `.bin` dort haben.** Jede Datei wird
geprüft und aufgelistet - das Tool greift nicht einfach, was es findet.
Beim Start (und jedes Mal, wenn Sie auf **Aktualisieren** klicken) wird
jede `.bin` in `firmware/` gegen denselben Plausibilitätstest geprüft,
den der Bootloader selbst auf ein frisches Image anwendet (ihre ersten
4 Bytes müssen wie ein echter anfänglicher Stack-Pointer für den
RAM dieses Chips aussehen, und ihre Größe muss in den Hauptslot passen).
Jede Datei erscheint in der Liste mit einem klaren ✓ oder ✗ und dem
Grund:

| Datei | Größe | Status |
|---|---|---|
| URTC_v1_0_F303CC.bin | 30.9 KB | ✓ sieht gültig aus |
| URTC_v1_0_F303CC_old.bin | 30.4 KB | ✓ sieht gültig aus |
| notes.txt.bin | 0.1 KB | ✗ erstes Wort sieht nicht wie ein gültiger Stack-Pointer aus |

- **Genau eine Datei besteht die Prüfung** → sie wird in dem Moment für
  Sie ausgewählt, in dem das Tool startet. Eine ungültige Datei allein
  im Ordner wird *nicht* automatisch ausgewählt, nur weil nichts anderes
  mit ihr konkurriert.
- **Mehr als eine gültige Datei** → nichts wird automatisch ausgewählt;
  wählen Sie die gewünschte aus der Liste.
- **Sie wählen trotzdem eine ungültig aussehende Datei** → das Tool
  bittet Sie zuerst um Bestätigung. Diese Prüfung existiert, um
  offensichtliche Fehler abzufangen (falsche Datei, abgebrochener
  Download, ein leerer Platzhalter) - sie kann nicht alles abfangen
  (eine beschädigte, aber plausible Datei, oder eine mit dem falschen
  Schlüssel signierte), wofür die eigene CRC32/HMAC-Prüfung des
  Bootloaders während der eigentlichen Übertragung da ist.
- **Nichts gefunden, oder Sie wollen eine Datei von ganz woanders** →
  verwenden Sie die Schaltfläche **Durchsuchen .bin...**, die unabhängig
  davon funktioniert, wo die Datei tatsächlich liegt (und in beiden
  Fällen dieselbe Validierungsprüfung ausführt).

**Optionale `<dateiname>.manifest.json`, neben einer Firmware-Datei**
(z. B. `URTC_v1_0_F303CC.bin.manifest.json`), fügt eine zusätzliche,
nicht blockierende Plausibilitätsprüfung hinzu: falls vorhanden, wird
ihr `sha256`-Feld direkt vor dem Flashen mit der tatsächlichen Datei
verglichen, wobei `version`/`build_date` zur Referenz daneben protokolliert
werden.

```json
{"version": "1.0", "build_date": "2026-07-23", "sha256": "e5a4918c..."}
```

Eine Abweichung wird als klare Warnung protokolliert, kein harter
Stopp - dies ist eine Komfortprüfung, um früh eine offensichtlich
falsche oder beschädigte Datei abzufangen, kein Ersatz für die eigene
HMAC-Verifizierung des Bootloaders während der eigentlichen
Übertragung, die so oder so die maßgebliche Prüfung bleibt.

Später einen neuen Build hinzufügen: einfach in `firmware/` ablegen und
auf **Aktualisieren** klicken - kein Neustart nötig.

## 4. Prüfen, was aktuell installiert ist

Wenn Sie unter Linux sind und SocketCAN verfügbar ist, sehen Sie oben
eine **Transport**-Auswahl - wählen Sie Seriell/SLCAN oder SocketCAN
vor dem Verbinden. Unter Windows erscheint diese Zeile überhaupt
nicht; Seriell/SLCAN ist die einzige Option.

Klicken Sie auf **Verbinden**, und das Tool fragt die Platine
automatisch, was sie aktuell ausführt (CAN-ID `0x7F8` → `0x7F9` - siehe
`docs/CANBUS.TXT`). Dies funktioniert, egal ob die Platine ihre
Anwendung normal ausführt *oder* im Bootloader sitzt, sodass Sie
keinen Reset auslösen müssen, nur um es herauszufinden. Klicken Sie
jederzeit danach auf **Abfragen**, um erneut zu prüfen (nützlich direkt
nach Abschluss eines Flashs, um zu bestätigen, dass die neue Version
tatsächlich übernommen wurde).

**Wenn der Bootloader selbst antwortet** (Platine sitzt im Bootloader,
führt ihre Anwendung nicht aus), meldet er auch seine eigene Version -
etwas Getrenntes von der Version der installierten Anwendung, verfolgt
über ihre eigene `BOOTLOADER_VERSION_MAJOR/MINOR/PATCH` in
`BOOTLOADER.C` und als zweiter Frame (`0x7FA`) direkt neben `0x7F9`
gesendet. Die laufende Anwendung sendet dies nie - sie hat keine
M�glichkeit, die Version eines aktuell geflashten Bootloaders zu
kennen, außer den Bootloader selbst zu fragen, sodass dies nur
auftaucht, wenn die Platine tatsächlich dort sitzt (direkt nach `0x7F0`,
oder bei einem frischen Start, bevor sie zur Anwendung springt).

Was Sie sehen werden:

- **`v1.0 (application, HardwareID 0x0303CC01)`** - normaler Fall,
  Anwendung läuft, alles stimmt überein.
- **`Bootloader running, no valid firmware currently installed,
  bootloader v1.0.0`** - die Platine steckt im Bootloader fest, ohne
  etwas, zu dem sie springen könnte (leerer Chip, oder jede Prüfung
  auf dem Hauptslot schlug fehl). Dies ist genau die Situation, für die
  dieses Tool existiert - flashen Sie sie. Die hier gezeigte
  Bootloader-Version ist die des Bootloaders selbst, ohne Bezug zu
  irgendeiner Anwendungsversion, die ihre Prüfungen nicht bestanden
  hat.
- **`⚠ HardwareID mismatch!`** in Rot angezeigt - etwas hat geantwortet,
  aber seine HardwareID stimmt nicht mit dem überein, was dieses Tool
  erwartet. Flashen Sie nicht, ohne vorher zu verstehen, warum; der
  Bootloader würde das Update sowieso ablehnen, aber eine Abweichung
  hier kann auch bedeuten, dass Sie komplett auf die falsche Platine
  zielen.
- **Keine Antwort** (Rot) - Platine reagiert nicht, falsche Bitrate,
  oder nicht tatsächlich verbunden. Prüfen Sie die physische
  Verbindung und, im SocketCAN-Weg, dass die Schnittstelle tatsächlich
  aktiv ist (`ip link show`).

**Erweiterungsplatine:** ein separates Dropdown und ein
Abfragen/Speichern-Paar, direkt unter der Versionsprüfung. Liest und
setzt, welche der 5 möglichen `CONN_EXPANSION`-Konfigurationen (keine,
oder eine der 4 geplanten Varianten - siehe `EXPANSION.TXT`) physisch
installiert ist, über CAN (`0x1A0`/`0x1A1`). Es gibt keine elektrische
M�glichkeit für die Platine, dies selbst zu erkennen, also muss es ihr
gesagt werden - dies lebt hier (nicht nur in `URTC Tester`), da es sich
um einen einmaligen Hardware-Konfigurationsschritt handelt, der am
natürlichsten zusammen mit einem Firmware-Update erfolgt. **Speichern**
fragt zuerst nach Bestätigung, da dies über Stromzyklen hinweg
bestehen bleibt, bis es explizit erneut geändert wird.

## 5. Flashen

1. **Verbinden**: wählen Sie Seriell/SLCAN oder SocketCAN (nur Linux),
   dann den Port/die Schnittstelle, dann klicken Sie auf Verbinden. Für
   Seriell/SLCAN öffnet dies den CAN-Kanal mit 500 kbit/s (die feste
   Busgeschwindigkeit von URTC); für SocketCAN wird erwartet, dass die
   Schnittstelle bereits auf dieser Bitrate ist (Schritt 1 oben) -
   dieses Tool stellt sie nicht ein. So oder so wird die aktuelle
   Version automatisch abgefragt - siehe Abschnitt 4 oben.
2. **Firmware auswählen**: wählen Sie aus der erkannten Liste, oder
   Durchsuchen - siehe Abschnitt 3 oben, um genau zu erfahren, wie
   Erkennung und Validierung funktionieren.
3. **Flashen**:
   - Lassen Sie "Platine führt aktuell die Anwendung aus" markiert,
     wenn die Platine eingeschaltet ist und normal läuft - das Tool
     sendet zuerst den magischen Payload-Auslöser `0x7F0`, der jeden
     Aktuator sicher herunterfährt, bevor es in den Bootloader
     zurücksetzt.
   - Deaktivieren Sie es, wenn die Platine bereits im Bootloader
     sitzt (direkt nach einem frischen JTAG-Flash, oder wenn die
     Versionsprüfung oben "no valid firmware currently installed"
     zeigte).
   - Klicken Sie auf **Firmware Flashen** und bestätigen Sie. Das
     Protokoll zeigt jeden Schritt des Protokolls; der Fortschrittsbalken
     verfolgt den Schreibfortschritt Seite für Seite während der
     Übertragung, dann den Kopierfortschritt während der abschließenden
     Backup-zu-Haupt-Kopie.

Wenn die Verifizierung an irgendeinem Punkt fehlschlägt (CRC32-, HMAC-,
oder HardwareID-Abweichung), wird der Hauptslot des Bootloaders nie
berührt - die Platine führt weiterhin die Firmware aus, die sie bereits
hatte. Es ist immer sicher, es einfach erneut zu versuchen.

## 6. Den kompletten Chip via SWD/JTAG programmieren (fortgeschritten)

Der Abschnitt "4. Program complete chip via SWD/JTAG" im Tool führt
einen vollständigen Inbetriebnahme-Flash durch - löscht den gesamten
Chip komplett, schreibt dann sowohl das Bootloader-Image
(`0x08000000`) als auch das Anwendungs-Image (`0x08008000`) neu. Dies
ist eine **andere Art von Operation** als die Abschnitte 1-5 oben:

|  | CAN-OTA-Update (Abschnitte 1-5) | Vollständiges SWD/JTAG-Chip (Abschnitt 6) |
|---|---|---|
| Selbstheilend bei Unterbrechung | Ja - der Golden-Image-Backup-Slot garantiert, dass die laufende Firmware überlebt | Nein - führt nichts aus, bis neu programmiert |
| Wiederherstellbar | Automatisch, keine Aktion nötig | Ja - einfach wieder verbinden und erneut über SWD flashen; der Debug-Port hängt nicht vom Flash-Inhalt ab. Nur eine echte permanente Sperre (Option-Byte RDP2) würde dies verhindern, und nichts in diesem Tool setzt Option-Bytes |
| Berührt den Bootloader | Nie | Ja, per Design |
| Benötigt | Einen USB-CAN-Adapter | Eine SWD/JTAG-Sonde (ST-Link oder ähnlich) |
| Typische Verwendung | Routinemäßige Firmware-Updates | Erste Inbetriebnahme auf einem leeren Chip, oder Wiederherstellung einer unbrauchbaren Platine |

**Benötigt eines von** (das Tool erkennt automatisch, was verfügbar
ist, und aktiviert nur das Gefundene):
- **pyOCD** - `pip install pyocd`. Frei, Open Source, keine separate
  Installation über das pip-Paket hinaus.
- **STM32CubeProgrammer** - das offizielle Tool von ST, separat von
  [st.com](https://www.st.com) installiert. Wenn Sie es bereits für
  andere STM32-Arbeiten haben, ist hier keine zusätzliche Installation
  nötig.

Beide werden als Kommandozeilen-Subprozesse gesteuert, nicht als
Python-Bibliotheken importiert - Sie sehen den genauen Befehl
protokolliert, bevor er ausgeführt wird.

**Dateiformate:** `.bin` (benötigt die feste Adresse, die dieses Tool
bereits kennt - Sie geben sie nicht ein) oder `.hex` (trägt seine
eigene Adresse, wird unverändert verwendet). Mischen ist in Ordnung -
Bootloader als `.hex` und Anwendung als `.bin`, oder umgekehrt, beides
funktioniert. Beide Dateiauswähler validieren die ausgewählte Datei
(plausible Größe für den Ziel-Slot, und - wo das Format eine
zuverlässige Prüfung erlaubt - ein plausibler anfänglicher
Stack-Pointer), bevor Sie fortfahren dürfen, genau wie es der
Firmware-Auswähler des CAN-Wegs bereits tat.

**Die Verbindung wird geprüft, bevor irgendetwas Destruktives
ausgeführt wird**, wobei **positive Beweise** für eine echte
Sonde/ein echtes Ziel erforderlich sind statt nur des Fehlens eines
Fehlers - der eigene Exit-Code von STM32CubeProgrammer ist allein kein
zuverlässiges Erfolgs-/Fehlersignal, daher läuft eine dedizierte
Verbindungsprüfung (`pyocd list --probes`, oder ein reines
Verbindungs-`-c port=SWD` für STM32CubeProgrammer), bevor der
Massenlösch-Schritt jemals läuft. Die Ausgabe jedes nachfolgenden
Befehls wird auch als zweite Schicht auf bekannten Fehlertext
durchsucht, falls der Exit-Code eines Tools allein in irgendeiner
anderen Situation ebenfalls nicht vertrauenswürdig wäre.

**Der Testlauf ist standardmäßig aktiviert.** Lassen Sie ihn beim
ersten Mal markiert und drücken Sie "Flash Complete Chip" - er druckt
die genauen Befehle ins Protokoll, ohne die Platine zu berühren. Lesen
Sie sie durch, bestätigen Sie, dass Pfade und Adressen richtig
aussehen, *dann* deaktivieren Sie den Testlauf und tun es wirklich.

**"Back up entire flash before erasing"** liest zuerst die gesamte
256KB-Flash-Region in eine `.bin`-Datei, über den eigenen
Speicher-Lesen-in-Datei-Befehl desselben Tools (`-r` für
STM32CubeProgrammer, `commander savemem` für pyOCD) - eine echte
Versicherung hier, denn anders als ein CAN-OTA-Update (das der
Golden-Image-Backup-Slot bereits schützt), hat ein vollständiges
Chip-Löschen kein anderes Rückgängigmachen. Standardmäßig
deaktiviert, da es 10-30s hinzufügt und auf einem
neuen/leeren Chip nicht nötig ist; lohnt sich zu markieren, bevor eine
Platine überschrieben wird, die bereits etwas ausführt. Wenn das Lesen
tatsächlich keine Datei erzeugt, wird das Löschen verweigert, statt
ohne das angeforderte Backup fortzufahren.

**Teststatus:** die obige Verbindungsprüfungslogik wurde gegen echte
STM32CubeProgrammer-Ausgabe verifiziert (sowohl ein echtes
Erfolgsverbindungs-Log als auch ein dokumentierter "No target
connected"-Fehler, beide aus dem eigenen Community-Forum von ST
stammend) und gegen das genaue Falsch-Erfolg-Szenario, das ein echter
Benutzer erlebt hat. Die vollständige
Lösch-/Programmier-/Verifizierungssequenz gegen einen echten ST-Link
und einen echten STM32F303CC wurde noch nicht end-to-end durchgeführt -
die Umgebung, die dies geschrieben hat, hat keinen USB-Zugriff.
Behandeln Sie einen ersten echten vollständigen Versuch mit
angemessener Vorsicht - zuerst eine Ersatz-/Testplatine, falls Sie eine
haben, und behalten Sie einen Rückfallplan (das eigene Flash-Tool von
STM32CubeIDE, oder `st-flash`) im Hinterkopf, falls etwas an Ihrer
spezifischen pyOCD-Version oder Sonde nicht dem entspricht, was hier
angenommen wird.

## 7. CLI-Modus (headless, keine GUI)

Für CI-Pipelines, Testbänke, oder Produktionslinien-Skripting, wo es
keinen Bildschirm gibt:

```
python3 urtc_flasher.py --cli --port /dev/ttyACM0 --file firmware.bin
```

```
usage: urtc_flasher.py --cli [-h] [--transport {serial,socketcan}] --port PORT
                             --file FILE [--no-trigger] [--force]
```

Exit-Codes: `0` Erfolg, `1` Protokoll-/Verbindungsfehler, `2` falsche
Argumente oder eine Firmware-Datei, die die Validierung nicht besteht
(übergeben Sie `--force`, um sie trotzdem zu flashen), `130` mit Strg+C
abgebrochen. Deckt nur den CAN-OTA-Update-Weg ab (Abschnitte 1-3) - der
vollständige SWD/JTAG-Chip-Weg ist vorerst absichtlich nur-GUI, angesichts
dessen, wie viel mehr auf dem Spiel steht, wenn ein skriptgesteuerter
Lauf eine falsche Datei-/Ziel-Kombination erwischt, ohne dass jemand
zusieht.

**`--transport mock`** führt die gesamte Update-Sequenz gegen einen
simulierten, speicherinternen Bootloader aus statt gegen eine echte
Platine - kein Adapter, kein Port, nichts Physisches beteiligt:

```
python3 urtc_flasher.py --cli --transport mock --file firmware.bin --no-trigger
```

Nützlich, um die eigene Logik dieses Tools zu testen (Wiederholungsverhalten,
Timeout-Handling, Exit-Codes) in einer CI-Pipeline oder bevor echte
Hardware berührt wird - nichts, das mit einer echten Platine spricht.
`--mock-fail 0x03` (oder jeder andere `VERIFY_FAIL_REASON_*`-Wert aus
`docs/CANBUS.TXT`) lässt das simulierte Update die Verifizierung
fehlschlagen statt erfolgreich zu sein, um den Fehlerpfad auf dieselbe
Weise zu testen.

## 8. Zuverlässigkeit während eines CAN-Updates, und Sitzungsprotokolle

Wenn das ACK einer Seite nicht innerhalb des normalen 3s-Fensters
während eines CAN-Updates ankommt, wiederholt das Tool das *Warten*
(nicht ein erneutes Senden der Seitendaten) bis zu zweimal mehr mit
einer kurzen zunehmenden Verzögerung, bevor es aufgibt, und erholt sich
so von einem verzögerten oder verlorenen ACK auf einem verrauschten
Bus, ohne dass die zugrunde liegenden Daten verloren gehen. Es sendet
absichtlich keine Seitendaten bei einem Timeout erneut - wenn die
ursprünglichen Daten tatsächlich gut angekommen sind und nur das ACK
verloren ging, würde ein erneutes Senden den Bootloader diese Bytes als
Anfang der *nächsten* Seite lesen lassen und die Übertragung
desynchronisieren. Jeder erneute Versuch prüft auch den eigenen
Heartbeat des Bootloaders (ungefähr einmal pro Sekunde gesendet) gegen
das, was der vollständige Empfang der aktuellen Seite implizieren
würde - wenn sie übereinstimmen, sagt das Protokoll dies, was ein
echter Beweis dafür ist, dass die Daten durchgekommen sind und nur das
ACK verloren ging, nicht nur ein längeres Warten und eine Hoffnung.

Jede Sitzung schreibt auch eine zeitgestempelte Protokolldatei nach
`tools/flasher/V1.0/logs/` (`urtc_flasher_YYYYMMDD_HHMMSS.log`),
unabhängig vom Bildschirmprotokoll - nützlich, um eine vollständige
Spur an denjenigen weiterzugeben, der die Firmware geschrieben hat,
falls im Feld etwas schiefgeht. Dieser Ordner wird automatisch erstellt
und ist sicher zu löschen; nichts liest alte Protokolle zurück.

## 9. Diagnose — Busaktivität, Bitrate, und Debug-Pakete

**Bitrate-Auswahl + Auto-Erkennung** (nur Seriell/SLCAN): der Bus von
URTC ist fest auf 500 kbit/s, was der Standard bleibt - dies ist für
einen falsch konfigurierten Adapter oder zur Fehlersuche bei einer
nicht standardmäßigen Platine gedacht. **Auto-Erkennung** probiert jede
Standard-SLCAN-Bitrate der Reihe nach gegen eine Versionsabfrage und
stoppt bei der ersten, die eine echte Antwort erhält; noch nicht
verbunden, wenn Sie darauf klicken. Die Bitrate von SocketCAN wird auf
Betriebssystemebene festgelegt (`ip link`), daher ist dieses Steuerelement
für diesen Transport deaktiviert - es gibt hier nichts zu versuchen.

**Busaktivität** ("Prüfen (2s)", neben Abfragen): zählt echte
Protokoll-Frames, die tatsächlich während eines festen 2-Sekunden-Fensters
auf dem verbundenen Transport gesehen wurden. Dies ist absichtlich
**nicht** dasselbe wie ein echter CAN-Bus-Auslastungsprozentsatz oder
die eigenen Fehlerzähler des Controllers (REC/TEC) - diese benötigen
eine Netlink-Abfrage (SocketCAN) oder adapterspezifische Erweiterungen
(SLCAN), die dieses Tool nicht auf standardmäßige, abhängigkeitsfreie
Weise erhalten kann. Was es gibt: ein echtes, direkt gemessenes Signal
für "spricht etwas auf diesem Bus, und ungefähr wie oft", auf beiden
Transporten. Speziell für SocketCAN zeigt es auch das 2-Sekunden-Delta
der eigenen Schnittstellenstatistiken von Linux
(`/sys/class/net/<iface>/statistics/`) - grundlegende
rx/tx/error/drop-Zähler, die jede Schnittstelle bereitstellt, als
einfache Dateien gelesen, keine zusätzliche Abhängigkeit. Das Verbinden
über SocketCAN liest auch `/sys/class/net/<iface>/carrier` - eine
einfache 0/1-Datei, die jede Linux-Schnittstelle bereitstellt. Wenn ein
CAN-Controller in Bus-Off geht, ruft der Kernel-Treiber
`netif_carrier_off()` auf, daher ist "kein Träger" hier ein echter
Beweis für ein Bus-Off oder eine ähnlich tote Verbindung, als Warnung
mit dem genauen Wiederherstellungsbefehl protokolliert (`sudo ip link
set <iface> down && sudo ip link set <iface> up type can bitrate
500000 restart-ms 100`). Dieses Tool führt diesen Befehl nicht selbst
aus - ein echtes Bus-Off zu bereinigen erfordert, die Schnittstelle auf
Kernel-Ebene herunter- und wieder hochzufahren, was Root erfordert und
als Änderung der Systemnetzwerkkonfiguration zählt, nichts, das
stillschweigend in Ihrem Namen getan werden sollte.

**Debug-Paket exportieren** (über dem Protokoll): speichert eine
`.zip`-Datei mit dem aktuellen Bildschirmprotokoll, grundlegender
Systemdiagnose (Betriebssystem, Python-Version, welche Tools gefunden
wurden, aktueller Transport/Port/Bitrate), und der aktuell ausgewählten
CAN-Firmware-Datei - nützlich, um ein vollständiges Bild an denjenigen
weiterzugeben, der die Firmware geschrieben hat, falls im Feld etwas
schiefgeht, statt das Protokoll
von Hand zu kopieren.

## 10. SWD/JTAG — Dateiformate, Slot-Verifizierung, und Sondenauswahl

**Dateiformate**: die Bootloader-/Anwendungsauswähler des
SWD-Abschnitts akzeptieren `.bin`, `.hex`, und `.elf`/`.axf`. ELF/AXF
wird mit einer kleinen Menge handgeschriebenen
Struct-Unpackings analysiert (nur ELF-Header + Programm-Header - keine
Symbole, keine Section-Header), absichtlich ohne `pyelftools` zu
verwenden: dieses Projekt bleibt bei null Abhängigkeiten außerhalb der
Standardbibliothek, und vollständiges ELF-Parsing ist mehr, als diese
spezifische Plausibilitätsprüfung benötigt. Verifiziert gegen die
eigenen kompilierten `BOOTLOADER.elf`/`APP.elf` dieses Projekts - beide
validieren korrekt an ihren echten Ladeadressen
(`0x08000000`/`0x08008000`), nicht nur an synthetischen Testdateien.
Nur 32-Bit-ARM-Little-Endian, was alles ist, was ein Cortex-M-Ziel
jemals ist. Die deklarierte Größe einer `.hex`-Datei ist die
tatsächliche belegte Byteanzahl, nicht die Adressspanne von ihrem
niedrigsten bis höchsten Record - daher validiert eine spärliche Datei
(ein kleiner Block echter Firmware plus ein entfernter, separater
Block von Option-Bytes oder Kalibrierungsdaten, den manche
STM32-Toolchains in einen Export bündeln) anhand ihres tatsächlichen
Inhalts statt der Lücke dazwischen. Ein rohes Firmware-Image unter
einer anderen Erweiterung als `.bin` (`.img`, `.rom`, oder gar keine
Erweiterung - wählbar über die "Alle Dateien"-Option des
Dateiauswählers) erhält seine Basisadresse davon, in welchen Slot Sie
es laden, genauso wie es eine `.bin` täte.

**Bootloader-/Anwendungs-Slot-Verifizierung**: die Dateiauswähler
verifizieren, dass jedes Image für den Slot bestimmt ist, in den es
gesetzt wird, nicht nur, dass es wie *irgendeine* Art gültiger Firmware
aussieht. Ein Bootloader-Image und ein Anwendungs-Image haben einen
gleich plausiblen Stack-Pointer - gleicher Chip, gleicher RAM - daher
kann diese Prüfung allein sie nicht unterscheiden, wenn eines im Slot
des anderen landet. Was es kann: der **Reset-Handler** eines gelinkten
Images ist eine echte, absolute Adresse, die zur Linkzeit fest
eingebettet wird, und zeigt immer nur in die Region, für die es
tatsächlich gelinkt wurde. Verifiziert gegen die eigenen echten
kompilierten `BOOTLOADER.bin`/`APP.bin` dieses Projekts: ihre
Reset-Handler sind `0x080030F1` bzw. `0x0800C725`, jeweils korrekt
innerhalb des Adressbereichs ihres eigenen Slots und außerhalb des des
anderen - daher wird das Setzen eines der beiden in den falschen Slot
erkannt und blockiert, nicht stillschweigend akzeptiert. Dieselbe
Logik gilt für `.hex`/`.elf`, stattdessen gegen ihre eigene eingebettete
Ladeadresse geprüft.

**Option-Bytes prüfen** (Abschnitt 4, nur STM32CubeProgrammer - pyOCD
exponiert dies nicht auf dieselbe Weise über CLI): ein
schreibgeschützter `-ob displ`-Dump, kein Löschen/Schreiben. Markiert
die RDP-Stufe mit derselben Sorgfalt, die dieses gesamte Tool rund um
das SWD-Risiko walten lässt:
- **RDP0** - kein Schutz, normal für eine Entwicklungsplatine.
- **RDP1** - über den Read Unprotect von CubeProgrammer reversibel,
  aber das löscht den Chip komplett als Teil der Entfernung - nichts,
  was dieses Tool automatisch für Sie tut.
- **RDP2** - die einzige wirklich **permanente** Sperre in diesem
  ganzen Projekt. Anders als jedes andere oben dokumentierte Risiko
  (alle über SWD wiederherstellbar), deaktiviert RDP2 den Debug-Port
  für immer, per eigenem Design von ST. Diese Prüfung existiert, um
  dies vor einer Chip-Komplett-Operation zu erkennen, nicht danach.

**Sondenauswahl** (Abschnitt 4): wenn mehr als ein ST-Link/Sonde
gleichzeitig verbunden ist, erfordert jeder Befehl, dass eine
explizit aus dem Sonden-Dropdown gewählt wird - kein "welche auch immer
das Betriebssystem zuerst aufzählt". Mit genau einer verbundenen Sonde
wird sie automatisch ausgewählt; bei null oder mehreren, drücken Sie
Aktualisieren und wählen Sie. Dies gilt sowohl für den
Chip-Komplett-Flash als auch für die Option-Bytes-Prüfung, da beide
nah genug am Destruktiven sind, dass das Erraten der falschen Platine
auf einer Werkbank mit mehreren Geräten ein echtes Risiko darstellt.

**pyOCD-Schreibvorgänge werden mit einem expliziten Rücklesen
verifiziert**, nicht nur dem Exit-Code vertraut. Der eigene
`flash`-Befehl von pyOCD überspringt das Neuschreiben von Seiten, die
bereits übereinstimmen (eine Geschwindigkeitsoptimierung, kein
Verifizierungsbericht), daher fügt dieses Tool einen
`commander compare`-Schritt gegen beide Images nach dem Schreiben
hinzu - eine echte Byte-für-Byte-Prüfung, die dem entspricht, was das
`-v`-Flag von STM32CubeProgrammer bereits tut. Nur für `.bin`: `compare`
prüft den Flash-Inhalt gegen die rohen Bytes der Datei, was mit der
eigenen Kodierung einer `.hex`/`.elf`-Datei auch nach einem
erfolgreichen Flash nicht korrekt übereinstimmen würde, daher
überspringen diese 2 Formate diesen spezifischen Schritt und
verlassen sich stattdessen auf die eigene interne
Schreibzeit-Verifizierung von pyOCD.

## 11. Übertragungstelemetrie und Verifizierungsfehler-Details

**Übertragungstelemetrie**: das Protokoll zeigt die effektiven KB/s und
die verstrichene Zeit pro Seite während eines CAN-Updates, plus eine
Zusammenfassungszeile am Ende (Gesamtzeit, durchschnittliche KB/s, wie
viele Seiten-ACK-Wiederholungen aufgetreten sind). Rein informativ -
ändert das Flash-Verhalten nicht, macht es nur leichter, auf einen
Blick "das ist einfach langsam" von "da stimmt wirklich etwas nicht"
zu unterscheiden.

**Spezifische Verifizierungsfehler-Gründe**: wenn die Verifizierung
während eines CAN-Updates fehlschlägt, sendet `BOOTLOADER.C` ein
Grund-Byte zusammen mit Status `0x05` (Verifizierung fehlgeschlagen) -
unvollständige Übertragung, CRC32-Abweichung, HMAC-Abweichung, oder
HardwareID-Abweichung, statt dass jeder Fehler gleich aussieht. Siehe
`docs/CANBUS.TXT` für das genaue Frame-Format (`0x7F5`, DLC 2 für
diesen spezifischen Status). Dieses Tool und der Bootloader stimmen bei
diesem Frame-Format überein, also flashen Sie beide zusammen, wenn Sie
einen benutzerdefinierten Bootloader mit einer anderen Protokollversion
bauen.

## 12. Optionales F-RAM-Löschen vor dem Flashen

Abschnitt 3 hat ein Kontrollkästchen, **"Auch das persistente F-RAM vor
dem Flashen löschen"** - standardmäßig deaktiviert. Wenn markiert,
sendet es den Lösch-Befehl mit magischem Payload (`0x192` - siehe
`docs/CANBUS.TXT`) an das persistente FM24CL64B-F-RAM der Platine,
bevor die Update-Sequenz beginnt, und löscht dabei jeden gespeicherten
Werkzeugparameterzustand.

**Für ein normales Update nicht erforderlich.** Eine Versionsabweichung
im eigenen Layout des gespeicherten Datensatzes wird bereits beim
nächsten Start sicher erkannt und ignoriert (siehe den
Parameter-Persistenz-Abschnitt von
`src/F303-master/V1.0/README.md`) - dieses Kontrollkästchen existiert
für einen wirklich sauberen Neuanfang, nicht weil das Überspringen
etwas kaputt lassen würde.

**Funktioniert nur, während die Anwendung läuft** - der Bootloader
selbst behandelt `0x192` überhaupt nicht, nur `STM32F303CC.C` tut es.
Dieses Kontrollkästchen wird stillschweigend übersprungen (mit einer
Protokollzeile, die erklärt, warum), wenn das Kontrollkästchen "Platine
führt aktuell die Anwendung aus" darüber nicht markiert ist, da in
diesem Fall angenommen wird, dass die Platine bereits im Bootloader
sitzt.

**Eine fehlende Bestätigung stoppt den Flash nicht.** Wenn der eigene
Bestätigungs-Frame des Lösch-Befehls nicht innerhalb von 2 Sekunden
zurückkommt, wird dies als Warnung protokolliert, und das eigentliche
Firmware-Update wird trotzdem fortgesetzt - Löschen ist ein
sekundärer, optionaler Schritt neben dem eigentlichen Zweck dieses
Tools, nichts, das ein ansonsten erfolgreiches Update wegen des
Fehlens seines eigenen Bestätigungs-Frames abbrechen sollte. Prüfen
Sie den F-RAM-Status separat (die eigene
Zustand-Abfragen-Schaltfläche von `URTC Tester`), falls Ihnen das
wichtig ist.

## Den HMAC-Schlüssel / die HardwareID ändern

Der gemeinsame Signierschlüssel lebt an 2 Stellen, die immer
übereinstimmen müssen: das `HMAC_KEY`-Array von `BOOTLOADER.C`, und
die `HMAC_KEY`-Konstante dieses Tools nahe dem Anfang von
`urtc_flasher.py`. Wenn Sie eine ändern, ändern Sie die andere und
bauen/flashen Sie den Bootloader neu, bevor Sie versuchen, irgendetwas
mit dem neuen Schlüssel zu signieren - ein Image, das mit einem
Schlüssel signiert wurde, den der Bootloader nicht hat, wird die
Verifizierung immer sicher fehlschlagen lassen und den Hauptslot
unberührt lassen.

**Oder ersetzen Sie all dies, ohne das Skript zu berühren:** eine
optionale `urtc_config.json` neben `firmware/` kann den Signierschlüssel,
die HardwareID, und die Speicherkarten-Werte setzen - nützlich für eine
andere Platinenrevision, einen rotierten Schlüssel, oder (für die
Speicherkarten-Felder) die Anpassung dieses Tools an eine andere
Chip-Variante oder ein anderes Partitionsschema, ohne eine neue
Skriptversion pro Bereitstellung zu benötigen:
```json
{
  "hardware_id": "0x0303CC01",
  "hmac_key_hex": "555254432D4859445241...",
  "app_max_size": 114688,
  "bootloader_max_size": 32768,
  "flash_page_size": 2048,
  "bootloader_flash_addr": "0x08000000",
  "app_flash_addr": "0x08008000"
}
```
Jedes Feld ist optional - ersetzen Sie nur, was sich tatsächlich
ändert. Eine fehlende Datei fällt stillschweigend auf die kompilierten
Standardwerte zurück; eine vorhandene, aber defekte Datei protokolliert
eine Warnung und fällt ebenfalls auf diese Werte zurück, statt das Tool
wegen eines Tippfehlers abstürzen zu lassen. Welche Quelle aktiv ist,
wird beim Start protokolliert, sodass immer sichtbar ist, welche Werte
eine bestimmte Sitzung tatsächlich verwendet hat. `hardware_id`
akzeptiert entweder eine JSON-Zeichenkette (`"0x0303CC01"`) oder eine
einfache JSON-Zahl (`50580689`) - je nachdem, was natürlicher ist, je
nachdem, wie die Datei erzeugt wird. `app_max_size`,
`bootloader_max_size`, `flash_page_size`, `bootloader_flash_addr`, und
`app_flash_addr` sind hier ebenfalls ersetzbar, neben dem
Signierschlüssel und der HardwareID oben - nützlich, falls dieses Tool
jemals an eine andere Chip-Variante oder ein anderes Partitionsschema
angepasst wird.
