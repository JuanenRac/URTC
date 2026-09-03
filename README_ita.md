<p align="center">
  <img src="images/URTC_LOGO.svg" alt="URTC Logo" width="100%">
</p>

# 🚀 URTC — Universal Robot Tool Controller (v0.2)

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  🇮🇹 <b>Italiano</b> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>


<p align="left">
  <img src="https://img.shields.io/badge/Licenza-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Hardware-CERN%20OHL--S-orange.svg" alt="CERN OHL-S">
  <img src="https://img.shields.io/badge/Linguaggio-C-00599C.svg" alt="C">
  <img src="https://img.shields.io/badge/Piattaforma-STM32F303-003551.svg" alt="STM32">
  <img src="https://img.shields.io/badge/Bus-CAN-yellow.svg" alt="CAN">
</p>


> **⚠️ Avviso di sicurezza:** questa scheda pilota un **diodo laser da incisione da 10W** e più stadi di riscaldamento (cartuccia per saldatore T12, hotend per stampante 3D). Costruirla e usarla significa lavorare con apparecchiature che possono causare **ustioni, incendi o danni agli occhi** se assemblate o utilizzate senza le adeguate misure di sicurezza (occhiali protettivi con classificazione adatta alla lunghezza d'onda del diodo, protezione termica, un interruttore di alimentazione accessibile). Questo è un progetto hobbistico/maker condiviso così com'è - costruiscilo e usalo a tuo rischio, e non saltare le pratiche di sicurezza di base solo perché il firmware ha dei watchdog.

Ciao a tutti! Volevo condividere un progetto a cui sto lavorando chiamato URTC (Universal Robot Tool Controller). È una scheda di controllo monolitica e altamente integrata, progettata specificamente per espandere le capacità di bracci robotici e impianti di automazione, il che la rende perfetta per piattaforme come PAROL6 e Faze4 - due bracci robotici open-source progettati e sviluppati da [Source-Robotics](https://source-robotics.com/) ([GitHub](https://github.com/Source-Robotics)).

**URTC è un progetto indipendente e non ufficiale.** Non è sviluppato né approvato da Source-Robotics - è un controller di tool-head compatibile, costruito per funzionare bene con PAROL6 e Faze4, e la stessa architettura basata su CAN è aperta ad essere adattata anche ad altre piattaforme di bracci robotici.

Qui trovi la descrizione completa di cosa sia, cosa faccia, e l'ecosistema hardware che attualmente gestisce.

**Stato: 🚧 Progetto in evoluzione attiva — nessuna Release ancora.** URTC è in sviluppo continuo e attivo su entrambi i fronti contemporaneamente: firmware (nuovi profili strumento, l'ecosistema di espansione slave, cambi di protocollo) e hardware (schematico e distinta base ancora in fase di finalizzazione, nessuna scheda popolata esiste ancora). Poiché entrambi i lati si muovono insieme, ciò che si trova in questo repository in un dato momento è un'istantanea del lavoro in corso, non un prodotto stabile e versionato - nomi di file, struttura delle cartelle, numero di strumenti, e documentazione possono ancora tutti cambiare man mano che il progetto si assesta. Una volta che sia firmware che hardware raggiungeranno uno stato genuinamente stabile e verificato su hardware reale, verrà taggata una vera **Release** che raggrupperà tutto insieme (firmware, bootloader, strumenti PC, file di progettazione hardware, e documentazione) come un'istantanea coerente e congelata. Fino ad allora, considera `main` come l'obiettivo in continuo movimento che è.

---

## ⚙️ Cos'è URTC?

URTC è una scheda di controllo tutto-in-uno e compatta, alimentata da un microcontrollore STM32 (STM32F303CCT6, LQFP48). Comunica con il controller principale del robot via bus CAN, permettendo l'esecuzione in tempo reale e a bassa latenza di compiti complessi direttamente al tool-head o all'asse. Include un display OLED integrato per diagnostica istantanea - splash di avvio animato, icone animate per ogni strumento, telemetria dal vivo su un pannello a due toni - un singolo LED RGB di stato più un anello di LED RGB indirizzabili per l'illuminazione della fotocamera, un connettore di espansione a 20 pin per schede aggiuntive, una F-RAM integrata che mantiene i setpoint dello strumento attivo attraverso una perdita di alimentazione, e stadi di alimentazione dedicati, analogici e ad alta corrente.

## 🛠️ Architettura Scalabile e Matrice degli Strumenti

Il punto di forza principale di URTC è la sua estrema versatilità. Invece di sostituire l'elettronica per ogni lavoro diverso, la scheda presenta un'architettura a matrice scalabile:

* **Schema di identificazione a 32 indirizzi:** l'hardware e il protocollo di comunicazione sono progettati per identificare fino a 32 strumenti o end-effector diversi direttamente sulla testa del robot, tramite una matrice ID a jumper di saldatura a 5 bit (ID0-ID4). Di queste 32 letture, 31 mappano direttamente su un profilo strumento; la 32ª (tutti e 5 i jumper installati, `11111`) è riservata invece come indirizzo di "configurazione libera" - vedi sotto.
* **25 profili automatizzati plug-and-play:** il firmware gestisce nativamente 25 profili strumento - la scheda legge l'identità fisica del tool-head e configura gli stadi di alimentazione, i sensori, e la commutazione logica senza soluzione di continuità, senza necessità di un ri-flash completo. 6 indirizzi in più rimangono liberi nello schema esistente per futuri profili strumento.
* **Configurazione libera dello strumento:** la lettura riservata dei jumper `11111` non seleziona uno strumento fisso - dice alla scheda di ricercare quale strumento usare da un registro nella propria F-RAM persistente, impostato in anticipo via CAN (tramite `URTC Flasher`). Utile per una scheda che necessita di essere riprogrammata su uno strumento diverso senza risaldare fisicamente i jumper. Vedi `docs/EEPROM.TXT` sezione 5 per il meccanismo completo.

## 🔌 Flessibilità Hardware e Supporto Motori

Per gestire una varietà così ampia di applicazioni, l'hardware URTC è pienamente equipaggiato per controllare:

* **Motori passo-passo NEMA:** NEMA 8, 11, 14, e 17 funzionano direttamente sul TMC2209 integrato, così come NEMA 23 e 34 - fino a **2,0A** su ciascuno di essi tramite lo stadio driver della scheda principale. Per NEMA 23/34 alla loro piena coppia nominale, un TMC5160 sul connettore di espansione (vedi sotto) supporta fino a **10A**, scalati in corrente dai MOSFET/resistenza di sense esterni scelti per quella scheda - il limite integrato di 2,0A non si applica una volta che un motore è stato spostato sul driver di espansione.
* **Motori BLDC trifase / gimbal** per movimento ad alta precisione.
* **Motori con sensori Hall e tachimetri** per controllo a circuito chiuso.
* **Ingressi dedicati** per sensori di prossimità ottici riflettenti come il TCRT5000, più un ingresso generico attivo-basso per finecorsa/limit-switch condiviso tra quattro profili strumento.

## 🧩 Connettore di Espansione

Un header a 20 pin, separato dai connettori specifici per strumento, per schede aggiuntive che necessitano più di quanto un dato profilo strumento da solo esponga - un asse a passo aggiuntivo (TMC2209 o TMC5160), una seconda scheda sensore, cose di questo tipo.

| Pin | Segnale |
|---|---|
| 4 | 24V |
| 1 | 3.3V |
| 1 | 5V |
| 3 | GND |
| 2 | I2C bit-banged (SCL/SDA) — proprio bus separato, distinto dall'I2C2 hardware di OLED/F-RAM |
| 3 | STEP/DIR/EN — universale per entrambi i chip driver sotto |
| 4 | SPI bit-banged (CS/SCK/MISO/MOSI) — per l'interfaccia di configurazione/diagnostica di un TMC5160, o qualsiasi altro chip configurabile via SPI |
| 1 | GPIO generico (ingresso di interrupt capace di EXTI se un futuro add-on necessita una risposta sensore rapida, es. un finecorsa) |
| 1 | TMC5160 DIAG0 (linea di diagnostica stallo/guasto, interrogata via `0x182`/`0x183`) |

20 pin totali.

**Due bus I2C separati, di proposito:** OLED/F-RAM usano l'unico periferico hardware I2C utilizzabile di questo chip (I2C2, su PA9/PA10); il connettore di espansione ottiene il proprio bus I2C bit-banged, indipendente (PB10/PB11 - le uniche altre coppie di pin capaci di I2C su questo chip erano già impegnate per altre funzioni, quindi il bit-banging era il modo per dare a questo connettore un proprio bus senza un conflitto hardware). Qualsiasi cosa collegata all'header di espansione - un ADC/DAC I2C, un port expander, qualsiasi cosa una data scheda add-on necessiti - condivide questo bus bit-banged con qualsiasi altro dispositivo I2C lato espansione, ma non può allungare il clock (clock stretching) né altrimenti interferire con la temporizzazione propria dell'OLED sul suo bus hardware separato I2C2.

**Un TMC2209 o un TMC5160, non necessariamente entrambi.** Entrambi i chip usano la stessa interfaccia STEP/DIR/EN per il movimento vero e proprio, quindi quella parte è universale. Dove differiscono è nella configurazione/diagnostica: un TMC2209 usa la propria UART a filo singolo per questo, mentre un TMC5160 usa SPI - e poiché i due sono mutuamente esclusivi su una data scheda di espansione, i 4 pin SPI fungono anche naturalmente da sede per la linea UART singola di un TMC2209, invece di richiedere un ulteriore pin dedicato che nessuno userebbe contemporaneamente al bus SPI. Il bus SPI bit-banged parla esattamente il protocollo che un TMC5160 si aspetta (SPI Modalità 3, MSB per primo, CS mantenuto basso per l'intera transazione - vedi `0x180`/`0x181` in `docs/CANBUS.TXT` per il comando generico di byte-passthrough che lo pilota) invece che questo firmware debba conoscere il layout specifico dei registri di quel chip. Anche la linea DIAG0 di stallo/guasto di un TMC5160 è cablata (`0x182`/`0x183`) - riutilizza uno dei due pin GPIO generici, che erano già stati destinati esattamente a questo tipo di ingresso rapido guidato da interrupt.

Il dettaglio completo pin per pin - quale pin dell'MCU sostiene quale segnale, e il ragionamento dietro un paio di vincoli di layout che il package a 48 pin di questo chip ha - vive in `docs/PINOUT_CONNECTORS.TXT` e `src/F303-master/README.md`.

### Le 6 varianti di scheda di espansione

4 delle 6 varianti di scheda di espansione portano un driver per motore passo-passo - o un TMC2209 (fino a 2A/bobina, MOSFET di potenza integrati) o un TMC5160A (fino a 10A+/bobina, necessita 8 MOSFET di potenza esterni che il driver stesso non include). Indipendentemente da questa scelta di driver, una scheda con driver è o **basic** (driver + connettori soltanto, nessun MCU - STEP/DIR/EN instradati direttamente dalla scheda principale) o **advanced** (aggiunge un secondo microcontrollore, STM32F303CBT6, più 2 chip sensore locali - un ADC 16-bit ADS1115 e una termocamera della famiglia MLX9064x - e generazione locale di PWM per strumenti la cui temporizzazione necessita di essere generata direttamente sul tool-head anziché instradata via cavo). 2×2 combinazioni, più altre 2 schede basic solo-sensore (ADS1115 o MLX9064x, cablate direttamente allo STM32F303CC della scheda principale stessa, nessun driver e nessun MCU slave) per uno strumento che necessita solo di uno di quei 2 chip e nient'altro che una scheda advanced porta anch'essa - 6 schede in totale - vedi `BOM/BOM_EXPANSION_*.TXT` (6 file), `docs/EXPANSION.TXT`, e `docs/PINOUT_SLAVE.txt`.

Il proprio STM32F303CBT6 della variante advanced parla con la scheda principale attraverso il bus I2C bit-banged esistente del connettore di espansione sopra - scheda principale come master, chip slave che risponde come vero slave hardware I2C - e pilota il proprio secondo bus I2C, locale-solo, per i 2 chip sensore. Ha un proprio bootloader e firmware applicativo, aggiornato allo stesso modo della scheda principale (CAN-OTA da `URTC Flasher`), semplicemente ritrasmesso attraverso quel collegamento I2C piuttosto che raggiungendo direttamente il chip slave. Vedi `src/F303-slave/README.md` e `src/F303-slave/boot/README.md` per il dettaglio tecnico completo.

## 💾 Persistenza dei Parametri

Una F-RAM FM24CL64B integrata (64Kbit, I2C) mantiene un'istantanea aggiornata periodicamente dei setpoint dello strumento attivo e delle impostazioni globali di LED/OLED, così una perdita improvvisa di alimentazione non lascia "cosa stava facendo questa scheda" tanto ignoto quanto lo era stata la perdita stessa, imprevista. Condivide il bus hardware I2C2 dell'OLED anziché ottenerne uno proprio - questo MCU ha un solo periferico hardware I2C utilizzabile per questo scopo, già impegnato dall'OLED (vedi `src/F303-master/README.md` sezione 6 per il ragionamento completo).

**Lo stato recuperato è interrogabile, mai applicato automaticamente a nulla di pericoloso.** All'avvio, qualsiasi cosa fosse stata salvata diventa leggibile via CAN (`0x190`/`0x191`) - ma un setpoint del riscaldatore, la potenza del laser, o un comando motore non vengono mai riarmati silenziosamente da soli. Solo le impostazioni sicure e passive (colori LED, modalità OLED) vengono ripristinate direttamente. Reinviare deliberatamente un setpoint dopo aver effettivamente esaminato cosa sia successo resta una decisione del controller master, non qualcosa che questa scheda decide da sola nell'istante in cui torna l'alimentazione.

## 💼 Catalogo Strumenti Automatizzati Nativamente (25 Profili Firmware)

Tramite la sua logica di commutazione dinamica, il firmware gestisce nativamente i seguenti tool-head:

1. **Stazione Saldante (T12):** controllo di temperatura PID preciso usando feedback ADC diretto per gestire punte saldanti T12 standard, più un alimentatore motorizzato di filo che spinge il filo di stagno nel giunto (condivide `CONN_MOT` e il proprio protocollo stepper con gli strumenti a solo movimento sotto - cede il proprio ingresso finecorsa generico per fargli spazio). [Config jumper/cablaggio →](images/TOOL_SOLDERING_IRON.png)
2. **Dispenser di Pasta Saldante SMT:** controllo di alimentazione millimetrico per deposizione precisa di pasta saldante su PCB. [Config jumper/cablaggio →](images/TOOL_PASTE_DISPENSER.png)
3. **Dispenser di Pasta Termica / Liquido:** gestione della fluidità per paste ad alta viscosità o adesivi liquidi. [Config jumper/cablaggio →](images/TOOL_LIQUID_DISPENSER.png)
4. **Cacciavite Elettrico Intelligente:** controllo di rotazione e arresto basato su limiti di coppia o finecorsa. [Config jumper/cablaggio →](images/TOOL_SCREWDRIVER.png)
5. **Pinza a Vuoto / Pneumatica:** controllo della pompa a vuoto e lettura del livello di pressione per operazioni sicure di Pick-and-Place. [Config jumper/cablaggio →](images/TOOL_VACUUM_PICKUP.png)
6. **Trapano (BL4260):** controllo di velocità PWM, inversione di direzione, e frenata elettrica dinamica con letture RPM in tempo reale, sulla propria linea dedicata di enable/freno, indipendente dall'enable del driver stepper. Ingresso finecorsa generico disponibile. [Config jumper/cablaggio →](images/TOOL_DRILL.png)
7. **Pinza Gimbal:** manipolazione ad alta sensibilità usando motori brushless gimbal trifase. [Config jumper/cablaggio →](images/TOOL_GRIPPER_GIMBAL.png)
8. **Pinza NEMA:** forza di serraggio robusta controllata tramite un motore stepper heavy-duty. [Config jumper/cablaggio →](images/TOOL_GRIPPER_NEMA.png)
9. **Sistema AOI (Ispezione Ottica Automatizzata):** controllo stroboscopico sincrono dell'array LED di illuminazione per la cattura della fotocamera di visione artificiale. Ingresso finecorsa generico disponibile. [Config jumper/cablaggio →](images/TOOL_AOI_INSPECTION.png)
10. **Diodo Laser da Incisione (10W ottici):** modulazione PWM della potenza del fascio con un loop hardware di sicurezza (watchdog CAN) che si blocca se la comunicazione con l'host viene persa. Ingresso finecorsa generico disponibile. [Config jumper/cablaggio →](images/TOOL_LASER_ENGRAVER.png)
11. **Hotend di Stampa 3D:** controllo PID della cartuccia riscaldante, lettura del termistore NTC, controllo dell'estrusore, e una ventola dedicata di raffreddamento layer controllata via PWM a 25kHz (4 fili, feedback tachimetrico, watchdog di comunicazione proprio) - tutto integrato in un unico blocco. [Config jumper/cablaggio →](images/TOOL_3D_PRINTER.png)
12. **Sonda per Scanner 3D:** ingresso hardware a interrupt ultra-rapido (EXTI) con priorità assoluta per digitalizzazione della superficie in tempo reale e rilevamento urti senza ritardo. Copre anche il tastatore metrologico - stesso percorso hardware, una sonda fisica diversa sullo stesso tool-head. [Config jumper/cablaggio →](images/TOOL_SCAN_PROBE.png)
13. **Testa Pick & Place SMT:** asse rotativo A per il corretto allineamento dei pad, sulla stessa interfaccia stepper dei dispenser pasta/liquido e di entrambe le pinze sopra. [Config jumper/cablaggio →](images/TOOL_SMT_PICKPLACE.png)
14. **Elettromagnete Heavy-Duty:** controllo di presa on/off per parti ferromagnetiche, dall'uscita del riscaldatore T12 riutilizzata come driver GPIO generico. [Config jumper/cablaggio →](images/TOOL_ELECTROMAGNET.png)
15. **Testa Saldatrice a Punti:** impulsi di saldatura con precisione al millisecondo per strisce di nickel di battery-pack, con un sensore di contatto superficiale che regola l'impulso. [Config jumper/cablaggio →](images/TOOL_SPOT_WELDER.png)
16. **Aerografo per Coating Conforme:** controllo dello spray di rivestimento protettivo per PCB finiti - la valvola dello spray e il proprio sensore vivono sulla mainboard propria del robot, fuori dallo scopo proprio di questa scheda. [Config jumper/cablaggio →](images/TOOL_CONFORMAL_COATING.png)
17. **Pinza a Vuoto per Grande Formato:** array multi-ventosa per schede FR4 non popolate, sulla stessa interfaccia stepper dello strumento #13 sopra. [Config jumper/cablaggio →](images/TOOL_VACUUM_GRIPPER_LG.png)
18. **Testa di Test Funzionale:** test di tensione/continuità a sonda volante - lettura base tramite l'ADC integrato, lettura avanzata tramite un ADC 16-bit ADS1115 su una scheda di espansione **advanced**. [Config jumper/cablaggio →](images/TOOL_FLYING_PROBE.png)
19. **Testa di Cura UV:** driver LED UV ad alta potenza per la cura istantanea di colla/maschera. [Config jumper/cablaggio →](images/TOOL_UV_CURING.png)
20. **Ugello per Rilavorazione ad Aria Calda:** elemento riscaldante, soffiatore a turbina, e feedback della termocoppia per rifusione di componenti SMD disallineati - condivide il proprio loop di controllo termico con il saldatore. [Config jumper/cablaggio →](images/TOOL_HOTAIR_REWORK.png)
21. **Inseritore Pneumatico Press-Fit:** controllo attuatore lineare per pressare connettori nei PCB - l'attuatore e il proprio sensore vivono sulla mainboard propria del robot, fuori dallo scopo proprio di questa scheda. [Config jumper/cablaggio →](images/TOOL_PRESSFIT_INSERTER.png)
22. **Attuatore di Cablaggio/Crimpatura:** ganascia ad alta coppia per spellare/crimpare terminali, pilotata dal driver proprio di una **scheda di espansione** piuttosto che da quello della scheda principale. [Config jumper/cablaggio →](images/TOOL_CRIMPING_ACTUATOR.png)
23. **Ispezione Avanzata PCB:** termografia (array della famiglia MLX9064x - tutti e 3 i membri della famiglia, MLX90640/MLX90641/MLX90642, supportati oggi, sia tramite il chip slave proprio di una scheda di espansione **advanced** sia tramite una scheda di espansione **basic** MLX9064x cablata direttamente alla scheda principale) per individuare cortocircuiti tramite firma termica, insieme all'illuminazione ad anello LED. Copre anche la depanellizzazione con micro-mandrino - stesso percorso hardware del trapano sopra, un bit diverso per un lavoro diverso. [Config jumper/cablaggio →](images/TOOL_THERMAL_INSPECTION.png)
24. **Valvola di Jetting Pasta Saldante:** erogazione micro-goccia piezoelettrica, precisione di impulso sub-millisecondo generata localmente su una scheda di espansione **advanced**. [Config jumper/cablaggio →](images/TOOL_PASTE_JETTING.png)
25. **Saldatrice a Ultrasuoni / Sigillatrice per Confezionamento:** trigger a trasduttore ad alta frequenza per saldatura di involucri in plastica. [Config jumper/cablaggio →](images/TOOL_ULTRASONIC_WELDER.png)

*(Le immagini di configurazione strumento esistono per gli strumenti 1-12; le immagini per gli strumenti 13-25 verranno popolate man mano che la documentazione hardware si aggiorna - i nomi file sopra corrispondono alla convenzione di denominazione già in uso in `images/`.)*

## 🖥️ Interfaccia OLED Locale

Ogni tool-head mostra telemetria dal vivo, specifica per lo strumento, su un OLED a due toni 128×64: uno splash di avvio animato all'accensione, un indicatore lampeggiante di attività CAN, una lettura "hero" dal vivo nella striscia superiore (temperatura, RPM, potenza - qualsiasi cosa sia più importante per lo strumento attivo), e una piccola icona animata a quattro fotogrammi per profilo strumento.

### Il modulo

Entrambe le varianti fisiche sotto sono lo stesso pannello elettricamente (pilotato da SSD1306 o SSD1315 - la sequenza di init del firmware è verificata compatibile con entrambi, vedi `OLED_Init()` in `firmware_oled_driver.c`; l'SSD1315 è un controller sostitutivo più recente, drop-in, con cui molti moduli vengono venduti oggi sotto lo stesso listino/serigrafia "SSD1306"), **128×64**, e la stessa suddivisione a due toni "giallo/blu", dove il materiale LED fisico stesso è diviso in due zone a colore fisso (questo non è selezionabile via software):

* **16 pixel superiori (pagine 0-1): giallo.** URTC usa questa striscia per qualsiasi cosa sia più utile vedere a colpo d'occhio senza leggere attentamente - l'indicatore di attività CAN, letture hero dal vivo, o (sullo splash di avvio / schermate di strumento non valido) breve testo di stato.
* **48 pixel inferiori (pagine 2-7): blu.** Tutto il resto - icone strumento, telemetria dettagliata, il volto animato JuanenBOT sulla schermata splash, la grande scritta lampeggiante ERROR.

Entrambi finiscono sullo stesso bus I2C2 e sullo stesso `OLED_Init()` - il firmware non può distinguere quale dei due sia collegato, e non ne ha bisogno. Sono mutuamente esclusivi su una data scheda (vedi la nota di `CONN_OLED2` in `BOM/BOM.TXT` - il nome usato in questo documento per ciò che lo schematico chiama `LCD1`).

#### Opzione A — montaggio diretto (`CONN_OLED2`, il footprint effettivamente popolato sulla scheda)

<img src="images/OLED_DIRECT_MOUNT.jpg" width="220">

Un pannello nudo senza scheda breakout separata - solo il vetro e il suo nastro FPC a 30 pin, saldato direttamente nel footprint `CONN_OLED2` (`FPC30`, WiseChip UG-2864, il nome usato in questo documento per ciò che lo schematico chiama `LCD1` - vedi `BOM/BOM.TXT` e `URTC_NETLIST.TXT`). Dei 30 pin, solo un sottoinsieme è effettivamente cablato - il resto è il bus a interfaccia parallela del pannello (`D2`–`D7`, `RW`, `E/!RD`), lasciato non connesso poiché la scheda parla con esso solo via I2C:

| Pin(s) di CONN_OLED2 | Rete | Funzione |
|---|---|---|
| 1, 8, 29, 30 | GND / AGND | Massa |
| 9 | VDD | Alimentazione logica (da `+3V3B`, la rail dedicata solo all'OLED - vedi BOM §1) |
| 28 | VCC | Alimentazione pannello |
| 2–5 | C2P/C2N/C1P/C1N | Condensatori charge-pump — `C26`/`C27` nella distinta base |
| 26 | IREF | Resistore di impostazione corrente di riferimento |
| 27 | VCOMH | Disaccoppiamento tensione comune interna |
| 10, 12 | BS0, BS2 | Collegati a GND |
| 11 | BS1 | Collegato a `+3V3B` |
| 18 | D0/SCK | I2C2 SCL — PA9 |
| 19 | D1/DIN/SDA | I2C2 SDA — PA10 |

`BS0`/`BS1`/`BS2` sono lo strap proprio di selezione interfaccia del pannello (GND/VCC/GND qui), fisso via hardware anziché esposto all'MCU - è questo che mette il controller in modalità I2C fin dall'inizio, anziché nella modalità parallela 8080/6800 a cui appartengono gli altri 22 pin FPC.

#### Opzione B — modulo breakout (`CONN_OLED`, alternativa esterna)

<img src="images/OLED_BREAKOUT_MODULE.jpg" width="220">

Lo stesso pannello pre-montato su una piccola scheda carrier con un header a 4 pin - utile se preferisci cablare un modulo pronto all'uso piuttosto che procurarti il pannello FPC nudo. Cablato direttamente a `CONN_OLED` senza necessità di incrocio - l'ordine dei pin proprio del modulo (`GND · VDD · SCK · SDA`) corrisponde esattamente, pin per pin, al pinout di `CONN_OLED`:

| Pin modulo OLED | Pin CONN_OLED | Segnale |
|---|---|---|
| GND | 1 | Massa |
| VDD | 2 | +3.3V (alimentazione logica display) |
| SCK | 3 | SCL — PA9, I2C2 hardware |
| SDA | 4 | SDA — PA10, I2C2 hardware |

### Splash di avvio

<img src="ani/splash_boot.gif" width="480">


### Icone strumenti (una per profilo, animazione a 4 fotogrammi)

<table>
<tr>
<td align="center"><img src="ani/00_soldering_iron.gif" width="80"><br>Saldatore T12</td>
<td align="center"><img src="ani/01_paste_dispenser.gif" width="80"><br>Dispenser Pasta</td>
<td align="center"><img src="ani/02_liquid_dispenser.gif" width="80"><br>Dispenser Liquido</td>
<td align="center"><img src="ani/03_screwdriver.gif" width="80"><br>Cacciavite</td>
</tr>
<tr>
<td align="center"><img src="ani/04_vacuum_pickup.gif" width="80"><br>Pinza a Vuoto</td>
<td align="center"><img src="ani/05_drill.gif" width="80"><br>Trapano (BL4260)</td>
<td align="center"><img src="ani/06_gripper_gimbal.gif" width="80"><br>Pinza Gimbal</td>
<td align="center"><img src="ani/07_gripper_nema.gif" width="80"><br>Pinza NEMA</td>
</tr>
<tr>
<td align="center"><img src="ani/08_aoi_inspection.gif" width="80"><br>Ispezione AOI</td>
<td align="center"><img src="ani/09_laser_engraver.gif" width="80"><br>Incisore Laser</td>
<td align="center"><img src="ani/10_3d_printer.gif" width="80"><br>Hotend Stampante 3D</td>
<td align="center"><img src="ani/11_scan_probe.gif" width="80"><br>Sonda Scanner 3D</td>
</tr>
<tr>
<td align="center"><img src="ani/12_smt_pickplace.gif" width="80"><br>SMT Pick & Place</td>
<td align="center"><img src="ani/13_electromagnet.gif" width="80"><br>Elettromagnete</td>
<td align="center"><img src="ani/14_spot_welder.gif" width="80"><br>Saldatrice a Punti</td>
<td align="center"><img src="ani/15_conformal_coating.gif" width="80"><br>Coating Conforme</td>
</tr>
<tr>
<td align="center"><img src="ani/16_vacuum_gripper_lg.gif" width="80"><br>Pinza a Vuoto (LG)</td>
<td align="center"><img src="ani/17_flying_probe.gif" width="80"><br>Sonda Volante</td>
<td align="center"><img src="ani/18_uv_curing.gif" width="80"><br>Cura UV</td>
<td align="center"><img src="ani/19_hotair_rework.gif" width="80"><br>Rilavorazione Aria Calda</td>
</tr>
<tr>
<td align="center"><img src="ani/20_pressfit_inserter.gif" width="80"><br>Inseritore Press-Fit</td>
<td align="center"><img src="ani/21_crimping_actuator.gif" width="80"><br>Attuatore Crimpatura</td>
<td align="center"><img src="ani/22_thermal_inspection.gif" width="80"><br>Ispezione Termica</td>
<td align="center"><img src="ani/23_paste_jetting.gif" width="80"><br>Jetting Pasta</td>
</tr>
<tr>
<td align="center"><img src="ani/24_ultrasonic_welder.gif" width="80"><br>Saldatrice Ultrasuoni</td>
</tr>
</table>


### Avviso ID strumento non valido

Se i jumper ID non corrispondono a nessuno dei 25 profili assegnati, la scheda blocca ogni attuatore e lampeggia invece questo:

<img src="ani/error_warning.gif" width="480">

Tutte le GIF sorgente delle animazioni vivono in [`/ani`](ani/).

## 🔴🟢🔵 LED di Stato Digitale

Separato dall'OLED e dall'anello di illuminazione a 8 pixel, `CONN_LED1` porta un singolo LED RGB indirizzabile (famiglia WS2812B, pilotato via SPI/DMA) dedicato allo stato a colpo d'occhio.

**Automatico di default, sovrascrivibile dall'host a richiesta.** Il firmware colora questo LED da solo, con priorità a tre vie:

* 🔴 **Rosso** — un guasto hardware è attivo (`system_error_flag`). Vince sempre, indipendentemente da qualsiasi altra cosa in corso.
* 🔵 **Blu** — la scheda sta funzionando attivamente: una trama CAN (qualsiasi ID) è arrivata negli ultimi 1,5 secondi.
* 🟢 **Verde** — inattiva, in attesa di comandi: nessun traffico CAN da oltre 1,5 secondi.

Il master può comunque sovrascrivere questo in qualsiasi momento inviando l'ID CAN `0x100` (DLC 8) con l'intensità di rosso, verde, e blu come primi tre byte (0-255 ciascuno - colore 24-bit completo, non solo i tre automatici). Un colore inviato dall'host resta attivo per 10 secondi prima di tornare allo schema automatico - abbastanza lungo da essere effettivamente visto, abbastanza breve perché la scheda non resti bloccata a mostrare un colore personalizzato obsoleto se l'host smette di aggiornarlo. Inviare di nuovo `0x100` (sia lo stesso colore che uno nuovo) rinnova questa finestra di 10 secondi, quindi un host che vuole mantenere il controllo personalizzato deve semplicemente continuare a inviarlo periodicamente. Un guasto hardware interrompe sempre un override attivo - il rosso ha priorità su qualsiasi colore che l'host avesse impostato.

Vedi `docs/CANBUS.TXT` (ID `0x100`) per il layout esatto dei byte, che condivide anche questo stesso messaggio con il controllo dell'anello LED e della modalità notturna dell'OLED.

## 📸 Foto

![URTC v1.0](images/URTC_BOARD.png)

*(Lavoro in corso — altre angolazioni e una scheda popolata in arrivo presto.)*

## 🔧 Compilazione e Flashing

La flash di URTC è divisa in due parti indipendenti, così la scheda può essere riflashata sullo stesso cavo ombelicale CAN che già usa per tutto il resto - senza mai più necessitare di accesso fisico all'header JTAG/SWD dopo la configurazione iniziale.

### Struttura della memoria flash (256K totali, modello di aggiornamento golden-image / A-B)

```
0x08000000 ┌─────────────────────────────────┐
           │  Bootloader (30K)                 │  Viene sempre eseguito per primo ad ogni avvio.
           │                                   │  Ascolta brevemente sul CAN, poi o
           │                                   │  salta all'app o attende un
           │                                   │  aggiornamento. Pilota l'OLED direttamente
           │                                   │  durante un aggiornamento (vedi sotto).
0x08007800 ├─────────────────────────────────┤
           │  Pagina metadati (2K)             │  Descrive qualsiasi cosa si trovi nello
           │                                   │  slot principale in questo momento: HardwareID,
           │                                   │  versione, dimensione, CRC32, e una
           │                                   │  firma HMAC-SHA256. Il
           │                                   │  bootloader verifica tutto questo
           │                                   │  prima di saltare mai all'app.
0x08008000 ├─────────────────────────────────┤
           │  Slot principale (112K)           │  Questo è il firmware applicativo /
           │                                   │  URTC_MAIN_FIRMWARE_v0.2.7.* — il firmware
           │                                   │  vero e proprio che gira giorno per giorno,
           │                                   │  descritto ovunque altrove in
           │                                   │  questo README. Mai toccato da
           │                                   │  un aggiornamento finché un'immagine
           │                                   │  verificata e nota come funzionante non è
           │                                   │  pronta a sostituirlo.
0x08024000 ├─────────────────────────────────┤
           │  Slot backup / staging (112K)     │  Solo archiviazione grezza, mai
           │                                   │  eseguito direttamente. Ogni
           │                                   │  aggiornamento CAN scrive qui per primo.
0x08040000 └─────────────────────────────────┘
```

**Perché uno slot di backup.** Un aggiornamento CAN non viene mai scritto nello slot che sta attualmente eseguendo. Va prima nel backup, viene completamente verificato lì - dimensione, CRC32, e una firma HMAC-SHA256 che dimostra che sia effettivamente arrivato dal processo di build proprio di questo progetto, non solo che sia arrivato intatto - e solo dopo viene copiato nello slot principale. Una perdita di alimentazione in qualsiasi punto prima che quella copia inizi lascia il firmware attualmente in esecuzione completamente intatto, quindi non c'è finestra in cui un download interrotto possa rendere inutilizzabile la scheda. Se la perdita di alimentazione avviene *durante* la copia stessa, il bootloader se ne accorge al successivo avvio (il backup, mai toccato durante la copia, è ancora completamente intatto) e semplicemente riprende a copiare da lì finché non ha successo.

### 0. Compilazione dai sorgenti (opzionale — `firmware/` include già binari precompilati)

Due modi per passare dai sorgenti di questo repository ai 4 binari sopra:

- **Automatizzato:** `build_firmware.sh` (Linux) o `build_firmware.bat` (Windows), alla radice del repository. Ognuno installa la toolchain ARM GNU se manca, scarica il commit fissato di ST HAL/CMSIS, e compila, linka, e passa via `objcopy` tutti e 4 i binari (app + bootloader scheda principale, app + bootloader slave di espansione) direttamente in `firmware/`, poi rigenera `firmware/firmware_manifest.json`. Esegui senza argomenti per una build completa, `--clean` per svuotare prima la cache locale `build/`, o `master`/`slave` per compilare solo la coppia propria di un chip. `build_firmware.sh` viene eseguito end-to-end contro il vero albero sorgente di questo progetto; `build_firmware.bat` rispecchia la stessa logica per Windows - se i due mai dovessero non concordare, fidati della logica dello script `.sh` come riferimento.
- **Manuale:** ogni comando eseguito da entrambi gli script, più il ragionamento dietro ogni scelta di toolchain/HAL, è dettagliato passo passo in `docs/COMPILE_STM32F303.TXT` - utile su un sistema operativo diverso, con un sorgente HAL/CMSIS diverso, o semplicemente per vedere esattamente cosa gli script automatizzano.

Dopo qualsiasi modifica al sorgente firmware (o prima di fidarsi di un incremento di versione), esegui **`check_version_consistency.sh`** dalla radice del repository: legge le costanti di versione delle Track A/E (firmware scheda principale, applicazione slave di espansione) come fonte di verità e controlla ogni posizione che `VERSION_CHECKLIST.txt` documenta per quel tag di versione, segnalando qualsiasi discrepanza - segnala soltanto, non corregge nulla da solo. `VERSION_CHECKLIST.txt` è il riferimento completo per tutti e 5 i track di versione indipendenti che questo progetto porta (firmware principale, hardware/PCB, bootloader principale, applicazione slave di espansione, bootloader slave di espansione) ed esattamente cosa va toccato quando si incrementa uno qualsiasi di essi.

### 1. Configurazione iniziale — richiede JTAG/SWD (una tantum)

Il bootloader può arrivare sul chip solo tramite programmazione fisica - non c'è modo di flashare via CAN una scheda che non ha ancora un bootloader su di essa. Questo è un passo una tantum:

1. Apri il progetto in **STM32CubeIDE** (compilato e testato contro il target STM32F303CC), o usa **STM32CubeProgrammer** direttamente con gli output compilati sotto.
2. Flasha **entrambe** le immagini via SWD (ST-Link) tramite l'header integrato `STM_JTAG` - ogni file `.hex` ha il proprio indirizzo di destinazione incorporato, così la maggior parte degli strumenti (incluso STM32CubeProgrammer) può caricare entrambi nella stessa sessione:
   * `URTC_MAIN_BOOTLOADER_v0.3.6.hex` → `0x08000000`
   * `URTC_MAIN_FIRMWARE_v0.2.7.hex` → `0x08008000`
3. Imposta l'identità dello strumento tramite i jumper di saldatura ID prima di accendere - la scheda li legge una volta all'avvio, come sempre. Cinque jumper (ID0-ID4), che coprono l'intero spazio di 32 indirizzi (31 indirizzi diretti per strumento, più l'indirizzo riservato `11111` di configurazione libera - vedi la sezione Matrice degli Strumenti sopra).
4. Accendi. Il bootloader ascolta per ~600ms, non vede nulla, e salta direttamente nell'applicazione - da qui in avanti, tutto si comporta esattamente come descritto nel resto di questo README.

**L'header JTAG non viene mai rimosso né disabilitato.** È sempre lì come ripiego - se un aggiornamento CAN va mai storto, o semplicemente lo preferisci, puoi riflashare entrambe le immagini via SWD in qualsiasi momento.

**Due pulsanti integrati, BOOT e RESET**, sono presenti anche per il recupero - RESET è un reset hardware ordinario (`NRST`), e BOOT porta `BOOT0` alto, che è una decisione a livello di chip presa *prima* che qualsiasi cosa in questo repository venga eseguita: normalmente (non tenuto premuto) il chip si avvia dalla flash nel bootloader proprio di questo progetto come descritto sopra; tenuto premuto al reset, si avvia invece nel bootloader di System Memory di fabbrica proprio di ST (USB DFU/recupero UART, completamente separato da qualsiasi cosa qui). Vedi `src/F303-master/README.md` sezione 4a per il dettaglio tecnico completo.

### 2. Aggiornamenti successivi — via bus CAN

Una volta che il bootloader è in posizione, aggiornare l'applicazione non richiede più affatto accesso fisico alla scheda - basta inviare la nuova build firmware sulla stessa linea CAN ombelicale che già porta i comandi al tool-head.

**La sequenza di aggiornamento:**

1. **Trigger.** Il master invia `0x7F0` (DLC 4, payload `B0 07 1D 5A`) all'*applicazione in esecuzione*. Questo taglia in sicurezza l'alimentazione a ogni attuatore inline - motori, riscaldatori, laser - e resetta il chip. Questo requisito di payload magico fa sì che una trama corrotta o malformata non possa attivare accidentalmente un reset in modalità aggiornamento.
2. **Avvio.** Dopo il reset, il bootloader sta ascoltando. Il master invia `0x7F1` (DLC 8, dimensione totale del firmware big-endian + HardwareID big-endian). Un'immagine costruita per hardware diverso viene rifiutata proprio qui, prima che un singolo byte di flash venga toccato. Il bootloader cancella esattamente tante pagine dello slot di backup quante ne servono alla nuova immagine e risponde con una trama di stato (`0x7F5`).
3. **Firma.** Il master invia la firma HMAC-SHA256 attesa come quattro trame `0x7F7` (8 byte ciascuna, in ordine) - calcolata sull'immagine firmware con una chiave condivisa tra il bootloader e qualsiasi strumento firmi la build.
4. **Dati.** Il master trasmette il file `.bin` come una sequenza di trame `0x7F2` (fino a 8 byte di dati firmware grezzi ciascuna), inviate una dopo l'altra - il CAN garantisce che le trame arrivino nell'ordine in cui sono state inviate su un singolo bus, quindi non serve alcun numero di sequenza per trama. Il bootloader mette in buffer i byte in arrivo in una pagina di 2KB in RAM e la scrive nello slot di *backup* una volta piena, rileggendo ogni half-word e confrontandola con ciò che era destinato a essere scritto prima di considerare completata la pagina, e inviando una conferma `0x7F3` (con l'indice di pagina) dopo ogni scrittura verificata. Un'implementazione master ragionevole attende la conferma di ogni pagina prima di inviare i dati della pagina successiva, per evitare di sovraccaricare il buffer di ricezione del bootloader.
5. **Fine e verifica.** Una volta che ogni byte è stato inviato, il master invia `0x7F4` (DLC 8, CRC32 big-endian + versione major/minor). Il bootloader controlla la dimensione dello slot di backup, calcola il proprio CRC32 e HMAC-SHA256 e li confronta entrambi con ciò che il master ha dichiarato. Solo se tutto corrisponde procede a copiare il backup nello slot principale, pagina per pagina, con la stessa verifica a rilettura di sopra. Una volta che quella copia è completa e confermata, salva i nuovi metadati e si resetta nell'applicazione aggiornata. Su qualsiasi discrepanza - dimensione, CRC32, HMAC, o HardwareID - lo slot principale non viene mai toccato affatto, e il bootloader semplicemente torna ad ascoltare per un nuovo tentativo.

**Trame di stato (`0x7F5`, DLC 1):** `0x01` in ascolto, `0x02` in cancellazione, `0x03` in ricezione, `0x06` in verifica, `0x07` copia backup in principale, `0x04` verificato OK (sta per saltare), `0x05` verifica fallita, `0xFF` errore.

**Heartbeat (`0x7F6`, DLC 2, ogni ~1s mentre ascolta o aggiorna):** byte di stato + percentuale di avanzamento (0-100, o `0xFF` dove una percentuale non si applica). Permette al master di distinguere "il nodo è vivo ma non ha ancora iniziato ad ascoltare" da "il nodo è completamente non responsivo" - utile per l'avvio automatizzato e per individuare un bootloader bloccato senza dover attendere un timeout.

**Avanzamento a schermo.** Il bootloader pilota l'OLED direttamente durante un aggiornamento - nessuno deve indovinare se qualcosa sta succedendo. Mostra "UPDATING" più una barra di avanzamento e percentuale dal vivo mentre le pagine vengono scritte o copiate, "FLASH OK" per un istante prima di resettarsi nel nuovo firmware, e "ERROR" se una scrittura di pagina fallisce, il trasferimento si blocca per più di 10 secondi, o la verifica torna con una discrepanza.

**⚠️ Testalo su banco prima di fidartene sul campo.** Il protocollo sopra compila e linka senza problemi e la logica è stata ragionata con attenzione, ma un bootloader è esattamente il tipo di firmware in cui "compila correttamente" è ben lontano da "affidabile sull'hardware" - la temporizzazione reale di programmazione flash, il comportamento CAN attraverso un trasferimento di migliaia di trame, e il passaggio di consegne bootloader-applicazione devono tutti essere verificati su una scheda reale (idealmente con JTAG a portata di mano come ripiego) prima di fare affidamento su questo per un aggiornamento non presidiato con attuatori reali collegati.

### Strumenti PC

Due strumenti GUI standalone, multipiattaforma (Windows/Linux), supportano questa
scheda - **URTC Flasher** (aggiornamenti CAN-OTA e a chip completo via SWD/JTAG, sia
per questa scheda che, su una variante di espansione Advanced, per il proprio
chip slave di espansione) e **URTC Tester** (un esercitatore di bus CAN dal vivo
che mostra qualunque profilo strumento sia attualmente selezionato via jumper). Entrambi vivevano
in precedenza in questo repository sotto `tools/`; ognuno è ora un proprio
progetto indipendente, con proprio README, licenza, e traduzioni:

- [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER)
- [URTC Tester](https://github.com/JuanenRac/URTC-TESTER)

Esiste anche un'alternativa basata sul web che copre terreno simile (monitoraggio dal vivo, analisi CAN, flashing OTA, ispezione termica) senza installare nulla localmente: [URTC Web Studio](https://github.com/JuanenRac/URTC-WEB-STUDIO).

## 📋 Changelog

Firmware e bootloader sono versionati e rilasciati indipendentemente -
flashare un nuovo bootloader non implica una nuova versione dell'applicazione e
viceversa, quindi ognuno ha la propria cronologia nel proprio file piuttosto che
un unico numero di versione combinato che implicherebbe che si muovano sempre
insieme:

- Firmware (`src/F303-master/`): [`src/F303-master/CHANGELOG.md`](src/F303-master/CHANGELOG.md)
- Bootloader (`src/F303-master/boot/`): [`src/F303-master/boot/CHANGELOG.md`](src/F303-master/boot/CHANGELOG.md)
- Applicazione Slave di Espansione (`src/F303-slave/`, STM32F303CBT6): [`src/F303-slave/CHANGELOG.md`](src/F303-slave/CHANGELOG.md)
- Bootloader Slave di Espansione (`src/F303-slave/boot/`): [`src/F303-slave/boot/CHANGELOG.md`](src/F303-slave/boot/CHANGELOG.md)

**Politica di versioning:** tutti e 4 i componenti (2 firmware applicativi, 2 bootloader - `FIRMWARE_VERSION_MAJOR`/`MINOR`/`PATCH` e `BOOTLOADER_VERSION_MAJOR`/`MINOR`/`PATCH`) sono **incrementali** - ogni build reale incrementa automaticamente il proprio `PATCH` di 1 (`bump_version.py` nella radice del repository, chiamato da `build_firmware.sh`/`.bat` subito prima di compilare ciascun componente), con riporto su `MINOR` (poi `MAJOR`) quando `PATCH` supererebbe 9, la stessa regola in base 10 del "contachilometri" usata da un odometro reale - es. `0.1.7` → `0.1.8` → `0.1.9` → `0.2.0`, mai `0.1.10`. Ogni bootloader mantiene anche una propria copia del `FIRMWARE_VERSION_*` dell'applicazione corrispondente, sincronizzata automaticamente dallo stesso bump. Vedi [`CHANGELOG.md`](CHANGELOG.md) nella radice del repository per lo stato attuale di tutti e 4 i componenti a colpo d'occhio, e [`VERSION_CHECKLIST.txt`](VERSION_CHECKLIST.txt) per la meccanica completa per traccia.

## 🔍 Stato Attuale

**Firmware (`src/F303-master/`):** funzionalmente completo per tutti i 25 profili strumento - controllo PID termico, telemetria per strumento, watchdog di comunicazione, rilevamento stallo/guasto, e la diagnostica dal vivo propria dell'OLED, insieme a una coppia di interrogazione dello strumento attivo (`0x110`/`0x111`), un passthrough SPI generico (`0x180`/`0x181`) per il connettore di espansione, una F-RAM integrata che mantiene i setpoint attraverso una perdita di alimentazione (`0x190`/`0x191`), il meccanismo di configurazione libera dello strumento tramite jumper `11111` (`0x1A2`/`0x1A3`), la segnalazione del tipo di periferica + numero seriale del dispositivo (`0x1A4`/`0x1A5`) per distinguere più schede altrimenti identiche su un bus condiviso, e un bridge CAN-a-I2C (`0x210`-`0x221`) che raggiunge il chip slave di espansione sulle schede di espansione advanced. Versionato indipendentemente dal bootloader (vedi il Changelog sotto).

**Bootloader (`src/F303-master/boot/`):** sistema di aggiornamento A/B a golden-image funzionalmente completo - aggiornamenti OTA firmati HMAC-SHA256 via CAN, uno slot di backup che garantisce che un aggiornamento fallito non renda mai inutilizzabile la scheda, e la propria segnalazione di versione (`0x7FA`) indipendente dall'applicazione. Compila e linka senza problemi; vedi l'avvertenza sul test da banco sopra prima di fidartene non presidiato con attuatori reali collegati.

**Strumenti PC:** sia [URTC Flasher](https://github.com/JuanenRac/URTC-FLASHER) (aggiornamenti CAN OTA + programmazione a chip completo via SWD/JTAG) che [URTC Tester](https://github.com/JuanenRac/URTC-TESTER) (esercitatore dal vivo di controllo/telemetria per strumento) sono funzionalmente completi per ciò che si erano proposti di fare, ognuno ora un proprio progetto indipendente con un proprio README che copre configurazione e ogni controllo in dettaglio.

**Hardware:** schematico e distinta base sono ancora in fase di finalizzazione; nessuna scheda popolata esiste ancora per validare quanto sopra contro silicio reale. Tutto quanto sopra compila, linka, ed è stato ragionato con attenzione, ma "compila correttamente" e "verificato sull'hardware" sono due affermazioni diverse - vedi l'avviso di sicurezza in cima a questo README, e tratta un primo bring-up con la cautela che qualsiasi scheda nuova merita.

Se qualcuno nella community sta lavorando su end-effector personalizzati, cambia-utensili intelligenti, o integrazione avanzata di strumenti per PAROL6, Faze4, o qualsiasi altra piattaforma di braccio robotico, mi piacerebbe chiacchierare, scambiare idee, o approfondire i comandi CAN!

## 📂 Struttura del Repository

```
/
├── 3D/
│   ├── RACK/                    Rack di montaggio scheda, 2 varianti (x1, x3) - ciascuna in
│   │                            .stl/.3mf/.amf/.scad
│   ├── REVOLVER/                Segnaposto - vuoto, contenuto non ancora iniziato
│   └── TOOLS/
│       └── PAROL6/              Parti stampabili in 3D per strumento, per il braccio robotico PAROL6 -
│                                una sottocartella per strumento (0.Universal parts, poi 1-12
│                                corrispondenti alla numerazione del Catalogo Strumenti sopra), ciascuna in
│                                .stl/.3mf/.amf/.scad dove popolata; diverse
│                                (4, 6-12) sono ancora segnaposto vuoti
├── ani/                          27 GIF: un'animazione a 4 fotogrammi per profilo strumento (00-24,
│                                 corrispondenti all'ID numerico proprio di ogni strumento), lo splash di avvio
│                                 (splash_boot.gif), e l'avviso di ID non valido
│                                 (error_warning.gif) - tutte decodificate direttamente dal
│                                 sorgente firmware proprio di questo progetto (le tabelle proprie
│                                 ToolIcons[]/SplashFace[]/ErrorText[] di firmware_render.c), non
│                                 disegnate a mano separatamente, così corrispondono sempre a ciò che
│                                 l'OLED reale mostra effettivamente
├── BOM/
│   ├── BOM.TXT                  Distinta base completa della scheda PCB
│   ├── BOM_EXPANSION_BASIC_TMC2209.TXT     Scheda di espansione, basic + TMC2209
│   ├── BOM_EXPANSION_BASIC_TMC5160A.TXT    Scheda di espansione, basic + TMC5160A
│   ├── BOM_EXPANSION_ADVANCED_TMC2209.TXT  Scheda di espansione, advanced + TMC2209
│   ├── BOM_EXPANSION_ADVANCED_TMC5160A.TXT Scheda di espansione, advanced + TMC5160A
│   ├── BOM_EXPANSION_BASIC_ADS1115.TXT     Scheda di espansione, basic + ADS1115 (solo sensore, senza driver/MCU)
│   └── BOM_EXPANSION_BASIC_MLX9064X.TXT    Scheda di espansione, basic + MLX9064x (solo sensore, senza driver/MCU)
├── docs/
│   ├── CANBUS.TXT               Riferimento del protocollo bus CAN (tutti gli ID comando/telemetria)
│   ├── ECOVIA.TXT               Matrice di identificazione strumento e logica di mutazione pin
│   ├── TOOLS.TXT                Catalogo ad alto livello di tutti i 25 strumenti - cosa fa ognuno e
│   │                            quali periferiche usa, nessun dettaglio a livello di pin
│   ├── PINOUT.TXT               Pinout completo dell'MCU, blocco per blocco
│   ├── PINOUT_CONNECTORS.TXT    Pinout dei connettori fisici (CONN_DRILL, CONN_SEN, ecc.)
│   ├── EXPANSION.TXT            Connettore CONN_EXPANSION e le varianti di scheda add-on
│   ├── PINOUT_SLAVE.txt         Pinout completo per il chip slave di espansione (solo varianti advanced)
│   ├── EEPROM.TXT               Mappa completa dei registri F-RAM (ogni impostazione persistita, offset dei byte)
│   ├── COMPILE_STM32F303.TXT    Guida di build da zero per tutti i 4 binari firmware -
│   │                            toolchain, configurazione ST HAL/CMSIS, comandi esatti di compilazione/linking;
│   │                            build_firmware.sh/.bat alla radice del repository automatizzano lo stesso
│   │                            processo end-to-end
│   ├── datasheet/               2 datasheet di componenti non già coperti sotto
│   │                            PCB/datasheet/ (CFM_40.pdf, EFB0424VHD-CP0.pdf)
│   └── tool_image_generator/    Toolkit che genera images/TOOL_*.png (vedi sotto) -
│                                render_engine.py + tool_data.py + generate_all.py, e
│                                PROCEDURE.TXT che spiega come aggiungere l'immagine propria di un nuovo strumento
│                                o rigenerarne una esistente
├── src/
│   ├── F303-master/
│   │   ├── STM32F303CC_main.c    Punto di ingresso - definizioni globali e main()
│   │   ├── firmware_*.c/.h       ~85 altri file, uno per sottosistema (OLED, LED, gestori
│   │   │                         CAN per strumento, init, persistenza, ecc.), incluso
│   │   │                         firmware_ads1115.c (driver ADS1115 diretto, scheda Basic+ADS1115),
│   │   │                         vedi la tabella completa file-per-file nel README.md proprio di questa cartella
│   │   ├── melexis_mlx90640/     Libreria ufficiale di Melexis per MLX90640 (Apache-2.0,
│   │   │                         C puro) più il driver a connessione diretta proprio di questa scheda
│   │   │                         costruito sopra di essa, per la scheda di espansione Basic+MLX9064x
│   │   ├── melexis_mlx90641/     Stessa idea, libreria MLX90641 (Apache-2.0, C++ - vedi la
│   │   │                         sezione 8a del README.md proprio di questa cartella per il motivo per cui questa unica
│   │   │                         libreria è C++ in un progetto altrimenti tutto-C)
│   │   ├── melexis_mlx90642/     Stessa idea, libreria MLX90642 (Apache-2.0, C puro) - vedi
│   │   │                         la sezione 8a per il motivo per cui il driver proprio di questo sensore è
│   │   │                         genuinamente più semplice degli altri 2
│   │   ├── STM32F303CCTx_APP.ld  Script di linking per l'applicazione (slot principale 112K a 0x08008000)
│   │   ├── README.md             Riferimento tecnico: piattaforma hardware, il sistema di selezione
│   │   │                         strumento tramite jumper ID, cablaggio periferiche per strumento - vedi
│   │   │                         CANBUS.TXT per il protocollo a livello di trasmissione di cui questo spiega il perché
│   │   └── boot/
│   │       ├── bootloader_main.c  Punto di ingresso per il bootloader
│   │       ├── bootloader_*.c/.h  9 altri file (tipi/costanti condivisi, crittografia,
│   │       │                      flash/metadati, OLED, protocollo CAN)
│   │       ├── STM32F303CCTx_BOOTLOADER.ld  Script di linking per il bootloader (regione 30K a 0x08000000)
│   │       └── README.md          Stesso ruolo di riferimento tecnico dell'applicazione, per il bootloader
│   └── F303-slave/               Chip compagno (STM32F303CBT6) solo sulle 2 varianti di scheda di espansione
│       │                         ADVANCED - vedi la sezione Connettore di Espansione
│       │                         sopra. Propria coppia bootloader/applicazione, proprio protocollo di aggiornamento
│       │                         basato su I2C (non CAN), proprio versionamento indipendente.
│       ├── slave_main.c          Punto di ingresso
│       ├── slave_*.c/.h          7 altri file (tipi/costanti condivisi, protocollo di collegamento I2C,
│       │                         bus sensore locale, PWM locale)
│       ├── STM32F303CBTx_SLAVEAPP.ld  Script di linking (slot principale 54K a 0x08005000)
│       ├── README.md             Riferimento tecnico: perché questo chip esiste, il
│       │                         bus sensore locale ADS1115/MLX9064x, il PWM locale, il protocollo di
│       │                         collegamento I2C verso la scheda principale
│       ├── melexis_mlx90640/     Libreria ufficiale di Melexis per MLX90640 (Apache-2.0,
│       │                         C puro, non modificata, proprio file di licenza) - mantenuta come propria
│       │                         unità di compilazione separata, deliberatamente mai fusa in
│       │                         il sorgente proprio di questo progetto, poiché Apache-2.0 richiede che
│       │                         la nota di copyright propria di quel codice resti intatta
│       ├── melexis_mlx90641/     Libreria ufficiale di Melexis per MLX90641 (Apache-2.0, C++ -
│       │                         una libreria genuinamente separata dalla propria MLX90640, non una
│       │                         variante di essa - vedi la sezione 3 del README.md proprio di questa cartella
│       │                         per il motivo per cui è C++ e come la build gestisce questo)
│       ├── melexis_mlx90642/     Libreria ufficiale di Melexis per MLX90642 (Apache-2.0, C
│       │                         puro) - interfaccia di trasporto genuinamente più semplice degli altri
│       │                         2 sensori, vedi la sezione 3 del README.md per il motivo
│       └── boot/
│           ├── slaveboot_main.c   Punto di ingresso per il bootloader
│           ├── slaveboot_*.c/.h   7 altri file (crittografia, flash/metadati, protocollo)
│           ├── STM32F303CBTx_SLAVEBOOT.ld  Script di linking (regione 18K a 0x08000000)
│           └── README.md          Stesso ruolo di riferimento tecnico dell'applicazione
├── firmware/
│   ├── URTC_MAIN_BOOTLOADER_v0.3.6.bin  Bootloader compilato, flash a 0x08000000
│   ├── URTC_MAIN_BOOTLOADER_v0.3.6.elf  Bootloader compilato, flash a 0x08000000
│   ├── URTC_MAIN_BOOTLOADER_v0.3.6.hex  Bootloader compilato, flash a 0x08000000 (indirizzo incorporato)
│   ├── URTC_MAIN_FIRMWARE_v0.2.7.bin    Bin applicazione compilato, flash a 0x08008000
│   ├── URTC_MAIN_FIRMWARE_v0.2.7.elf    Elf applicazione compilato, flash a 0x08008000
│   ├── URTC_MAIN_FIRMWARE_v0.2.7.hex    HEX applicazione compilato, flash a 0x08008000 (indirizzo incorporato)
│   ├── URTC_SLAVE_BOOTLOADER_v0.1.9.{bin,elf,hex}  Bootloader proprio dello slave di espansione, flash a 0x08000000
│   │                             sull'STM32F303CBT6 (solo schede di espansione advanced)
│   ├── URTC_SLAVE_FIRMWARE_v0.1.6.{bin,elf,hex}  Applicazione propria dello slave di espansione, flash a 0x08005000
│   └── firmware_manifest.json    Indice leggibile da macchina di tutti e 4 i componenti sopra - versione,
│                                 indirizzo flash, e dimensione/CRC32 proprio di ogni file, affinché
│                                 uno strumento esterno controlli cosa c'è qui e cosa è più recente rispetto a
│                                 qualsiasi cosa abbia attualmente. Rigenerato automaticamente da
│                                 generate_manifest.py (chiamato dall'ultimo passo proprio di build_firmware.sh/.bat) -
│                                 mai modificato a mano.
├── images/
│   ├── OLED_DIRECT_MOUNT.jpg     LCD1/CONN_OLED2 - pannello FPC 30-pin nudo, opzione montaggio diretto
│   ├── OLED_BREAKOUT_MODULE.jpg  CONN_OLED - modulo breakout I2C esterno, opzione alternativa
│   ├── URTC_LOGO.svg             Logo generale del progetto, incorporato in cima a questo README
│   ├── URTC_BOARD.png           Foto scheda
│   ├── URTC_SCHEMATIC.png       Schematico scheda
│   ├── URTC_PCB_TOP.png         Layer TOP della scheda (quando aggiunto)
│   ├── URTC_PCB_BOTTOM.png      Layer BOTTOM della scheda (quando aggiunto)
│   └── TOOL_*.png               Diagramma di riferimento jumper/cablaggio per strumento, uno per profilo
│                                (tutti i 25 presenti - vedi il link proprio di ogni strumento nel Catalogo Strumenti sopra)
├── PCB/
│   ├── URTC_V1.0.sch            Schematico Eagle (quando aggiunto)
│   ├── URTC_V1.0.brd            Layout scheda Eagle (quando aggiunto)
│   ├── URTC_V1.0_JLCPCB.ZIP     File gerber, bom e cpl (quando aggiunti)
│   ├── URTC_BOM.TXT             BOM grezza esportata da Eagle (esportazione di verità di riferimento - vedi
│   │                            BOM/BOM.TXT per la versione curata e organizzata propria di questo progetto)
│   ├── datasheet/               Datasheet di tutti i componenti usati nella scheda
│   └── *_PARLIST/PINLIST/NETLIST.TXT   Netlist esportate da Eagle (verità di riferimento per la mappatura dei pin)
├── tools/
│   ├── build_test.py            Controllo build/compilazione senza incremento di versione
│   └── ci_validate.py           Validazione manifest/CHANGELOG/docs usata dalla CI
├── VERSION_CHECKLIST.txt        Checklist meccanica per incrementare correttamente uno qualsiasi dei
│                                4 numeri di versione indipendenti propri di questo progetto
├── check_version_consistency.sh  Controlli automatizzati di coerenza versione/file - esegui prima di
│                                fidarti delle affermazioni proprie di VERSION_CHECKLIST.txt
├── build_firmware.sh            Installa la toolchain, scarica l'HAL/CMSIS proprio di ST, e
│                                compila tutti e 4 i binari firmware end-to-end (Linux)
├── build_firmware.bat           Stesso, per Windows - vedi docs/COMPILE_STM32F303.TXT per
│                                il processo manuale completo che entrambi gli script automatizzano
├── build-test.sh / build-test.bat  Controllo build/compilazione senza incremento di versione
│                                (wrapper leggeri attorno a tools/build_test.py)
├── bump_version.py              Incremento di versione stile contachilometri, eseguito da build_firmware.sh/.bat
├── bump_manifest_version.py     Sincronizza la versione di hydra-umc.project.json con quella nativa (--sync)
├── generate_manifest.py         Rigenera firmware/firmware_manifest.json - chiamato
│                                automaticamente come ultimo passo di un'esecuzione completa di
│                                build_firmware.sh/.bat, o standalone in qualsiasi momento serva al manifest di
│                                mettersi al passo senza una rebuild completa
├── LICENSE
├── README.md                    Questo file
└── README_spa.md / README_ita.md / README_fra.md / README_deu.md / README_zho.md / README_jpn.md  <- traduzioni
```

I file di progettazione hardware (schematico/board/netlist Eagle) verranno aggiunti man mano che il layout si stabilizza.

## 🔗 Progetti Correlati

Questo progetto fa parte dell'ecosistema robotico HYDRA-UMC dello stesso autore (JuanenRac / Electro Hobby 3D). Vale la pena conoscerlo, poiché una richiesta potrebbe in realtà riguardare uno di questi invece di questo repository.

**Progetti Figli** — ciascuno è uno strumento desktop o browser costruito specificamente per flashare, testare o pilotare questo firmware
- **[URTC-FLASHER](https://github.com/JuanenRac/URTC-FLASHER)** — strumento desktop con GUI per il flashing delle schede URTC, CAN-OTA più SWD/JTAG a chip intero; flasha esattamente questo firmware.
- **[URTC-TESTER](https://github.com/JuanenRac/URTC-TESTER)** — strumento desktop di diagnostica CAN-bus dal vivo per schede URTC, un pannello per profilo utensile; un pannello per ciascuno dei 25 profili utensile propri di questo firmware.
- **[URTC-WEB-STUDIO](https://github.com/JuanenRac/URTC-WEB-STUDIO)** — alternativa basata su browser a URTC-TESTER tramite la Web Serial API, senza installazione locale; lo stesso protocollo CAN dei 2 strumenti desktop sopra.

**Direttamente Correlati**
- **[HYDRA-UMC](https://github.com/JuanenRac/HYDRA-UMC)** — la scheda madre fisica del braccio robotico: host CM5 + coprocessore STM32H745 dual-core, che coordina fino a 8 bracci utensile via CAN-OTA/SPI-OTA; il suo vero tunnel relay FDCAN2 è il canale su cui poggia la testa utensile di questo firmware.
- **[HYDRA-UMC-SDK](https://github.com/JuanenRac/HYDRA-UMC-SDK)** — il contratto JSON-Schema condiviso e la barriera di sicurezza contro cui ogni bridge valida i propri comandi; copre anche servizi, UI, adattatori CM5 e questo firmware.
- **[URTC-SMART-RACK](https://github.com/JuanenRac/URTC-SMART-RACK)** — firmware per un rack di montaggio schede con decodifica reale dell'ID utensile e logica di preriscaldamento Smart Idle; condivide il proprio ecosistema utensili e il bus CAN di questo firmware.
- **[URTC-VISION-TOOL](https://github.com/JuanenRac/URTC-VISION-TOOL)** — firmware più un vero companion di visione Python per una testa utensile di ispezione termica/RGB; condivide il proprio ecosistema utensili e il bus CAN di questo firmware.
- **[HYDRA-UMC-DETECTION-HEF](https://github.com/JuanenRac/HYDRA-UMC-DETECTION-HEF)** — registro reale di modelli compilati con verifica di caricamento sicuro per architettura Hailo/checksum; fornisce il riconoscimento visivo dietro i propri utensili di questo firmware.

**Fa Anche Parte dell'Ecosistema**

*Hardware e Piattaforma di Base*
- **[HYDRA-UMC-OS](https://github.com/JuanenRac/HYDRA-UMC-OS)** — livello prodotto riproducibile su Raspberry Pi OS per il CM5: agente in sola lettura, config/profili validati, provisioning WiFi al primo contatto.

*Backend Centrale e Client*
- **[HYDRA-UMC-SERVER](https://github.com/JuanenRac/HYDRA-UMC-SERVER)** — il vero backend headless (REST/WebSocket) con cui parla davvero ogni client di controllo.
- **[HYDRA-UMC-STUDIO](https://github.com/JuanenRac/HYDRA-UMC-STUDIO)** — dashboard di controllo web con visualizzazione 3D multi-robot in tempo reale.
- **[HYDRA-UMC-SUITE](https://github.com/JuanenRac/HYDRA-UMC-SUITE)** — centro di comando sciame desktop (PySide6) per più server contemporaneamente, pacchettizzato come eseguibile standalone.
- **[HYDRA-UMC-ANDROID-CONTROL](https://github.com/JuanenRac/HYDRA-UMC-ANDROID-CONTROL)** — app di controllo nativa per Android con login biometrico e un companion Wear OS abbinato.
- **[HYDRA-UMC-IOS-CONTROL](https://github.com/JuanenRac/HYDRA-UMC-IOS-CONTROL)** — app di controllo per iOS/iPadOS (Flutter) con sincronizzazione WebSocket in tempo reale.
- **[HYDRA-UMC-DSI](https://github.com/JuanenRac/HYDRA-UMC-DSI)** — interfaccia touch nativa per il touchscreen DSI da 7" a bordo, incorporata direttamente nel CM5.
- **[HYDRA-UMC-EDITOR-URDF](https://github.com/JuanenRac/HYDRA-UMC-EDITOR-URDF)** — creatore/editor grafico desktop di URDF che invia i modelli finiti al catalogo di STUDIO.
- **[HYDRA-UMC-BRIDGE-AMR](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-AMR)** — barriera di coordinamento per flotte AGV/AMR tramite un publisher MQTT VDA 5050 reale.
- **[HYDRA-UMC-BRIDGE-CNC](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-CNC)** — coordinatore ad alto livello per celle CNC con accesso reale a stato/byte di controllo GRBL.
- **[HYDRA-UMC-BRIDGE-DROIDS](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-DROIDS)** — barriera di coordinamento per droidi con zampe/umanoidi, con un vero mittente di comandi per Boston Dynamics Spot.
- **[HYDRA-UMC-BRIDGE-LASER](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-LASER)** — coordinatore di sicurezza per celle laser che legge 3 salvaguardie GPIO reali di chiave/involucro/interblocco.
- **[HYDRA-UMC-BRIDGE-OPENPNP](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-OPENPNP)** — coordinatore ad alto livello sicuro per il flusso schede del pick-and-place OpenPnP.
- **[HYDRA-UMC-BRIDGE-PRINTER3D](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-PRINTER3D)** — barriera di coordinamento sicura per stampanti 3D Moonraker/Klipper, con comandi di lavoro reali e controllati.
- **[HYDRA-UMC-BRIDGE-ROS2](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-ROS2)** — coordinatore di sicurezza con un vero trasporto ROS 2 rclpy, importato in modo lazy.
- **[HYDRA-UMC-BRIDGE-UAV](https://github.com/JuanenRac/HYDRA-UMC-BRIDGE-UAV)** — barriera di coordinamento per UAV dotati di fotocamera, con un vero mittente di comandi MAVLink.

*Nodo IA Visione (Hailo-8)*
- **[HYDRA-UMC-VISION-NODE](https://github.com/JuanenRac/HYDRA-UMC-VISION-NODE)** — hub di integrazione per la pipeline di visione Hailo-8, con un vero controllo di prontezza hardware per fase.
- **[HYDRA-UMC-VISION-STREAMER](https://github.com/JuanenRac/HYDRA-UMC-VISION-STREAMER)** — generatore reale di pipeline GStreamer + config MediaMTX, con una vera barriera di integrazione HailoRT.
- **[HYDRA-UMC-VISUAL-SERVOING-API](https://github.com/JuanenRac/HYDRA-UMC-VISUAL-SERVOING-API)** — vera legge di correzione Position-Based Visual Servoing, con cancello di sicurezza sullo stato di zona a monte.
- **[HYDRA-UMC-SAFETY-ZONES](https://github.com/JuanenRac/HYDRA-UMC-SAFETY-ZONES)** — vero controllo di violazione zona e richiesta E-STOP, con imposizione della freschezza di calibrazione.

*Nodo IA Cognitivo (Hailo-10)*
- **[HYDRA-UMC-COGNITIVE-NODE](https://github.com/JuanenRac/HYDRA-UMC-COGNITIVE-NODE)** — hub di integrazione per la pipeline cognitiva Hailo-10 (orchestrazione LLM/VLA/voce).
- **[HYDRA-UMC-VLA-ENGINE](https://github.com/JuanenRac/HYDRA-UMC-VLA-ENGINE)** — vera codifica/decodifica di token d'azione e generazione di traiettoria per un modello Vision-Language-Action.
- **[HYDRA-UMC-VOICE-UI](https://github.com/JuanenRac/HYDRA-UMC-VOICE-UI)** — vero front-end vocale (VAD + parser di intenti) con un relay verso Watch limitato e soggetto a conferma.
- **[HYDRA-UMC-SEMANTIC-PLANNER](https://github.com/JuanenRac/HYDRA-UMC-SEMANTIC-PLANNER)** — vera scomposizione dei task basata su regole e recupero semantico degli errori sui codici errore MCU.
- **[HYDRA-UMC-DOCS-QA](https://github.com/JuanenRac/HYDRA-UMC-DOCS-QA)** — vera ricerca documentale TF-IDF (solo libreria standard) sui documenti Markdown di questo ecosistema.

*Orchestrazione e Sciame*
- **[HYDRA-UMC-ORCHESTRATOR](https://github.com/JuanenRac/HYDRA-UMC-ORCHESTRATOR)** — hub di integrazione con un vero contratto di health-report gRPC/Protobuf e una macchina a stati di missione.
- **[HYDRA-UMC-JOB-DISPATCHER](https://github.com/JuanenRac/HYDRA-UMC-JOB-DISPATCHER)** — vera coda di lavori basata su priorità con deduplicazione, su una vera API HTTP.
- **[HYDRA-UMC-NODE-HEALING](https://github.com/JuanenRac/HYDRA-UMC-NODE-HEALING)** — vero watchdog di salute della flotta basato su gRPC, con retry/backoff e rilevamento di discrepanza d'identità.
- **[HYDRA-UMC-PATH-PLANNER-3D](https://github.com/JuanenRac/HYDRA-UMC-PATH-PLANNER-3D)** — vero pianificatore di percorsi 3D basato su RRT, con vera validazione delle collisioni ostacolo/spazio di lavoro.
- **[HYDRA-UMC-SWARM-SYNC](https://github.com/JuanenRac/HYDRA-UMC-SWARM-SYNC)** — vera sincronizzazione di stato CRDT LWW-Element-Map, con property test per la convergenza multi-cella.

*Gemello Digitale e Simulazione*
- **[HYDRA-UMC-TWIN](https://github.com/JuanenRac/HYDRA-UMC-TWIN)** — hub di integrazione per il motore di gemello digitale, con un vero contratto di sincronizzazione per compatibilità di versione.
- **[HYDRA-UMC-HIL-BRIDGE](https://github.com/JuanenRac/HYDRA-UMC-HIL-BRIDGE)** — vero interblocco di sicurezza hardware-in-the-loop che instrada i comandi tra simulazione e hardware reale.
- **[HYDRA-UMC-PHYSICS-REPLICA](https://github.com/JuanenRac/HYDRA-UMC-PHYSICS-REPLICA)** — vera cinematica diretta e validazione dei limiti articolari su un vero sottoinsieme URDF.
- **[HYDRA-UMC-SYNTHETIC-DATA-GEN](https://github.com/JuanenRac/HYDRA-UMC-SYNTHETIC-DATA-GEN)** — vero generatore procedurale di scene 2D con esportazione di annotazioni YOLO/COCO.

*Dati e Analisi*
- **[HYDRA-UMC-DATALAKE](https://github.com/JuanenRac/HYDRA-UMC-DATALAKE)** — vero archivio di serie temporali basato su sqlite3, con una vera API HTTP di ingestione/query.
- **[HYDRA-UMC-ANOMALY-DETECTOR](https://github.com/JuanenRac/HYDRA-UMC-ANOMALY-DETECTOR)** — vero rilevatore di anomalie FFT + baseline statistica, con monitoraggio della deriva.
- **[HYDRA-UMC-PRODUCTION-REPORTS](https://github.com/JuanenRac/HYDRA-UMC-PRODUCTION-REPORTS)** — vero calcolo OEE/disponibilità sullo storico di DATALAKE, con esportazione CSV riproducibile.
- **[HYDRA-UMC-TELEMETRY-COLLECTOR](https://github.com/JuanenRac/HYDRA-UMC-TELEMETRY-COLLECTOR)** — vera pipeline di ingestione CAN/WebSocket verso DATALAKE, con deduplicazione per sequenza.

*Gateway Industriale*
- **[HYDRA-UMC-GATEWAY-INDUSTRIAL](https://github.com/JuanenRac/HYDRA-UMC-GATEWAY-INDUSTRIAL)** — hub di integrazione che inoltra ai protocolli industriali, con un vero livello di allowlist dei comandi/backpressure.
- **[HYDRA-UMC-OPCUA-SERVER](https://github.com/JuanenRac/HYDRA-UMC-OPCUA-SERVER)** — vero spazio di indirizzi OPC-UA, verificato con una vera sessione client del protocollo binario.
- **[HYDRA-UMC-MQTT-BROKER](https://github.com/JuanenRac/HYDRA-UMC-MQTT-BROKER)** — vero broker MQTT con autenticazione opzionale per client e ACL sui topic.
- **[HYDRA-UMC-MTCONNECT-ADAPTER](https://github.com/JuanenRac/HYDRA-UMC-MTCONNECT-ADAPTER)** — veri endpoint XML `/probe` e `/current` di MTConnect, con output in modalità degradata.

*Strumenti Complementari e Operazioni dell'Ecosistema*
- **[HYDRA-UMC-DASHBOARD-AI](https://github.com/JuanenRac/HYDRA-UMC-DASHBOARD-AI)** — pannelli Smart Summaries e Anomaly Highlighting su DATALAKE/ANOMALY-DETECTOR, con un fallback statistico onesto.
- **[HYDRA-UMC-TOOL-CLI](https://github.com/JuanenRac/HYDRA-UMC-TOOL-CLI)** — CLI di flotta con un vero e stabile contratto di exit-code, un client live reale della stessa API di HYDRA-UMC-SERVER.
- **[HYDRA-UMC-WATCH](https://github.com/JuanenRac/HYDRA-UMC-WATCH)** — app companion WearOS con avvisi aptici reali e un relay vocale verso il telefono abbinato.
- **[HYDRA-UMC-UPDATER](https://github.com/JuanenRac/HYDRA-UMC-UPDATER)** — strumento amministrativo desktop che scopre, clona e aggiorna ogni repository di questo ecosistema.

## 👤 AUTORE
**JuanenRac** (Electro Hobby 3D)
📧 electrohobby3d@gmail.com
📺 [youtube.com/@electrohobby3d](https://youtube.com/@electrohobby3d)

## 📜 LICENZA

URTC è (c) 2026 JuanenRac (Electro Hobby 3D). Questa nota deve essere inclusa in qualsiasi distribuzione di questo progetto o lavoro derivato.

Poiché questo progetto è composto da diversi tipi di contenuto, le singole parti sono rese disponibili sotto licenze diverse - ciascuna adatta a ciò che effettivamente copre, piuttosto che forzare un'unica licenza ad adattarsi a tutto:

1. Il **firmware** situato in `./firmware` (applicazione e bootloader CAN allo stesso modo) è disponibile sotto la **GNU General Public License v3.0 (GPL-3.0)**. Testo completo su https://www.gnu.org/licenses/gpl-3.0.html.

2. I **progetti hardware** (file schematico/board Eagle, gerber, e le parti stampabili in 3D sotto `./PCB` e `./3D`) sono disponibili sotto la **CERN Open Hardware Licence v2 - Strongly Reciprocal (CERN-OHL-S v2)**. Testo completo su https://cern-ohl.web.cern.ch/.

3. La **documentazione** (questo README e le proprie traduzioni - `README_spa.md`, `README_ita.md`, `README_fra.md`, `README_deu.md`, `README_zho.md`, `README_jpn.md` - più i file di riferimento sotto `./docs`) è disponibile sotto **Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)**. Testo completo su https://creativecommons.org/licenses/by-sa/4.0/.

Se costruisci su questo progetto, tieni presente la separazione delle licenze: le modifiche al codice del firmware dovrebbero rimanere GPL-3.0, le modifiche all'hardware dovrebbero rimanere CERN-OHL-S, e i derivati della documentazione dovrebbero rimanere CC BY-SA - ciascuno con attribuzione a questo progetto.

Questo repository copre solo il firmware e l'hardware propri della scheda URTC - gli strumenti PC (URTC Flasher, URTC Tester) che vivevano qui in precedenza sono ora progetti indipendenti con licenza propria, vedi "Strumenti PC" sopra.
