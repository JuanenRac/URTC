<p align="center">
  <img src="/images/URTC_LOGO_FLASHER.svg" alt="URTC Flasher Logo" width="100%">
</p>

# URTC Flasher (Windows / Linux)

**Versione:** 1.0 (la versione di questo strumento - mostrata nel banner
della finestra e nella barra del titolo, tracciata separatamente dalla
versione del firmware della scheda URTC che scrive)

**Autore:** JuanenRac (Electro Hobby 3D) &lt;electrohobby3d@gmail.com&gt;

Licenza: **GPL-3.0**, la stessa del firmware URTC stesso — vedi
`LICENSE` nella radice del repository. Questo copre `urtc_flasher.py` e
qualsiasi binario compilato a partire da esso.

Un piccolo strumento GUI multipiattaforma per aggiornare il firmware
della scheda URTC via bus CAN. Implementa esattamente il protocollo del
bootloader di `docs/CANBUS.TXT`: la verifica dell'HardwareID, la firma
HMAC-SHA256, il flusso di aggiornamento con slot di backup a immagine
dorata, il progresso in tempo reale tramite i messaggi di heartbeat del
bootloader, e un'interrogazione di versione (identifica se
l'applicazione *o* il bootloader di questa scheda si annuncia via CAN)
così puoi vedere cosa è attualmente installato prima di decidere cosa
flashare.

Due modi per parlare con la scheda, entrambi usando lo stesso protocollo
sottostante:

- **Seriale / SLCAN** — funziona su Windows e Linux. Serve un adattatore
  USB-CAN con firmware SLCAN, collegato come porta seriale virtuale.
- **SocketCAN** — **solo Linux**, e mostrato solo nell'interfaccia dello
  strumento su Linux. Parla direttamente con un'interfaccia di rete del
  kernel `can0`/`slcan0`. Se il tuo adattatore esegue già firmware
  `gs_usb`/candleLight (la maggior parte delle schede CANable lo fa di
  fabbrica), questo percorso **non richiede alcun reflash
  dell'adattatore** — il driver nativo di Linux lo gestisce
  direttamente.

**Stato:** il calcolo di CRC32 e HMAC-SHA256 in questo strumento è stato
verificato byte per byte contro l'implementazione C stessa del
bootloader, e il packing delle trame SocketCAN è stato verificato contro
il layout `struct can_frame` di Linux con un test di
packing/unpacking andata e ritorno. Ciò che **non** è stato testato su
nessuna delle 2 piattaforme è una scheda reale su hardware reale -
tratta il primo vero tentativo di flash con la stessa cautela che
daresti a qualsiasi nuovo strumento che parla con un bootloader: tieni a
portata di mano il JTAG come ripiego.

## 1. Fai parlare CAN al tuo adattatore

Quale di questi ti serve dipende dalla tua piattaforma e da quale
trasporto userai:

**Linux, percorso SocketCAN (consigliato se il tuo adattatore lo
supporta):**
Niente da flashare sull'adattatore stesso. Attiva l'interfaccia una
volta per avvio (o aggiungila alla configurazione di rete per farla
persistere):
```
sudo modprobe can vcan gs_usb   # gs_usb copre la maggior parte delle schede della famiglia CANable
sudo ip link set can0 type can bitrate 500000
sudo ip link set can0 up
```
Se il tuo adattatore si enumera con un nome diverso da `can0`, controlla
`ip link show` (o `dmesg` subito dopo averlo collegato) per il nome
reale. Alcuni adattatori richiedono `slcand` invece di un driver nativo
- se `ip link show` non mostra alcuna interfaccia CAN dopo averlo
collegato, questo è probabilmente il tuo caso; consulta la
documentazione del tuo adattatore per l'invocazione di `slcand`, che
crea un'interfaccia `slcan0` che poi attivi allo stesso modo di sopra.

**Windows, o Linux tramite il percorso Seriale/SLCAN:**
Una CANable Pro v2 viene spedita di default con firmware
**candleLight**, che parla con l'host usando il protocollo `gs_usb` -
lo stesso che il driver `gs_usb` di SocketCAN su Linux si aspetta
nativamente (vedi sopra). Quel protocollo **non** si presenta come
porta seriale, che è ciò di cui ha bisogno questo percorso. Per usare
Seriale/SLCAN invece (obbligatorio su Windows; opzionale su Linux):

1. Scarica firmware compatibile con SLCAN per il tuo adattatore (cerca
   "canable slcan firmware" — ci sono alcuni fork mantenuti; usa quello
   indicato dalla documentazione del tuo adattatore).
2. Metti l'adattatore in modalità DFU/bootloader (di solito un pulsante
   BOOT tenuto premuto durante l'accensione, o un jumper - controlla la
   documentazione del tuo adattatore).
3. Flasha il firmware SLCAN usando lo strumento di flash del produttore
   del tuo adattatore o `dfu-util`.
4. Ricollega - ora dovrebbe enumerarsi come porta seriale: una porta COM
   su Windows, o in stile `/dev/ttyACM0`/`/dev/ttyUSB0` su Linux.

Se il tuo adattatore esegue già firmware SLCAN, salta direttamente al
passo 2 qui sotto.

Una riga SLCAN ricevuta la cui lunghezza reale non corrisponde a ciò che
implica il suo stesso DLC dichiarato viene trattata come malformata e
saltata, invece che analizzata dai suoi primi N caratteri esadecimali
indipendentemente da ciò che segue - utile da sapere se stai facendo
debug contro un adattatore rumoroso o non standard.

