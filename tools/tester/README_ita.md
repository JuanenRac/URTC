<p align="center">
  <img src="/images/URTC_LOGO_TESTER.svg" alt="URTC Tester Logo" width="100%">
</p>

# URTC Tester (Windows / Linux)

**Versione:** 1.1 · **Autore:** JuanenRac (Electro Hobby 3D) &lt;electrohobby3d@gmail.com&gt;

Licenza: **GPL-3.0**, la stessa del firmware URTC e dello strumento di
flash - vedi `LICENSE` nella radice del repository.

Un esercitatore live del bus CAN per la scheda URTC. Si connette tramite
lo stesso adattatore USB-CAN usato dal flasher, chiede alla scheda per
quale dei suoi 12 profili strumento è attualmente configurata via jumper,
e mostra solo i controlli e la telemetria propri di quello strumento -
non una singola finestra che cerca di rappresentare tutti e 12 insieme.
Tutto ciò che fa è un comando runtime o una lettura di telemetria contro
l'applicazione in esecuzione; non tocca mai la flash, quindi non c'è
nulla qui che possa lasciare la scheda meno funzionante di come ha
iniziato.

## 1. Relazione con il flasher

Questo strumento e `tools/flasher/` condividono lo stesso layer di
trasporto (le classi SLCAN e SocketCAN sono identiche) poiché entrambi
in definitiva devono solo far entrare e uscire trame CAN dallo stesso
tipo di adattatore, ma svolgono compiti fondamentalmente diversi:

| | Flasher | Tester |
|---|---|---|
| Tocca la flash | Sì (è tutto il punto) | Mai |
| Parla con | Il bootloader, principalmente | L'applicazione in esecuzione |
| Scopo | Aggiornare il firmware | Esercitare/verificare l'hardware reale di una testa strumento |

Se non sei sicuro di quale ti serva: se la scheda sta già eseguendo
firmware e vuoi controllare che uno strumento funzioni davvero (il
riscaldatore scalda, il motore gira, il LED si accende), vuoi questo.

## 2. Installazione ed esecuzione

Stesso schema del flasher:

```
cd tools/tester
pip install -r requirements.txt
python urtc_tester.py          # Windows
python3 urtc_tester.py         # Linux
```

Oppure compila un binario standalone: `build_exe.bat` su Windows,
`./build_exe.sh` su Linux. Entrambi puliscono prima `build/`/`dist/` e
impacchettano `assets/` (il banner e l'icona) nell'eseguibile - vedi il
README stesso del flasher per il ragionamento completo dietro questi
script, poiché si applica identicamente qui.

**All'avvio**, il banner si mostra centrato sullo schermo per 5 secondi
prima che appaia la finestra principale, invece di vivere dentro la
finestra stessa - come il flasher, e per lo stesso motivo (mantiene la
finestra stessa compatta). L'icona di finestra/barra delle applicazioni
è allo stesso modo un piccolo design standalone, non il banner
rimpicciolito.

### Barra dei menu

- **File** - Salva Registri (il registro a schermo come testo semplice;
  per un pacchetto più completo che include diagnostica di sistema,
  vedi "Registri e pacchetti di debug" più sotto), ed Esci.
- **Lingua** - passa tra le 5 lingue disponibili (vedi "Lingua" più
  sopra per come funzionano le traduzioni).
- **Aiuto** - Readme (apre questo file in una finestra visualizzatore di
  sola lettura; recupera automaticamente una versione tradotta appena
  ne esiste una per la lingua corrente), GitHub di URTC (apre il
  repository del progetto nel tuo browser), Licenza (la licenza GPL-3.0
  di questo strumento, letta dal file `LICENSE` stesso del repository),
  e Informazioni (versione e autore).

### Struttura dei file

Questo strumento è organizzato in moduli per responsabilità, puramente
per leggibilità - non c'è alcuna differenza funzionale tra averli come
file separati o come uno grande. `tester_config.py` contiene le
costanti di configurazione/lingua/protocollo, `tester_transports.py`
contiene SLCAN/SocketCAN, `tester_bus_monitor.py` contiene il thread di
lettura CAN in background, e `TesterGUI` stessa è divisa tra
`tester_gui_core.py` (connessione, rilevamento, ciclo di vita della
finestra, e la barra dei menu) più 3 mixin che combina:
`tester_common_panels.py` (pannelli controlli globali/F-RAM/
espansione/autotest/monitor bus/trama personalizzata),
`tester_panel_helpers.py` (utilità condivise usate da ogni costruttore
di pannello strumento), e `tester_tool_panels.py` (gli 8 costruttori di
pannello specifici per strumento). `urtc_tester.py` ora è solo il punto
di ingresso - avvio senza CLI e la schermata di benvenuto.

**Lingua**: inglese di default. Si cambia tramite il menu **Lingua**
(nella barra dei menu in alto nella finestra) invece di un menu a
tendina nella finestra principale - cambia l'interfaccia (etichette,
pulsanti, dialoghi, e messaggi di registro) a una qualsiasi delle 5
lingue disponibili, salva immediatamente in `config.json` accanto a
questo strumento, applicato al prossimo avvio. Le traduzioni vivono in
file di testo semplice sotto `language/` (`english.lng`, `spanish.lng`,
`italian.lng`, `french.lng`, `german.lng`) come semplici coppie
`CHIAVE=Valore`, una per riga - le righe che iniziano con `#` e le righe
vuote vengono ignorate, e un `\n` letterale dentro un valore diventa un
vero a capo (usato dalla manciata di messaggi di dialogo multi-riga).
Modificabile direttamente se una traduzione necessita correzione, o
come punto di partenza per un'altra lingua (aggiungi
`language/<nome>.lng`, aggiungi `("<nome>", "Nome Nativo")` a
`AVAILABLE_LANGUAGES` vicino all'inizio di `tester_config.py`, e imposta
`"language": "<nome>"` in `config.json`). Una chiave mancante da un file
di lingua ricade nel mostrare il nome della chiave stessa invece di
andare in crash, e un file di lingua mancante o illeggibile (modifica
errata, nome file sbagliato) ricade sull'inglese per l'intera interfaccia
- in entrambi i casi lo strumento resta usabile mentre si risolve il
disallineamento.

**Configurazione SLCAN/SocketCAN su Linux** (reflash dell'adattatore,
permessi seriali, attivazione con `ip link`) è esattamente uguale alla
sezione 1 del flasher - vedi `tools/flasher/README.md` sezioni 1 e
2 invece di duplicarlo qui.

## 3. Come funziona

La finestra è disposta in 3 colonne: sinistra e centro contengono le
sezioni sempre visibili qui sotto (1-4, poi 6), destra contiene il
pannello per strumento della sezione 5, che è l'unica parte della
finestra che effettivamente cambia in base a cosa viene rilevato.
Dividere le sezioni sempre visibili su 2 colonne invece di impilarle
tutte in una mantiene la finestra dal crescere abbastanza in altezza da
non stare in uno schermo ordinario, man mano che più di queste sezioni
sono state aggiunte nel tempo. Il pannello stesso della stampante 3D (il
più alto dei 12) va un passo oltre e divide i propri controlli in 2
sottocolonne internamente, per lo stesso motivo.

**Connetti** (sezione 1, identica al flasher): scegli Seriale/SLCAN o
SocketCAN, la porta/interfaccia, opzionalmente auto-rileva il bitrate,
poi Connetti.

**Il rilevamento avviene automaticamente alla connessione** (o clicca
**Rileva** per ripeterlo): lo strumento invia `0x110` (interroga
strumento attivo) e `0x7F8` (interroga versione), e usa la risposta
per:
- Mostrare quale dei 12 profili strumento è attivo, e lo stato generale
  della scheda (qualsiasi errore dichiarato, guasto bus CAN, ancora
  nella schermata di avvio).
- Mostrare l'HardwareID e la versione firmware riportati, segnalando
  una discrepanza se non corrisponde al proprio `THIS_HARDWARE_ID` di
  questo progetto.
- Costruire il pannello **Controlli Strumento** a destra per quello
  strumento specifico - e solo quello strumento. Cambiare quale
  strumento è jumperato e rilevare di nuovo smonta il vecchio pannello e
  ne costruisce uno nuovo da zero.

**Controlli Globali** (sezione 2, sempre visibile indipendentemente da
quale strumento è attivo): l'override del colore del LED di stato, il
colore e accensione/spegnimento dell'anello LED, e la modalità display
OLED (`0x100`) - questi si applicano a ogni strumento, quindi non si
spostano nel pannello dinamico. In modalità Ispezione AOI nello
specifico, l'accensione/spegnimento dell'anello qui viene ignorato a
favore del controllo strobo proprio di quello strumento (secondo
`docs/CANBUS.TXT`) - il colore si applica comunque in entrambi i casi.

**Scheda di Espansione** (sezione 3, sempre visibile): il bus SPI
proprio di `CONN_EXPANSION` e la linea DIAG0 - nient'altro vive su
questo connettore oggi -