## 2. Installazione ed esecuzione

**Windows:**
```
python -m pip install -r requirements.txt
python urtc_flasher.py
```
Oppure compila un `.exe` standalone con `build_exe.bat` (vedi quel
file).

**Linux:**
```
python3 -m pip install -r requirements.txt
python3 urtc_flasher.py
```
Oppure compila un binario standalone con `./build_exe.sh` (prima
`chmod +x`).

Entrambi gli script passano `--noconfirm` a PyInstaller, quindi
ricompilare sopra un `dist/URTC_Flasher` già esistente lo sostituisce
direttamente invece di aspettare un prompt "sostituire?" facile da
perdere nell'output di uno script.

### Barra dei menu

- **File** - Salva Registri (il registro a schermo come testo semplice;
  per un pacchetto più completo che include diagnostica di sistema e il
  file firmware attualmente selezionato, vedi "Diagnostica" più sotto),
  ed Esci.
- **Lingua** - passa tra le 5 lingue disponibili (vedi "Lingua" più
  sotto per come funzionano le traduzioni).
- **Aiuto** - Readme (apre questo file in una finestra visualizzatore di
  sola lettura; recupera automaticamente una versione tradotta appena
  ne esiste una per la lingua corrente), GitHub di URTC (apre il
  repository del progetto nel tuo browser), Licenza (la licenza GPL-3.0
  di questo strumento, letta dal file `LICENSE` stesso del repository),
  e Informazioni (versione e autore).

**All'avvio**, il banner si mostra centrato sullo schermo per 5 secondi
prima che appaia la finestra principale - non fa parte della finestra
principale stessa (per questo la finestra è piuttosto compatta per
quanto effettivamente fa). L'icona di finestra/barra delle applicazioni
è un piccolo design standalone (`assets/urtc_icon.png`/`.ico`), non il
banner rimpicciolito - l'illustrazione completa del banner non regge
bene a 16-32px.

**Lingua**: inglese di default.
Si cambia tramite il menu **Lingua** (nella barra dei menu in alto nella
finestra) invece di un menu a tendina nella finestra principale - salva
immediatamente in `urtc_config.json` (lo stesso file usato per gli
override tecnici dell'hardware — la preferenza della lingua vive
semplicemente accanto a quelli), applicato al prossimo avvio. Le
traduzioni vivono in file di testo semplice sotto `language/`
(`english.lng`, `spanish.lng`, `italian.lng`, `french.lng`,
`german.lng`) come semplici coppie `CHIAVE=Valore`, una per riga - le
righe che iniziano con `#` e le righe vuote vengono ignorate, e un `\n`
letterale dentro un valore diventa un vero a capo (usato dalla manciata
di messaggi di dialogo multi-riga). Modificabile direttamente se una
traduzione necessita correzione, o come punto di partenza per un'altra
lingua (aggiungi `language/<nome>.lng`, aggiungi `("<nome>", "Nome
Nativo")` a `AVAILABLE_LANGUAGES` vicino all'inizio di
`flasher_config.py`, e imposta `"language": "<nome>"` in
`urtc_config.json`). Una chiave mancante da un file di lingua ricade nel
mostrare il nome della chiave stessa invece di andare in crash, e un
file di lingua mancante o illeggibile (modifica errata, nome file
sbagliato) ricade sull'inglese per l'intera interfaccia - in entrambi i
casi lo strumento resta usabile mentre si risolve il disallineamento.

Tkinter (il toolkit della GUI) viene fornito con Python su Windows, ma
sulle distro della famiglia Debian/Ubuntu è un pacchetto del sistema
operativo separato:
```
sudo apt install python3-tk
```
(Fedora: `sudo dnf install python3-tkinter`. Arch: `sudo pacman -S tk`.)
`build_exe.sh` controlla questo da solo e ti avvisa se manca invece di
fallire a metà.