**F-RAM di Persistenza** (sezione 4, anch'essa sempre visibile, ma
deliberatamente separata da Scheda di Espansione sopra): la FM24CL64B
condivide il bus I2C2 hardware proprio dell'OLED - un componente
centrale della scheda, non qualcosa cablato a `CONN_EXPANSION` affatto.
Raggruppare i due insieme implicherebbe una connessione tra loro che
non è reale - il connettore di espansione stesso non ha F-RAM, né
EEPROM, niente di non volatile su di esso.
- **Passthrough SPI**: digita byte hex separati da spazi (1-7 di essi,
  es. `01 02 03`), premi Invia, e vedi esattamente cosa è tornato su
  MISO durante quello stesso trasferimento (`0x180`/`0x181`) - un
  trasporto di byte grezzo, non consapevole dei registri TMC5160, come
  l'approccio proprio del firmware. Utile per esercitare il bus stesso
  prima che valga la pena costruire un pannello dedicato per il
  protocollo di registri di una specifica scheda di espansione.
- **Livello DIAG0**: **Interroga DIAG0** legge lo stato attuale della
  linea di diagnostica stall/guasto di un TMC5160 (`0x182`/`0x183`) -
  HIGH (inattivo) o LOW (asserito). Una semplice lettura polled, non un
  valore live/spinto - premi di nuovo il pulsante per aggiornarlo.
- **F-RAM di Persistenza**: **Interroga Stato** rilegge ciò che la
  scheda ha salvato l'ultima volta prima di una perdita di alimentazione
  (`0x190`/`0x191`) - quale strumento era, il setpoint, se un errore
  critico era attivo in quel momento. **Cancella F-RAM...** la cancella
  (`0x192`, con un dialogo di conferma prima - questo non può essere
  annullato).
- **Tipo scheda di espansione**: **Interroga** mostra quale delle 5
  configurazioni possibili di `CONN_EXPANSION` è attualmente impostata
  (`0x1A1` - vedi `EXPANSION.TXT`). Sola lettura qui - impostala dalla
  sezione CAN OTA propria di `URTC Flasher` invece, poiché è un passo
  di configurazione hardware una tantum, non qualcosa da cambiare con
  leggerezza da uno strumento diagnostico live.
- **Configurazione libera dello strumento**: **Interroga** mostra la
  lettura grezza dei jumper ID (0-31) accanto a ciò che dice
  attualmente il registro `free_tool_selection` della F-RAM (`0x1A3` -
  vedi `EEPROM.TXT` sezione 5) - consultato effettivamente solo da una
  scheda i cui jumper leggono 0x1F/11111b. Sola lettura qui, stesso
  ragionamento del tipo scheda di espansione sopra - `URTC Flasher` è
  l'unico strumento che lo scrive.
- **Tipo periferica e numero di serie**: **Interroga** mostra il tipo
  di periferica fisso (sempre URTC/0x03) accanto al numero di serie del
  dispositivo attualmente impostato (`0x1A5` - vedi `EEPROM.TXT`
  sezione 6), un'etichetta assegnata dall'utente per distinguere più
  schede altrimenti identiche sullo stesso bus CAN. Sola lettura anche
  qui - `URTC Flasher` scrive il numero di serie, questo strumento lo
  legge solo indietro.