**Permessi seriali su Linux:** se stai usando il percorso Seriale/SLCAN
e la connessione fallisce con "Permission denied", il tuo utente deve
essere nel gruppo proprietario dei dispositivi seriali (`dialout` su
Debian/Ubuntu; varia su altre distro):
```
sudo usermod -a -G dialout $USER
```
Esci e rientra (l'appartenenza al gruppo viene letta al login), poi
riprova. Lo strumento rileva questo errore specifico e mostra questa
stessa soluzione in un dialogo, ma vale la pena saperlo in anticipo.
SocketCAN non ha questo problema particolare — l'accesso a
un'interfaccia tipo `can0` non è controllato dal gruppo `dialout` — ma
attivare l'interfaccia in primo luogo (passo 1 sopra) richiede comunque
`sudo`, poiché è una modifica alla configurazione del dispositivo di
rete.

Usare `python -m pip`/`python3 -m pip` invece di un `pip` semplice evita
un problema comune su entrambe le piattaforme: lo script wrapper di
`pip` stesso non è sempre nel PATH anche subito dopo un'installazione
riuscita, mentre `-m pip` trova il modulo installato direttamente.

## 3. Dove vanno i file di firmware

Questo strumento si aspetta una cartella `firmware/` **dentro
`tools/flasher/V1.1/`**, proprio accanto a `urtc_flasher.py`:

```
tools/flasher/V1.1/
├── assets/
│   ├── URTC_LOGO_FLASHER.svg      <- sorgente del banner (vettoriale)
│   └── urtc_banner.png            <- mostrato in cima alla finestra, renderizzato dal .svg sopra
├── firmware/
│   ├── URTC_v1_0_F303CC.bin      <- metti qui i nuovi file .bin
│   └── URTC_v1_0_F303CC_old.bin  <- puoi anche tenere versioni precedenti
├── logs/                          <- creato automaticamente, un file per sessione
├── urtc_config.json               <- opzionale, non incluso di default (vedi "Cambiare la chiave HMAC" sotto)
├── urtc_flasher.py                <- punto di ingresso: argomenti CLI, splash screen, impostazione finestra principale
├── flasher_config.py              <- I/O file di configurazione, caricamento lingua, costanti del protocollo
├── flasher_transports.py          <- SLCAN, SocketCAN, MockCAN
├── flasher_swd_tools.py           <- wrapper STM32CubeProgrammer / pyOCD
├── flasher_validation.py          <- validazione file firmware (.bin/.hex/.elf)
├── flasher_protocol.py            <- la macchina a stati CAN OTA stessa
├── flasher_gui.py                 <- la finestra principale (FlasherGUI) e la sua barra dei menu
├── requirements.txt
├── build_exe.bat                  <- build standalone per Windows
├── build_exe.sh                   <- build standalone per Linux
└── README.md
```

Questo strumento è organizzato nei moduli sopra per responsabilità,
puramente per leggibilità - non c'è alcuna differenza funzionale tra
averli come file separati o come uno grande, e non esiste una forma
monolitica da mantenere sincronizzata come invece accade per il firmware
(questo è uno strumento PC, non qualcosa che viene flashato sulla
scheda, quindi c'è solo questa forma).

`assets/urtc_banner.png` è opzionale - se manca, lo strumento si avvia
semplicemente senza banner invece di fallire. Viene caricato tramite il
supporto PNG nativo di tkinter (Tk 8.6+, incluso in ogni versione
attuale di Python), non Pillow, quindi non aggiunge una nuova
dipendenza. Sia `build_exe.bat` che `build_exe.sh` già impacchettano
`assets/` nell'eseguibile standalone tramite `--add-data` di
PyInstaller, quindi funziona allo stesso modo sia che tu esegua dal
sorgente sia da un binario compilato.

Questo è deliberato: mantenere `firmware/` dentro `tools/flasher/V1.1/`
invece che nella radice del repository significa che l'intera cartella
`tools/flasher/V1.1/` è autonoma. Se vuoi solo flashare una scheda - su
un PC di officina, da una chiavetta USB, ovunque - puoi copiare
`tools/flasher/V1.1/` da sola senza nient'altro dal repository, e
funziona comunque.

**Puoi tenerne più di uno `.bin` lì.** Ogni file viene controllato ed
elencato - lo strumento non prende semplicemente quello che trova. All'avvio
(e ogni volta che clicchi **Aggiorna**), ogni `.bin` in `firmware/` viene
controllato contro lo stesso test di plausibilità che il bootloader
stesso applica a un'immagine nuova (i suoi primi 4 byte devono
sembrare un puntatore di stack iniziale reale per la RAM di questo chip,
e la sua dimensione deve entrare nello slot principale). Ogni file
appare nella lista con un ✓ o ✗ chiaro e il motivo:

| File | Dimensione | Stato |
|---|---|---|
| URTC_v1_0_F303CC.bin | 30.9 KB | ✓ sembra valido |
| URTC_v1_0_F303CC_old.bin | 30.4 KB | ✓ sembra valido |
| notes.txt.bin | 0.1 KB | ✗ la prima parola non sembra un puntatore di stack valido |

- **Esattamente un file passa il controllo** → viene selezionato per te
  nel momento in cui lo strumento si avvia. Un file non valido da solo
  nella cartella *non* viene auto-selezionato solo perché niente altro
  compete con esso.
- **Più di un file valido** → nulla viene auto-selezionato; scegli
  quello che vuoi dalla lista.
- **Selezioni comunque un file che sembra non valido** → lo strumento ti
  chiede prima di confermare. Questo controllo esiste per rilevare
  errori ovvi (file sbagliato, download troncato, un segnaposto vuoto) -
  non può rilevare tutto (un file corrotto ma plausibile, o uno firmato
  con la chiave sbagliata), a cui serve il controllo CRC32/HMAC del
  bootloader stesso durante il trasferimento reale.
- **Niente trovato, o vuoi un file da tutt'altra parte** → usa il
  pulsante **Sfoglia .bin...**, che funziona indipendentemente da dove
  vive realmente il file (ed esegue lo stesso controllo di validazione
  in entrambi i casi).

**`<nomefile>.manifest.json` opzionale, accanto a un file firmware**
(es. `URTC_v1_0_F303CC.bin.manifest.json`), aggiunge un controllo di
sanità extra e non bloccante: se presente, il suo campo `sha256` viene
confrontato con il file reale appena prima di flashare, con
`version`/`build_date` registrati accanto come riferimento.

```json
{"version": "1.0", "build_date": "2026-07-23", "sha256": "e5a4918c..."}
```

Una discrepanza viene registrata come un avviso chiaro, non un blocco
totale - questo è un controllo di comodità per rilevare precocemente un
file ovviamente sbagliato o corrotto, non un sostituto della verifica
HMAC del bootloader stesso durante il trasferimento reale, che rimane
comunque il controllo autoritativo.

Aggiungere una build nuova più tardi: basta metterla in `firmware/` e
cliccare **Aggiorna** - nessun riavvio necessario.

## 4. Controllare cosa è attualmente installato

Se sei su Linux e SocketCAN è disponibile, vedrai una scelta di
**Trasporto** in alto - scegli Seriale/SLCAN o SocketCAN prima di
connettere. Su Windows questa riga non appare affatto; Seriale/SLCAN è
l'unica opzione.

Clicca **Connetti**, e lo strumento chiede automaticamente alla scheda
cosa sta eseguendo attualmente (CAN ID `0x7F8` → `0x7F9` - vedi
`docs/CANBUS.TXT`). Questo funziona sia che la scheda stia eseguendo
normalmente la sua applicazione *sia* che si trovi nel bootloader, quindi
non serve provocare un reset solo per scoprirlo. Clicca **Interroga**
in qualsiasi momento dopo per ricontrollare (utile subito dopo che un
flash è completato, per confermare che la nuova versione sia
effettivamente stata applicata).

**Quando è il bootloader stesso a rispondere** (scheda ferma nel
bootloader, non in esecuzione della sua applicazione), riporta anche la
propria versione - una cosa separata dalla versione dell'applicazione
installata, tracciata tramite il proprio `BOOTLOADER_VERSION_MAJOR/
MINOR/PATCH` in `BOOTLOADER.C` e inviata come seconda trama (`0x7FA`)
proprio accanto a `0x7F9`. L'applicazione in esecuzione non invia mai
questo - non ha modo di sapere la versione di un bootloader attualmente
flashato se non chiedendolo al bootloader stesso, quindi questo appare
solo quando la scheda è effettivamente lì (subito dopo `0x7F0`, o a un
avvio nuovo prima che salti all'applicazione).

Cosa vedrai:

- **`v1.0 (application, HardwareID 0x0303CC01)`** - caso normale,
  applicazione in esecuzione, tutto corrisponde.
- **`Bootloader running, no valid firmware currently installed,
  bootloader v1.0.0`** - la scheda è bloccata nel bootloader senza
  niente a cui saltare (chip vuoto, o ogni controllo sullo slot
  principale è fallito). Questa è esattamente la situazione per cui
  esiste questo strumento - flashala. La versione di bootloader
  mostrata qui è quella del bootloader stesso, senza relazione con
  qualsiasi versione di applicazione che abbia fallito i suoi controlli.
- **`⚠ HardwareID mismatch!`** mostrato in rosso - qualcosa ha risposto,
  ma il suo HardwareID non corrisponde a ciò che si aspetta questo
  strumento. Non flashare senza prima capire perché; il bootloader
  rifiuterebbe comunque l'aggiornamento, ma una discrepanza qui può
  anche significare che stai puntando alla scheda sbagliata del tutto.
- **Nessuna risposta** (rosso) - scheda non risponde, bitrate sbagliato,
  o non effettivamente collegata. Controlla la connessione fisica e, sul
  percorso SocketCAN, che l'interfaccia sia effettivamente attiva
  (`ip link show`).

**Scheda di espansione:** un menu a tendina separato e una coppia
Interroga/Salva, subito sotto il controllo versione. Legge e imposta
quale delle 5 configurazioni possibili di `CONN_EXPANSION` (nessuna, o
una delle 4 varianti pianificate - vedi `EXPANSION.TXT`) è fisicamente
installata, via CAN (`0x1A0`/`0x1A1`). Non c'è modo elettrico per la
scheda di rilevarlo da sola, quindi va detto - questo vive qui (non solo
in `URTC Tester`) poiché è un passo di configurazione hardware una
tantum fatto più naturalmente insieme a un aggiornamento firmware.
**Salva** chiede prima conferma, poiché questo persiste tra i cicli di
accensione finché non viene esplicitamente cambiato di nuovo.

## 5. Flashare

1. **Connetti**: scegli Seriale/SLCAN o SocketCAN (solo Linux), poi la
   porta/interfaccia, poi clicca Connetti. Per Seriale/SLCAN questo apre
   il canale CAN a 500 kbit/s (la velocità di bus fissa di URTC); per
   SocketCAN si presume che l'interfaccia sia già a quella velocità
   (passo 1 sopra) - questo strumento non la imposta. In entrambi i
   casi, la versione attuale viene interrogata automaticamente - vedi
   sezione 4 sopra.
2. **Seleziona firmware**: scegli dalla lista rilevata, o Sfoglia - vedi
   sezione 3 sopra per sapere esattamente come funzionano rilevamento e
   validazione.
3. **Flasha**:
   - Lascia spuntato "La scheda sta attualmente eseguendo l'applicazione"
     se la scheda è accesa e funziona normalmente - lo strumento invia
     prima il trigger a payload magico `0x7F0`, che spegne in sicurezza
     ogni attuatore prima di resettare verso il bootloader.
   - Deseleziona se la scheda è già nel bootloader (subito dopo un
     flash JTAG nuovo, o se il controllo versione sopra ha mostrato
     "no valid firmware currently installed").
   - Clicca **Flasha Firmware** e conferma. Il registro mostra ogni
     passo del protocollo; la barra di progresso segue il progresso di
     scrittura pagina per pagina durante il trasferimento, poi il
     progresso di copia durante la copia finale da backup a principale.

Se la verifica fallisce in qualsiasi punto (discrepanza CRC32, HMAC, o
HardwareID), lo slot principale del bootloader non viene mai toccato -
la scheda continua a eseguire il firmware che aveva già. È sempre sicuro
semplicemente riprovare.

## 6. Programmare il chip completo via SWD/JTAG (avanzato)

La sezione "4. Program complete chip via SWD/JTAG" nello strumento fa un
flash completo di avvio - cancella l'intero chip in massa, poi scrive
da zero sia l'immagine del bootloader (`0x08000000`) che quella
dell'applicazione (`0x08008000`). Questo è un **tipo di operazione
diverso** dalle sezioni 1-5 sopra:

|  | Aggiornamento CAN OTA (sezioni 1-5) | SWD/JTAG chip completo (sezione 6) |
|---|---|---|
| Auto-riparante se interrotto | Sì - lo slot di backup a immagine dorata garantisce che il firmware in esecuzione sopravviva | No - non eseguirà nulla finché non viene riprogrammato |
| Recuperabile | Automaticamente, nessuna azione necessaria | Sì - basta ricollegare e flashare di nuovo via SWD; la porta di debug non dipende dal contenuto della flash. Solo un vero blocco permanente (option byte RDP2) impedirebbe questo, e niente in questo strumento imposta option byte |
| Tocca il bootloader | Mai | Sì, per design |
| Richiede | Un adattatore USB-CAN | Una sonda SWD/JTAG (ST-Link o simile) |
| Uso tipico | Aggiornamenti firmware di routine | Primo avvio su un chip vuoto, o recupero di una scheda inutilizzabile |

**Richiede uno tra** (lo strumento rileva automaticamente quale è
disponibile e abilita solo quelli che trova):
- **pyOCD** - `pip install pyocd`. Libero, open-source, nessuna
  installazione separata oltre al pacchetto pip.
- **STM32CubeProgrammer** - lo strumento ufficiale di ST, installato
  separatamente da [st.com](https://www.st.com). Se lo hai già per
  altri lavori STM32, non serve installare nient'altro qui.

Entrambi vengono eseguiti come sottoprocessi da riga di comando, non
importati come librerie Python - vedrai il comando esatto registrato
prima che venga eseguito.

**Formati file:** `.bin` (richiede l'indirizzo fisso che questo
strumento già conosce - non lo inserisci tu) o `.hex` (porta il proprio
indirizzo, usato così com'è). Mescolare va bene - bootloader come
`.hex` e applicazione come `.bin`, o viceversa, entrambi funzionano.
Entrambi i selettori file validano il file scelto (dimensione plausibile
per lo slot di destinazione, e - dove il formato permette di
controllarlo con sicurezza - un puntatore di stack iniziale plausibile)
prima di lasciarti procedere, allo stesso modo in cui già faceva il
selettore firmware del percorso CAN.

**La connessione viene controllata prima che venga eseguito nulla di
distruttivo**, richiedendo **evidenza positiva** di una sonda/target
reale invece della sola assenza di un errore - il codice di uscita
stesso di STM32CubeProgrammer non è un segnale affidabile di
successo/fallimento da solo, quindi un controllo di connessione dedicato
(`pyocd list --probes`, o un `-c port=SWD` solo-connessione per
STM32CubeProgrammer) viene eseguito prima che il passo di cancellazione
in massa lo faccia mai. L'output di ogni comando successivo viene anche
esaminato per testo di fallimento noto come seconda protezione, nel caso
il codice di uscita di uno strumento da solo non sia affidabile in
qualche altra situazione.

**Il dry run è attivo di default.** La prima volta, lascialo spuntato e
premi "Flash Complete Chip" - stampa i comandi esatti nel registro senza
toccare la scheda. Leggili, conferma che percorsi e indirizzi sembrino
corretti, *poi* deseleziona il dry run e fallo per davvero.

**"Back up entire flash before erasing"** legge prima l'intera regione
flash da 256KB in un file `.bin`, tramite il comando stesso di
lettura-memoria-su-file dello stesso strumento (`-r` per
STM32CubeProgrammer, `commander savemem` per pyOCD) - un'assicurazione
reale qui, poiché a differenza di un aggiornamento CAN OTA (che lo slot
di backup a immagine dorata già protegge), una cancellazione completa
del chip non ha altro annullamento. Disattivato di default poiché
aggiunge 10-30s e non serve su un chip nuovo/vuoto; vale la pena
spuntarlo prima di sovrascrivere una scheda che sta già eseguendo
qualcosa. Se la lettura non produce effettivamente un file, la
cancellazione viene rifiutata invece di procedere senza il backup che
hai richiesto.

**Stato dei test:** la logica di controllo connessione sopra è stata
verificata contro output reale di STM32CubeProgrammer (sia un genuino
log di connessione riuscita sia un fallimento documentato "No target
connected", entrambi provenienti dal forum comunitario stesso di ST) e
contro lo scenario esatto di falso successo che ha colpito un utente
reale. La sequenza completa di cancellazione/programmazione/verifica
contro un vero ST-Link e un vero STM32F303CC non è ancora stata
esercitata end-to-end - l'ambiente che ha scritto questo non ha accesso
USB. Tratta un primo vero tentativo completo con la dovuta cautela -
prima una scheda di riserva/test se ne hai una, e tieni presente un
piano di riserva (lo strumento di flash stesso di STM32CubeIDE, o
`st-flash`) nel caso qualcosa della tua specifica versione di pyOCD o
sonda non corrisponda a ciò che si assume qui.

## 7. Modalità CLI (senza schermo, no GUI)

Per pipeline CI, banchi di prova, o scripting da linea di produzione
dove non c'è display:

```
python3 urtc_flasher.py --cli --port /dev/ttyACM0 --file firmware.bin
```

```
usage: urtc_flasher.py --cli [-h] [--transport {serial,socketcan}] --port PORT
                             --file FILE [--no-trigger] [--force]
```

Codici di uscita: `0` successo, `1` errore protocollo/connessione, `2`
argomenti errati o un file firmware che fallisce la validazione (passa
`--force` per flasharlo comunque), `130` annullato con Ctrl+C. Copre
solo il percorso di aggiornamento CAN OTA (sezioni 1-3) - il percorso
completo SWD/JTAG del chip è deliberatamente solo-GUI per ora, data
quanto più è in gioco se un'esecuzione scriptata ottiene una
combinazione file/target sbagliata senza che nessuno stia osservando.

**`--transport mock`** esegue l'intera sequenza di aggiornamento contro
un bootloader simulato, in memoria, invece di una scheda reale - nessun
adattatore, nessuna porta, niente di fisico coinvolto:

```
python3 urtc_flasher.py --cli --transport mock --file firmware.bin --no-trigger
```

Utile per testare la logica propria di questo strumento (comportamento
di retry, gestione timeout, codici di uscita) in una pipeline CI o prima
di toccare hardware reale - non qualcosa che parla con una scheda reale.
`--mock-fail 0x03` (o qualsiasi altro valore `VERIFY_FAIL_REASON_*` da
`docs/CANBUS.TXT`) fa fallire la verifica dell'aggiornamento simulato
invece di avere successo, per testare il percorso di fallimento allo
stesso modo.

## 8. Affidabilità durante un aggiornamento CAN, e log di sessione

Se l'ACK di una pagina non arriva entro la normale finestra di 3s
durante un aggiornamento CAN, lo strumento riprova l'*attesa* (non un
reinvio dei dati della pagina) fino a due volte in più con un breve
backoff prima di arrendersi, recuperando da un ACK ritardato o perso su
un bus rumoroso senza che i dati sottostanti siano andati perduti.
Deliberatamente non reinvia i dati della pagina a un timeout - se i dati
originali sono effettivamente arrivati bene e si è perso solo l'ACK,
reinviare farebbe leggere al bootloader quei byte come l'inizio della
*prossima* pagina, desincronizzando il trasferimento. Ogni retry
controlla anche l'heartbeat stesso del bootloader (inviato
approssimativamente una volta al secondo) contro ciò che implicherebbe
aver ricevuto la pagina corrente per intero - quando sono coerenti, il
registro lo dice, il che è evidenza reale che i dati sono passati e si è
perso solo l'ACK, non solo un'attesa più lunga e una speranza.

Ogni sessione scrive anche un file di log con marca temporale in
`tools/flasher/V1.1/logs/` (`urtc_flasher_YYYYMMDD_HHMMSS.log`),
indipendente dal registro a schermo - utile per consegnare una traccia
completa a chi ha scritto il firmware se qualcosa va storto sul campo.
Questa cartella viene creata automaticamente ed è sicura da eliminare;
nulla rilegge vecchi log.

## 9. Diagnostica — attività bus, bitrate, e pacchetti di debug

**Selettore bitrate + auto-rilevamento** (solo Seriale/SLCAN): il bus di
URTC è fisso a 500 kbit/s, che rimane il default - questo serve per un
adattatore mal configurato o per il debug di una scheda non standard.
**Auto-rilevamento** prova ogni bitrate SLCAN standard a turno contro
un'interrogazione di versione e si ferma al primo che ottiene una vera
risposta; non ancora connesso quando ci clicchi. Il bitrate di SocketCAN
è impostato a livello di sistema operativo (`ip link`), quindi questo
controllo è disabilitato per quel trasporto - non c'è nulla qui da
provare.

**Attività bus** ("Controlla (2s)", accanto a Interroga): conta trame di
protocollo reali effettivamente viste durante una finestra fissa di 2
secondi sul trasporto connesso. Questo deliberatamente **non** è la
stessa cosa di una vera percentuale di carico bus CAN o dei contatori di
errore propri del controller (REC/TEC) - quelli richiedono
un'interrogazione netlink (SocketCAN) o estensioni specifiche
dell'adattatore (SLCAN) che questo strumento non ha un modo standard e
senza dipendenze per ottenere. Ciò che dà: un segnale genuino,
direttamente misurato, di "c'è qualcosa che parla su questo bus, e
approssimativamente con quale frequenza", su entrambi i trasporti. Per
SocketCAN nello specifico, mostra anche il delta di 2 secondi delle
statistiche di interfaccia stesse di Linux
(`/sys/class/net/<iface>/statistics/`) - contatori base rx/tx/error/drop
che ogni interfaccia espone, letti come file semplici, nessuna
dipendenza extra. Connettersi via SocketCAN legge anche
`/sys/class/net/<iface>/carrier` - un file semplice 0/1 che ogni
interfaccia Linux espone. Quando un controller CAN va in bus-off, il
driver del kernel chiama `netif_carrier_off()`, quindi "nessun carrier"
qui è evidenza reale di un bus-off o link similmente morto, registrato
come avviso con il comando esatto di ripristino (`sudo ip link set
<iface> down && sudo ip link set <iface> up type can bitrate 500000
restart-ms 100`). Questo strumento non esegue quel comando da solo -
ripulire un vero bus-off richiede abbassare e rialzare l'interfaccia a
livello kernel, il che richiede root e conta come modifica alla
configurazione di rete di sistema, non qualcosa da fare silenziosamente
per tuo conto.

**Esporta Pacchetto Debug** (sopra il registro): salva uno `.zip` con il
registro attuale a schermo, diagnostica di base del sistema (SO,
versione Python, quali strumenti sono stati trovati, trasporto/porta/
bitrate attuale), e il file firmware CAN attualmente selezionato - utile
per consegnare un quadro completo a chi ha scritto il firmware se
qualcosa va storto sul campo, invece di copiare il registro
a mano.

## 10. SWD/JTAG — formati file, verifica slot, e selezione sonda

**Formati file**: i selettori bootloader/applicazione della sezione SWD
accettano `.bin`, `.hex`, e `.elf`/`.axf`. ELF/AXF viene analizzato con
una piccola quantità di unpacking di struct scritto a mano (solo header
ELF + program header - niente simboli, niente section header),
deliberatamente senza usare `pyelftools`: questo progetto rimane a zero
dipendenze fuori dalla libreria standard, e il parsing ELF completo è
più di quanto serva a questo specifico controllo di plausibilità.
Verificato contro il `BOOTLOADER.elf`/`APP.elf` compilato reale di
questo progetto stesso - entrambi validano correttamente ai loro
indirizzi di caricamento reali (`0x08000000`/`0x08008000`), non solo
file di test sintetici. Solo ARM little-endian a 32 bit, che è tutto ciò
che un target Cortex-M è mai. La dimensione dichiarata di un file `.hex`
è il conteggio reale di byte occupati, non l'intervallo di indirizzi
dal suo record più basso al più alto - quindi un file sparso (un
piccolo blocco di firmware reale più un blocco distante e separato di
option byte o dati di calibrazione, che alcuni toolchain STM32
raggruppano in un'unica esportazione) valida sul suo contenuto reale
invece che sul divario tra loro. Un'immagine firmware grezza sotto
un'estensione diversa da `.bin` (`.img`, `.rom`, o nessuna estensione -
selezionabile tramite l'opzione "Tutti i file" del selettore file)
ottiene il suo indirizzo base da in quale slot lo stai caricando, allo
stesso modo di un `.bin`.

**Verifica slot bootloader/applicazione**: i selettori file verificano
che ogni immagine sia pensata per lo slot in cui viene messa, non solo
che sembri firmware valido di *qualche* tipo. Un'immagine bootloader e
una applicazione hanno un puntatore di stack ugualmente plausibile -
stesso chip, stessa RAM - quindi quel controllo da solo non può
distinguerle se una finisce nello slot dell'altra. Ciò che può: il
**gestore di reset** di un'immagine linkata è un indirizzo assoluto e
reale fissato al momento del linking, e punta solo dentro la regione
per cui è stata effettivamente linkata. Verificato contro il
`BOOTLOADER.bin`/`APP.bin` compilato reale di questo progetto stesso: i
loro gestori di reset sono `0x080030F1` e `0x0800C725` rispettivamente,
ciascuno correttamente dentro l'intervallo di indirizzi del proprio slot
e fuori da quello dell'altro - quindi mettere uno dei due nello slot
sbagliato viene rilevato e bloccato, non accettato in silenzio. La
stessa logica si applica a `.hex`/`.elf`, controllati contro il proprio
indirizzo di caricamento incorporato.

**Controlla Option Bytes** (sezione 4, solo STM32CubeProgrammer - pyOCD
non espone questo allo stesso modo via CLI): un dump di sola lettura
`-ob displ`, senza cancellazione/scrittura. Segnala il livello RDP con
la stessa cura che tutto questo strumento prende riguardo al rischio
SWD:
- **RDP0** - nessuna protezione, normale per una scheda di sviluppo.
- **RDP1** - reversibile tramite il Read Unprotect di CubeProgrammer, ma
  quello cancella il chip in massa come parte della rimozione - non
  qualcosa che questo strumento fa per te automaticamente.
- **RDP2** - l'unico blocco genuinamente **permanente** di tutto questo
  progetto. A differenza di ogni altro rischio documentato sopra (tutti
  recuperabili via SWD), RDP2 disabilita la porta di debug per sempre
  per design dello stesso ST. Questo controllo esiste per rilevarlo
  prima di un'operazione a chip completo, non dopo.

**Selezione sonda** (sezione 4): se più di uno ST-Link/sonda è connesso
alla volta, ogni comando richiede di sceglierne uno esplicitamente dal
menu a tendina Sonda - niente "quello che il SO enumera per primo". Con
esattamente una sonda connessa, viene auto-selezionata; con zero o
diverse, premi Aggiorna e scegli. Questo si applica sia al flash a chip
completo che al controllo option byte, poiché entrambi sono
sufficientemente vicini al distruttivo perché indovinare la scheda
sbagliata sia un rischio reale su un banco con più dispositivi.

**Le scritture di pyOCD sono verificate con una rilettura esplicita**,
non solo fidandosi del codice di uscita. Il comando `flash` stesso di
pyOCD salta la riscrittura di pagine che già corrispondono
(un'ottimizzazione di velocità, non un rapporto di verifica), quindi
questo strumento aggiunge un passo `commander compare` contro entrambe
le immagini dopo la scrittura - un vero controllo byte per byte, come
già fa il flag `-v` di STM32CubeProgrammer. Solo per `.bin`: `compare`
controlla il contenuto della flash contro i byte grezzi del file, il
che non corrisponderebbe correttamente alla codifica propria di un file
`.hex`/`.elf` anche dopo un flash riuscito, quindi quei 2 formati
saltano questo specifico passo e si affidano invece alla verifica
interna in tempo di scrittura propria di pyOCD.

## 11. Telemetria di trasferimento e dettaglio fallimento verifica

**Telemetria di trasferimento**: il registro mostra i KB/s effettivi e
il tempo trascorso per pagina durante un aggiornamento CAN, più una riga
di riepilogo alla fine (tempo totale, KB/s medio, quanti retry di ACK
pagina sono avvenuti). Puramente informativo - non cambia il
comportamento di flash, rende solo più facile distinguere a colpo
d'occhio "questo va solo lento" da "qualcosa non va davvero".

**Motivi specifici di fallimento verifica**: se la verifica fallisce
durante un aggiornamento CAN, `BOOTLOADER.C` invia un byte motivo
accanto allo stato `0x05` (verifica fallita) - trasferimento
incompleto, discrepanza CRC32, discrepanza HMAC, o discrepanza
HardwareID, invece che ogni fallimento sembri identico. Vedi
`docs/CANBUS.TXT` per il formato esatto della trama (`0x7F5`, DLC 2 per
questo stato specifico). Questo strumento e il bootloader concordano su
questo formato di trama, quindi flasha entrambi insieme se stai
costruendo un bootloader personalizzato con una versione diversa del
protocollo.

## 12. Cancellazione opzionale della F-RAM prima di flashare

La sezione 3 ha una casella di controllo, **"Cancella anche la F-RAM di
persistenza prima di flashare"** - disattivata di default. Se spuntata,
invia il comando di cancellazione a payload magico (`0x192` - vedi
`docs/CANBUS.TXT`) alla F-RAM di persistenza FM24CL64B della scheda
prima che inizi la sequenza di aggiornamento, cancellando qualsiasi
stato di parametri strumento avesse salvato.

**Non necessario per un aggiornamento normale.** Una discrepanza di
versione nel layout stesso del record salvato viene già rilevata e
ignorata in sicurezza al prossimo avvio (vedi la sezione persistenza
parametri di `src/F303-master/V1.1/README.md`) - questa casella esiste
per una pulizia genuinamente completa, non perché saltarla lascerebbe
qualcosa di rotto.

**Funziona solo mentre l'applicazione è in esecuzione** - il bootloader
stesso non gestisce affatto `0x192`, lo fa solo `STM32F303CC.C`. Questa
casella viene saltata in silenzio (con una riga di registro che spiega
perché) se la casella "la scheda sta attualmente eseguendo
l'applicazione" sopra non è spuntata, poiché in quel caso si presume che
la scheda sia già nel bootloader.

**Una conferma mancante non ferma il flash.** Se la trama di conferma
stessa del comando di cancellazione non torna entro 2 secondi, questo
viene registrato come avviso e l'aggiornamento firmware reale procede
comunque - cancellare è un passo secondario e opzionale accanto allo
scopo reale di questo strumento, non qualcosa che dovrebbe interrompere
un aggiornamento altrimenti riuscito per l'assenza della sua propria
trama di conferma. Controlla lo stato della F-RAM separatamente (il
pulsante Interroga Stato stesso di `URTC Tester`) se questo ti importa.

## Cambiare la chiave HMAC / l'HardwareID

La chiave di firma condivisa vive in 2 posti che devono sempre
corrispondere: l'array `HMAC_KEY` di `BOOTLOADER.C`, e la costante
`HMAC_KEY` di questo strumento vicino all'inizio di `urtc_flasher.py`.
Se ne cambi una, cambia l'altra e ricompila/reflasha il bootloader prima
di provare a firmare qualsiasi cosa con la nuova chiave - un'immagine
firmata con una chiave che il bootloader non ha fallirà sempre la
verifica, in sicurezza, lasciando lo slot principale intatto.

**Oppure sostituisci qualsiasi cosa di questo senza toccare lo script:**
un `urtc_config.json` opzionale accanto a `firmware/` può impostare la
chiave di firma, l'HardwareID, e i valori della mappa di memoria - utile
per una revisione scheda diversa, una chiave ruotata, o (per i campi
della mappa di memoria) adattare questo strumento a una variante di chip
o schema di partizioni diverso, senza bisogno di una versione di script
nuova per ogni distribuzione:
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
Ogni campo è opzionale - sostituisci solo ciò che effettivamente sta
cambiando. Un file mancante ricade in silenzio sui default compilati; un
file presente ma rotto registra un avviso e ricade anch'esso su quei
valori, invece di far crashare lo strumento per un errore di battitura.
Quale fonte è attiva viene registrato all'avvio, quindi è sempre visibile
quali valori una data sessione ha effettivamente usato. `hardware_id`
accetta sia una stringa JSON (`"0x0303CC01"`) che un numero JSON semplice
(`50580689`) - quale sia più naturale a seconda di come viene generato
il file. `app_max_size`, `bootloader_max_size`, `flash_page_size`,
`bootloader_flash_addr`, e `app_flash_addr` sono anch'essi sostituibili
qui, accanto alla chiave di firma e all'HardwareID sopra - utile se
questo strumento viene mai adattato a una variante di chip o schema di
partizioni diverso.