**Trama CAN Personalizzata** (sezione 6, anch'essa sempre visibile):
un'immissione di ID grezzo + byte hex con invio singolo e periodico -
per un comando che non ha ancora un proprio controllo qui, o per
testare qualcosa non (o non ancora) documentato in `docs/CANBUS.TXT`.
Nessuna validazione oltre all'intervallo ID e DLC≤8; qualsiasi cosa
questo invii è esattamente ciò che va sul bus. La stessa sezione apre
anche il **Monitor Bus Grezzo** (vedi sotto).

**Esegui Autotest** (accanto a Rileva): esegue un piccolo insieme di
controlli di comunicazione sicuri e a riposo per lo strumento
attualmente rilevato - conferma che sia l'interrogazione strumento
attivo che quella versione rispondano, poi (per strumenti con
telemetria) invia un setpoint/velocità/potenza sicuro di 0 e controlla
che arrivi la telemetria attesa. Deliberatamente non invia mai nulla
che effettivamente riscaldi, spari, o giri a potenza significativa -
questo verifica che il giro di comunicazione funzioni, non che un
attuatore risponda fisicamente, poiché confermare ciò richiede comunque
un umano che osserva. Chiede conferma prima di inviare qualsiasi cosa.
Gli strumenti senza telemetria (movimento semplice) o che sono
puramente guidati da eventi (sonda di scansione) ricevono una nota solo
informativa invece di un vero superato/fallito.

**Grafici di temperatura live**: sia i pannelli del saldatore che
dell'ugello della stampante 3D mostrano un piccolo grafico a linee
scorrevole accanto alla loro lettura di temperatura live - un semplice
widget Canvas di Tkinter, non una nuova dipendenza (matplotlib/
pyqtgraph romperebbero la politica di dipendenza zero di questo
strumento oltre pyserial). Scala asse Y fissa (0 fino al tetto di
setpoint proprio di quello strumento) invece di auto-scala, così il
trend è facile da leggere a colpo d'occhio invece che la scala si
sposti sotto di esso.

**Monitor Bus Grezzo** (aperto dalla sezione Trama CAN Personalizzata):
una finestra separata che mostra ogni trama vista, qualsiasi ID,
indipendente dal pannello strumento attivo - una tabella a scorrimento
live (Tempo/ID/DLC/Dati/Δt), Pausa/Pulisci, e una lettura approssimativa
di carico bus/frequenza trame (aggiornata una volta al secondo; la
cifra di carico non modella l'overhead di bit-stuffing, quindi trattala
come una cifra diagnostica approssimativa, non una misurazione
certificata). **Esporta .trc...**/**Esporta .asc...** salvano la
tabella attualmente mostrata come file di traccia semplificato in stile
PEAK PCAN-View / Vector CANalyzer rispettivamente - abbastanza vicino
da essere leggibile dalla maggior parte degli strumenti che si
aspettano quei formati, non garantito byte-identico a ciò che
producono le applicazioni reali. Se `urtc_custom_ids.json` esiste
accanto a questo script (opzionale, non incluso di default -
`{"0x199": "My Sensor"}`), la colonna ID mostra quel nome amichevole
accanto all'ID hex grezzo - utile per chiunque stia testando il
traffico proprio di una scheda di espansione personalizzata senza
bisogno di modificare il codice sorgente di questo strumento.

## 4. Copertura strumenti

Ognuno dei 12 profili ha il proprio pannello, costruito direttamente da
`docs/CANBUS.TXT`:

| Strumento | Controlli | Telemetria live |
|---|---|---|
| Saldatore | Temperatura di setpoint, accensione/spegnimento | Temperatura reale, endstop |
| Dispenser Pasta/Liquido, Cacciavite, entrambi i Gripper | Direzione + conteggio passi (movimento singolo) | nessuna (0x120 condiviso, nessuna telemetria per nessuno di questi 5) |
| Prelievo a Vuoto | nessuno | Lettura analogica, pezzo rilevato |
| Trapano | Velocità + direzione | RPM reale, endstop |
| Ispezione AOI | Modalità anello (spento/strobo/continuo) + periodo strobo | Endstop |
| Incisore Laser | Potenza + armato/sicuro dell'interlock | Endstop |
| Stampante 3D | Setpoint ugello, direzione/passi estrusore, potenza ventola strato, potenza ventola hotend | Temperatura hotend, RPM ventola strato, RPM ventola hotend |
| Sonda di Scansione | nessuno | Conteggio eventi impatto + timestamp (`0x095` a massima priorità) |

**I watchdog di comunicazione sono gestiti per te.** Il saldatore, il
laser, e l'ugello della stampante 3D hanno ciascuno un watchdog di
250ms nel firmware; la ventola dello strato ne ha uno di 1000ms.
Spuntare la relativa casella "Attivo" non invia semplicemente il
comando una volta - lo reinvia automaticamente (150ms per gli strumenti
con watchdog 250ms, 400ms per la ventola dello strato) finché la
casella rimane spuntata, allo stesso modo in cui un vero controller
master deve fare. Deselezionarla invia una singola trama zero/spento e
si ferma. La ventola hotend non ha watchdog (un rilevatore di stallo
invece - vedi `docs/CANBUS.TXT`), quindi è un semplice invio singolo.

## 5. Registri e pacchetti di debug

Come il flasher: un registro di sessione con marca temporale viene
scritto automaticamente in `tools/tester/logs/` (sicuro da
eliminare), ed **Esporta Pacchetto Debug** salva uno `.zip` con il
registro attuale a schermo più diagnostica di base del sistema (SO,
versione Python, trasporto/porta/bitrate attuale, strumento rilevato)
per consegnare a chi sta facendo debug di un problema di testa
strumento.

## 6. Limitazioni note

- **Non testato contro hardware reale.** Ogni pezzo qui - il layer di
  trasporto, la gestione ID CAN/layout byte, la temporizzazione
  keepalive del watchdog - è stato controllato in isolamento (trame
  simulate, un sottoprocesso reale per la temporizzazione dove
  rilevante) ma l'ambiente che ha costruito questo non ha accesso USB.
  Tratta una prima sessione reale con la stessa cautela che il README
  stesso del flasher richiede.
- **Un pannello strumento alla volta, per design**, non una limitazione
  attuale da rimuovere più avanti - vedi l'introduzione sopra per il
  motivo.
- **I colori LED globali sono un override diretto**, non una rilettura
  live - non c'è telemetria di cosa stanno effettivamente mostrando in
  questo momento i LED di stato/anello, solo cosa è stato comandato
  l'ultima volta.
