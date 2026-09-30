# MRX Loader - Guida in italiano

[Leggi questa guida in inglese](README.md).

<p align="center"><img src="icon.png" alt="Logo MRX Loader" width="240"></p>

MRX Loader è un firmware per Adafruit Feather RP2040 USB Host (prodotto 5723)
insieme a un'applicazione desktop PySide6. Il Feather si collega al PC tramite
USB-C per la gestione e usa la porta USB-A host integrata quando lavora in
modalità standalone. Non servono periferiche o collegamenti esterni ai pin del
Feather.

L'applicazione desktop è multipiattaforma e funziona sia su Windows sia su
Linux dallo stesso sorgente.

Versione di sviluppo attuale: **0.1.0** per firmware e applicazione desktop.
È una versione di sviluppo, non una release pubblica.

## Origine del progetto

Lavoro a MRX Loader da marzo 2026. L'idea è nata quando ho trovato un RCM
loader che costava circa 20 €, ma il suo `payload.bin` non era configurabile.
Mi sono chiesto: perché spendere 20 € per un dispositivo che non posso
personalizzare, quando con una cifra simile posso avere qualcosa che permette
di conservare più payload, sceglierli e gestirli? Da questa domanda è partita
l'idea di MRX Loader.

<p align="center"><img src="docs/mrx-gui-preview.png" alt="Anteprima dell'applicazione MRX Loader" width="1000"></p>

## Avvertenza

<div style="color: #e00020; border: 2px solid #e00020; padding: 12px; border-radius: 8px">
<strong>AVVERTENZA E LIMITAZIONE DI RESPONSABILITÀ:</strong> Non sono responsabile per danni ai dispositivi,
perdita di dati, hardware corrotto o inutilizzabile, payload non avviati,
conseguenze legali o altri problemi causati dalla compilazione, modifica,
programmazione o uso di questo progetto. Lo usi a tuo rischio, solo con
hardware e software che possiedi o che sei autorizzato a usare, e nel rispetto
delle leggi e dei termini applicabili. Il progetto è fornito così com'è e senza
garanzie. La Switch non fornisce una conferma USB affidabile dell'avvio del
payload; con Hekate, la conferma visibile è la schermata di Hekate stessa.
</div>

## Uso, modifiche e crediti

Puoi usare, copiare e modificare MRX Loader, anche per i tuoi progetti. Se
condividi o ridistribuisci il progetto, o una sua versione modificata, mantieni
un credito chiaro a **MRX Loader e al suo autore originale (il proprietario di
questo progetto)**, conserva le note di copyright e le licenze di terze parti,
e indica le modifiche che hai apportato. Non attribuirti il lavoro originale o
quello di altri. Questa autorizzazione non sostituisce le licenze dei componenti
di terze parti riportate in [Avvisi di licenza di terze parti](#avvisi-di-licenza-di-terze-parti).

## Funzionalità

- Il firmware espone i comandi di gestione tramite USB CDC, memorizza i payload
  in LittleFS, li verifica con SHA-256 e supporta l'aggiornamento tramite il
  bootloader ROM BOOTSEL dell'RP2040.
- All'avvio GPIO18/VBUS resta LOW. Il firmware abilita l'alimentazione USB-A
  solo dopo i controlli richiesti, inclusa la validità del payload selezionato.
- La GUI permette di elencare, caricare, verificare, selezionare ed eliminare
  payload e guida all'aggiornamento del firmware.
- Il trasferimento RCM è implementato come percorso esplicito del firmware.
  Dopo il trasferimento USB non può confermare che Hekate sia partito: il LED
  quindi non dichiara un successo che il firmware non può verificare.

## Stato della verifica hardware

Risultati ottenuti su hardware reale:

- **Programmazione manuale del UF2 tramite BOOTSEL ROM.** Il UF2 di sviluppo
  è stato copiato sull'unità `RPI-RP2`, la scheda si è riavviata e il probe CDC
  `GET_INFO` ha riportato MRX Loader firmware 0.1.0.
- **Avvio di Hekate dal loader standalone.** Il percorso RCM via USB-A è stato
  provato con Hekate, che è partito sul target. Poiché la Switch smette di
  rispondere su USB appena l'esecuzione inizia, la conferma visibile del
  successo è la schermata di Hekate.

I risultati lato host e lato compilazione sono riportati separatamente in
[Stato di verifica](#stato-di-verifica). L'identificativo USB `1209:0001` è
provvisorio: prima di una release pubblica serve un PID assegnato al progetto.

## Avvio rapido

### 1. Compilare il firmware

Servono CMake, Ninja, Git, il Raspberry Pi Pico SDK con il suo submodule
TinyUSB e la toolchain Arm GNU Embedded.

Linux:

```bash
export PICO_SDK_PATH=/percorso/pico-sdk
cmake -S firmware -B firmware/build -G Ninja \
  -DPICO_BOARD=adafruit_feather_rp2040_usb_host \
  -DPICO_TOOLCHAIN_PATH=/percorso/arm-gnu-toolchain
cmake --build firmware/build --parallel
```

Windows (PowerShell):

```powershell
$env:PICO_SDK_PATH = 'C:\percorso\pico-sdk'
cmake -S firmware -B firmware/build -G Ninja `
  -DPICO_BOARD=adafruit_feather_rp2040_usb_host `
  -DPICO_TOOLCHAIN_PATH='C:\percorso\arm-gnu-toolchain'
cmake --build firmware/build --parallel
```

Il UF2 viene scritto in `firmware/build/mrx_loader.uf2`. Il linker limita il
firmware a 1 MiB; LittleFS usa i restanti 7 MiB della flash dell'RP2040.

### 2. Avviare l'applicazione desktop

Serve Python 3.10 o successivo.

Linux:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r tools/mrx_gui/requirements.txt
python -m tools.mrx_gui.app
```

Windows (PowerShell):

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r tools\mrx_gui\requirements.txt
python -m tools.mrx_gui.app
```

Collega il Feather al PC tramite la porta USB-C. Se non è collegata alcuna
scheda, l'app si apre nello stato non connesso. L'app preferisce il VID/PID di
sviluppo `1209:0001` e prova anche le altre porte seriali con la risposta MRX
`GET_INFO`, quindi l'identificazione non dipende da un identico USB specifico.

Su Linux l'account utente deve poter aprire il dispositivo CDC. Se compaiono
errori di permesso, aggiungi una regola udev per il dispositivo oppure esegui
l'app con permessi sufficienti.

### 3. Programmare il UF2 sul Feather

Collega il Feather via USB-C e scegli **Update firmware** nella GUI. Dopo la
conferma dell'immagine l'app richiede BOOTSEL. Copia
`firmware/build/mrx_loader.uf2` nella radice dell'unità `RPI-RP2` e attendi che
l'unità scompaia. La scrittura la esegue il bootloader ROM dell'RP2040: il
firmware in esecuzione non sovrascrive sé stesso. Se l'ingresso automatico in
BOOTSEL fallisce, usa la procedura BOOTSEL fisica della scheda e copia
manualmente un UF2 noto valido.

### 4. Eseguire le suite di test host

```bash
python tools/test_mrx_protocol.py
python tools/test_mrx_gui_pty.py
```

`test_mrx_protocol.py` verifica framing del protocollo, recupero, parsing dei
pacchetti lato desktop, ciclo di vita di storage e payload su flash simulata,
validazione UF2 e il limite di 1 MiB del linker. La suite compila
direttamente i sorgenti C del firmware e non può verificare il
comportamento hardware.

`test_mrx_gui_pty.py` usa il vero worker di rilevamento della GUI contro un
dispositivo MRX finto servito su uno pseudo-terminale. Funziona su Linux e
macOS e viene saltato su Windows, dove gli pseudo-terminali non esistono.

Per il probe CDC separato contro un Feather fisico:

```bash
python tools/probe_mrx_cdc.py
```

## Indice

- [Avvio rapido](#avvio-rapido)
- [Dettagli di build, trasporto CDC, RCM e GUI](#dettagli-di-build-trasporto-cdc-rcm-e-gui)
- [Specifica dell'architettura di alimentazione](#specifica-dellarchitettura-di-alimentazione)
- [Specifica del protocollo MRX](#specifica-del-protocollo-mrx)
- [Specifica dell'architettura storage](#specifica-dellarchitettura-storage)
- [Stato di verifica](#stato-di-verifica)
- [Limitazioni note](#limitazioni-note)
- [Avvisi di licenza di terze parti](#avvisi-di-licenza-di-terze-parti)

## Specifiche tecniche complete

Le sezioni che seguono riportano in italiano tutte le guide, le specifiche, le
procedure di validazione, la checklist e le note sui componenti di terze parti
presenti nella versione inglese.

## Dettagli di build, trasporto CDC, RCM e GUI

### Toolchain e configurazione CMake

Il Pico SDK non è incluso nel repository. Il suo flusso CMake usa
`PICO_SDK_PATH` e `pico_sdk_init()`. Se ARM GCC non è nel `PATH`, imposta
`PICO_TOOLCHAIN_PATH` sulla radice della toolchain, cioè la cartella che
contiene `bin/arm-none-eabi-gcc`. Il checkout SDK deve includere il submodule
TinyUSB e supportare l'override dello script linker. Il repository include
Pico-PIO-USB 0.7.2 e LittleFS v2.11.3. Il linker riserva il primo MiB al
firmware e assegna gli altri 7 MiB a LittleFS; se il firmware supera il limite,
la fase di link fallisce.

Il firmware abilita TinyUSB host sulla root port 1 tramite Pico-PIO-USB. Il task
host gira sul Core 1, usa GPIO16 per D+ e GPIO17 per D- e pubblica lo stato con
`mrx_usb_host_get_status()`. La documentazione di quella fase specificava che
GPIO18/VBUS restava LOW e non veniva abilitato. Il connettore USB-A è integrato:
non servono accessori o cablaggi ai pin.

### Gestione payload e implementazione RCM

Il payload manager include i comandi CDC per elencare payload e leggere
metadati, upload/abort transazionali, eliminazione, selezione, query della
selezione e verifica SHA-256 a flusso. Gli upload usano sentinelle LittleFS
`.uploading`, offset sequenziali, hash calcolato durante la ricezione, verifica
con rilettura e commit atomico di metadati e configurazione.

Un class driver applicativo TinyUSB e dedicato a NVIDIA APX
(`0955:7321`), legge a blocchi limitati il payload selezionato e verificato da
LittleFS e compila con la toolchain ARM lo stub di rilocazione Tegra. L'iniezione
è un'API esplicita (`mrx_rcm_inject_selected()`), non viene eseguita all'avvio.
Con framing di trasferimento da 4 KiB e il limite lunghezza del comando RCM,
la dimensione massima accettata è `0x1FD58` byte. Il risultato finale
dell'API è non confermabile perché, quando l'esecuzione riesce, il target smette
di rispondere. La sequenza documentata Fusée Gelée usa gli endpoint bulk e una
richiesta di controllo `GET_STATUS` dell'endpoint sovradimensionata, come
descritto nella [divulgazione della vulnerabilità](https://misc.ktemkin.com/fusee_gelee_nvidia.pdf)
e nel [launcher di riferimento](https://github.com/Smeat/fusee-launcher/blob/master/fusee-launcher.py).

GPIO18/VBUS rimane LOW all'avvio e l'API RCM non lo abilita. Per convalidare
enumerazione, trasferimenti di controllo grandi ed esecuzione del payload
servono Feather e target in RCM; la build non dimostra questi comportamenti
fisici.

### Sequenza standalone e indicazioni LED

All'avvio il loader inizializza lo storage, verifica il payload configurato e
avvia l'host PIO sul Core 1. Durante il rilevamento PC tiene VBUS LOW. In
modalità normale attende fino a 750 ms il link USB-C del PC, poi, se assente,
passa a standalone. La modalità standalone esplicita salta questa finestra.
L'host deve essere pronto e deve esserci un payload selezionato valido prima
che GPIO18 diventi HIGH; senza selezione valida, VBUS resta LOW e il NeoPixel
segnala payload assente.

Il NeoPixel integrato usa GPIO20 per l'alimentazione e GPIO21 per i dati. Il
driver PIO usa PIO1, separato dai programmi host USB su PIO0. Gli stati
comprendono avvio, PC, attesa standalone, rilevamento dispositivo e RCM,
trasferimento, errore storage, payload assente e fallimento. Dopo il trigger
RCM l'esecuzione non è confermabile via USB: l'indicazione magenta resta per un
breve periodo invece di dichiarare successo o fallimento certo. La schermata
di Hekate è la conferma visibile.

Il protocollo MRX attuale non ha un comando per cambiare `boot_mode`. Il
rilevamento automatico normale non richiede modifiche; configurare dall'app la
modalità che salta la finestra di rilevamento richiede un'estensione futura.

### Test protocollo e storage su host

La suite host richiede Python 3.10 o più recente. Se trova GCC o Clang, compila
ed esegue anche l'implementazione firmware del protocollo in un host harness.
La build SDK compila a parte tutto il firmware RP2040.

```powershell
python tools\test_mrx_protocol.py
```

I controlli comprendono CRC-16/CCITT-FALSE, codifica e parsing pacchetti,
risincronizzazione stream, `GET_INFO`, comando sconosciuto, inizializzazione
GPIO sicura, GPIO18 disabilitato in `main.c` e regione linker generata da 1 MiB.
Il codice storage/configurazione è compilato contro una flash NOR simulata da
8 MiB per verificare prima formattazione, persistenza e recupero configurazione
e pulizia upload interrotto. Lo stesso harness prova upload payload, SHA-256,
metadati, elenco, selezione, lettura blocchi ed eliminazione. Verifica inoltre
layout invalidi, risposta `LIST_PAYLOADS` massima con buffer CDC limitato e,
con GCC, fallimento del link quando si supera la partizione firmware. Senza
compilatore C host, harness C e prova overflow linker vengono saltati; restano
i controlli Python e la configurazione CMake.

I test host non sostituiscono Feather RP2040 USB Host: non verificano
enumerazione USB reale, tempi flash, alimentazione elettrica o hardware.

### Verifica fisica USB CDC

Il firmware espone il protocollo come byte USB CDC grezzi. Aprire la porta
seriale con DTR attivo e inviare una richiesta `CMD_GET_INFO` senza DATA. Ci si
aspetta una risposta `CMD_GET_INFO | 0x80` con CRC valido e lo stesso numero di
sequenza. CDC ignora il baud rate configurato.

Con la porta USB-C del Feather collegata al PC, installare `pyserial` se serve
ed eseguire:

```powershell
python tools\probe_mrx_cdc.py
```

Il probe prova prima le porte VID `0x1209`, PID `0x0001`; se non ne trova,
prova le altre porte seriali. Controlla framing, CRC, sequenza, versione
protocollo, stringhe prodotto e hardware, device ID completato con zeri e
descrittore seriale USB se il sistema operativo lo espone. Il timeout per porta
è 500 ms e si può cambiare con `--timeout`. Il PID `0x0001` è provvisorio e va
sostituito con un PID assegnato prima di una release pubblica.

Procedura hardware: compila e copia `firmware/build/mrx_loader.uf2` sul Feather
in BOOTSEL; scollega e ricollega USB-C in modalità normale; esegui il probe e
verifica il messaggio `CDC GET_INFO handshake passed`; ripeti dopo aver
scollegato e ricollegato USB-C. La nuova sessione CDC deve accettare pacchetti
senza byte di framing obsoleti dalla sessione precedente. L'harness C host
verifica separatamente un frame parziale attraverso disconnessione e
riconnessione simulate. Questo test fisico richiede la scheda.

### Guida dettagliata dell'app desktop

L'app PySide6 rileva Feather MRX Loader via USB CDC, confronta la versione del
protocollo e mostra firmware, ID e spazio storage. Supporta elenco, upload,
verifica, selezione ed eliminazione payload. Gli upload sono inviati in blocchi
del protocollo, verificati con SHA-256, riletti dalla flash e facoltativamente
impostati come attivi. I comandi riservati `GET_LOG` e `REBOOT` non sono
esposti come operazioni normali dell'interfaccia.

L'app si avvia manualmente dalla cartella principale con
`python -m tools.mrx_gui.app`, su Windows come su Linux. Non sono previsti
servizi in background, installatori di sistema o avvio automatico al login.

Procedura payload: selezionare **Upload payload**, scegliere un file `.bin`,
`.payload` o un altro file, e mantenere **Select as active after verification**
se il nuovo payload deve diventare quello standalone. L'app invia blocchi
sequenziali, chiede al firmware di fare commit e rilettura, esegue
`VERIFY_PAYLOAD` e conferma la selezione. La tabella mostra nome, ID,
dimensione, prefisso SHA-256 e stato.

Procedura aggiornamento: preparare un UF2 MRX Loader RP2040, selezionare
**Update firmware** con Feather collegato via USB-C, poi l'app convalida
struttura blocchi, family ID RP2040 e intervallo firmware da 1 MiB, e chiede
conferma. Dopo BOOTSEL l'utente copia il file selezionato nella radice di
`RPI-RP2`. Quando l'unità scompare, l'app cerca di nuovo la scheda e mostra la
versione riportata dopo la riconnessione. Se l'ingresso automatico BOOTSEL
fallisce, usare la procedura fisica della scheda e un UF2 noto e valido.

## Specifica dell'architettura di alimentazione

Versione del documento: 1.0


### 1. Panoramica

Adafruit Feather RP2040 USB Host (modello 5723) dispone di tre possibili fonti
di alimentazione e di un'uscita controllata. Capire come interagiscono è
essenziale per scrivere correttamente il firmware MRX Loader.

Fonti di alimentazione:

- USB-C (da PC, alimentatore o power bank USB)
- Batteria LiPo da 3,7 V collegata al connettore JST

Uscita controllata:

- VBUS USB-A, generata da un convertitore boost a 5 V e controllata da GPIO18

Il firmware deve gestire esplicitamente l'uscita controllata e non deve
presumerne lo stato all'avvio.

### 2. Hardware delle fonti di alimentazione

#### 2.1 Alimentazione USB-C

Il connettore USB-C alimenta il Feather da un PC host, un alimentatore USB o
una power bank.

Con USB-C collegata:

- Il sistema è alimentato da USB-C.
- La batteria LiPo si carica fino a 200 mA tramite il caricatore integrato.
- La periferica USB nativa dell'RP2040 è disponibile per la comunicazione.

Con USB-C scollegata:

- Il caricatore LiPo è inattivo.
- Se è presente una batteria LiPo, il sistema passa automaticamente
  all'alimentazione a batteria.
- Se la batteria manca, il dispositivo non è alimentato.

Il passaggio tra USB-C e LiPo è gestito dall'hardware del Feather e non
richiede interventi del firmware. Per l'RP2040 la transizione è trasparente.

#### 2.2 Batteria LiPo

Il Feather accetta batterie LiPo standard a una cella da 3,7 V tramite il
connettore JST. Il caricatore integrato eroga una corrente di carica nominale
di 200 mA, sufficiente nei casi in cui il Feather venga collegato periodicamente
alla USB-C per ricaricarsi.

L'RP2040 e le sue periferiche funzionano a 3,3 V. Il regolatore integrato a
3,3 V accetta direttamente la tensione LiPo, nominalmente 3,7 V e con un
intervallo approssimativo da 3,0 V a 4,2 V.

La batteria LiPo è opzionale. Il dispositivo può funzionare solo con USB-C,
ma non può continuare a operare standalone dopo aver scollegato il PC. Questa
scheda non offre al firmware un accesso diretto alla tensione della batteria
tramite un pin ADC dedicato; la stima dello stato della batteria è fuori ambito
per l'implementazione iniziale.

### 3. Controllo di VBUS dell'host USB

#### 3.1 Hardware

Il Feather 5723 include un convertitore boost TPS61023 che genera 5 V dalla
tensione disponibile. L'uscita a 5 V è collegata al pin VBUS della porta USB-A.
Il pin di abilitazione del TPS61023 è collegato a GPIO18 dell'RP2040.

| Stato GPIO18 | Stato VBUS | Effetto sul dispositivo collegato |
| --- | --- | --- |
| LOW o ingresso | 5 V disattivati | Nessuna alimentazione USB-A; dispositivo disconnesso |
| HIGH | 5 V attivi, fino a 1 A | Il dispositivo USB riceve alimentazione e avvia l'enumerazione |

La linea VBUS ha un fusibile ripristinabile da 500 mA. Correnti prolungate
superiori a circa 500 mA fanno intervenire il fusibile e disconnettono VBUS.
In modalità RCM la Nintendo Switch assorbe circa 100 mA prima di ricevere un
payload, un valore entro il limite del fusibile.

#### 3.2 Controllo del firmware

All'avvio il firmware deve configurare GPIO18 come uscita e impostarlo LOW,
così VBUS parte disabilitato:

```c
gpio_init(USB_HOST_VBUS_ENABLE_PIN);          // GPIO 18
gpio_set_dir(USB_HOST_VBUS_ENABLE_PIN, GPIO_OUT);
gpio_put(USB_HOST_VBUS_ENABLE_PIN, 0);        // VBUS disabilitato all'avvio
```

Se GPIO18 non viene inizializzato come uscita, rimane ad alta impedenza. Il
TPS61023 potrebbe interpretare questo stato come abilitazione o disabilitazione
a seconda del pull-down interno. Va sempre impostato esplicitamente.

#### 3.3 Sequenza di attivazione VBUS

Per attivare VBUS dell'host USB, il firmware segue questa sequenza:

```text
1. Inizializzare lo stack host PIO USB sul Core 1.
2. Attendere che il Core 1 segnali di essere pronto.
3. Attivare VBUS: gpio_put(USB_HOST_VBUS_ENABLE_PIN, 1).
4. Attendere l'enumerazione del dispositivo tramite il task host TinyUSB.
```

VBUS non va attivato prima che lo stack PIO USB sia inizializzato e in
esecuzione. In caso contrario, la Switch può iniziare l'enumerazione prima che
l'host sia in ascolto e la procedura può fallire o andare in timeout.

Per disabilitare l'host, ad esempio passando alla sola modalità PC o in caso
di errore:

```text
1. Disattivare VBUS: gpio_put(USB_HOST_VBUS_ENABLE_PIN, 0).
2. Attendere almeno 100 ms per scaricare il dispositivo e disconnetterlo.
3. Se si cambia modalità, interrompere il servizio dello stack host.
```

L'attesa di 100 ms consente ai condensatori interni della Switch di scaricarsi
ed evita una disconnessione e riconnessione parziale.

#### 3.4 Ripristino forzato di VBUS

Portare GPIO18 LOW e poi HIGH esegue un ripristino forzato della porta USB host
e può recuperare un dispositivo bloccato o non responsivo:

```text
Disattivare VBUS (GPIO18 LOW)
Attendere 200 ms
Riattivare VBUS (GPIO18 HIGH)
Ripetere l'enumerazione
```

La specifica originale indica questa procedura come prima azione di recupero
in caso di errore o timeout dell'host USB.

### 4. Stati di alimentazione

Il firmware gestisce tre stati distinti, ognuno con capacità e comportamenti
richiesti diversi.

#### 4.1 Modalità alimentata dal PC

```text
PC (USB-C) ---> Feather RP2040
                     |
              VBUS host USB: OFF (GPIO18 LOW)
```

**Ingresso:** lo stack USB device dell'RP2040 rileva la connessione USB-C e
l'enumerazione dal PC riesce.
**Uscita:** la disconnessione USB-C, rilevata dall'evento di disconnessione
dello stack USB device.

Comportamento previsto:

- Lo stack USB device TinyUSB/CDC è attivo sul Core 0.
- La comunicazione del protocollo MRX con il PC è attiva.
- VBUS dell'host USB resta disabilitato (GPIO18 LOW), quindi la Switch non va
  collegata durante la modalità PC.
- Il NeoPixel indica che il PC è collegato.
- Le operazioni sui payload, le modifiche di configurazione e gli aggiornamenti
  del firmware avvengono in questa modalità.

La specifica mantiene VBUS disattivato in modalità PC perché l'utente sta
configurando il dispositivo; collegare anche la Switch sarebbe insolito e
aumenterebbe il rischio di un'iniezione accidentale. Un'opzione futura potrebbe
prevedere un'abilitazione esplicita.

#### 4.2 Modalità standalone alimentata

```text
Batteria LiPo o alimentatore USB
             |
             v
      Feather RP2040
             |
      Host USB (GPIO18 HIGH quando pronto)
             |
             v
        Connettore USB-A
             |
          Cavo USB-C
             |
             v
     Nintendo Switch (modalità RCM)
```

**Ingresso:** l'RP2040 si avvia e rileva che il PC non è collegato via USB; la
configurazione contiene un payload valido selezionato.
**Uscita:** rimozione dell'alimentazione.

Comportamento previsto:

- Lo stack USB device può restare inizializzato ma non viene usato attivamente
  perché non c'è un PC collegato.
- Il Core 1 esegue lo stack host PIO USB.
- GPIO18 viene portato HIGH per attivare VBUS quando lo stack host è pronto.
- Il firmware attende un dispositivo sulla porta USB-A, lo enumera e lo
  identifica.
- Se rileva VID `0x0955` e PID `0x7321` (Switch in modalità RCM), procede con
  il trasferimento; ignora altri dispositivi e continua ad attendere.
- Il NeoPixel segnala lo stato.

Tutti i dati necessari al trasferimento, inclusi payload e configurazione,
devono essere già presenti in flash prima di entrare in questa modalità. Non
c'è un PC disponibile né è possibile scaricare dati.

#### 4.3 Stato non alimentato

Se non ci sono USB-C, batteria LiPo o altra fonte esterna, il Feather è spento.
Non viene eseguito firmware. Uscendo da questo stato e applicando una fonte di
alimentazione, il dispositivo si avvia.

La flash QSPI esterna conserva i dati senza alimentazione; la durata nominale
minima indicata è di 20 anni in condizioni normali. Il firmware non deve
eseguire azioni speciali per conservarli. Ogni scrittura flash deve però
terminare in uno stato completo e coerente prima di essere considerata riuscita:
il firmware non può presumere che l'alimentazione rimanga disponibile durante
una sequenza di scrittura non recuperabile.

### 5. Sequenza di accensione

All'applicazione dell'alimentazione, il firmware esegue questi passaggi:

```text
Alimentazione applicata
  -> bootloader ROM RP2040 (trasparente all'applicazione)
  -> bootloader di secondo stadio (impostazione XIP)
  -> main()
  -> BOOT: inizializza GPIO, GPIO18 LOW, GPIO16/17 per host USB;
           inizializza il NeoPixel
  -> INITIALIZE: hardware, clock e watchdog
  -> LOAD_CONFIGURATION: storage_init(), storage_mount(), config_load();
           se il mount fallisce, formatta e ripete il mount
  -> VALIDATE: convalida il payload selezionato
  -> IDLE: controlla la connessione USB-C
       -> PC_MODE se il PC è connesso
       -> STANDALONE_MODE altrimenti
```

L'obiettivo previsto dalla specifica è un avvio inferiore a 500 ms in
condizioni normali. La durata reale va misurata durante i test; la correttezza
non deve dipendere da quel limite, perché mount LittleFS, recupero filesystem,
inizializzazione USB, sincronizzazione multicore e controlli futuri possono
modificare il tempo di avvio.

### 6. Comportamento quando si collega la Switch

In modalità standalone, dopo aver attivato VBUS, il task PIO USB rileva la
connessione, TinyUSB enumera il dispositivo e il firmware controlla VID/PID.
Se sono `0x0955:0x7321`, riconosce RCM, legge dalla flash il payload selezionato
ed esegue il trasferimento. Per un dispositivo diverso, registra il dispositivo
sconosciuto e continua ad attendere.

Il firmware non deve iniziare un trasferimento senza aver verificato prima che
il payload selezionato sia valido e leggibile. Se in quel momento la lettura
fallisce per un errore filesystem o dati corrotti, deve interrompere il
trasferimento, segnalare l'errore e non scegliere automaticamente un altro
payload.

Il diagramma storico della specifica associa un LED verde al successo. Nel
comportamento attuale di MRX Loader l'esito dopo il trigger RCM resta invece
non confermabile dal firmware; non va quindi mostrato come successo certo.

### 7. Comportamento quando la Switch si disconnette

Se la Switch si disconnette durante il trasferimento, la sessione è incompleta.
La specifica richiede di:

1. Interrompere il trasferimento in corso.
2. Ripristinare la porta host USB commutando GPIO18.
3. Registrare l'evento di disconnessione.
4. Tornare allo stato di attesa.

Se si disconnette prima dell'inizio del trasferimento, il firmware torna
semplicemente ad attendere. Non deve dedurre che una disconnessione durante
l'operazione significhi che la Switch si sia avviata: per il firmware è sempre
un errore o una sessione interrotta.

### 8. Comportamento quando manca l'alimentazione

L'RP2040 non può rilevare la rimozione imminente dell'alimentazione con tempo
sufficiente a completare una scrittura. Ogni scrittura flash deve quindi essere
atomica e verificabile.

- Non scrivere una configurazione che dipenda da una scrittura successiva per
  essere coerente.
- Non marcare un payload come valido prima di aver scritto e verificato tutti
  i dati.
- Non impostare `selected_payload_id` su un payload non ancora salvato in modo
  completo.

Queste regole consentono la rimozione dell'alimentazione in qualsiasi momento
senza lasciare il dispositivo in uno stato irrecuperabile. Le procedure di
recupero sono dettagliate nella sezione storage più avanti.

### 9. Codici di stato del NeoPixel

Il NeoPixel è l'unica indicazione visibile all'utente in modalità standalone.
La specifica originaria definisce questi colori e pattern:

| Stato | Colore | Pattern |
| --- | --- | --- |
| Avvio | Bianco | Impulso lento |
| Modalità PC attiva | Blu | Fisso |
| Standalone in attesa | Giallo | Impulso lento |
| Dispositivo USB rilevato | Ciano | Impulso rapido |
| RCM rilevato | Magenta | Impulso rapido |
| Trasferimento in corso | Magenta | Lampeggio rapido |
| Trasferimento riuscito | Verde | Fisso per 3 s |
| Trasferimento fallito | Rosso | Lampeggio rapido |
| Errore storage | Rosso | Due lampeggi |
| Nessun payload selezionato | Arancione | Impulso lento |
| Formattazione filesystem | Bianco | Lampeggio rapido |

Dopo un errore o uno stato terminale, la specifica prevede il ritorno
all'attesa standalone. La nota storica chiedeva di verificare sullo schema il
GPIO del NeoPixel Feather 5723 prima di scrivere il driver. Questa scheda usa
GPIO20 per l'alimentazione e GPIO21 per i dati; il comportamento attuale non
deve mostrare un esito RCM certo.

### 10. Vincoli di inizializzazione e alimentazione multicore

Lo stack host PIO USB richiede il Core 1, che va avviato dopo aver completato
sul Core 0 l'inizializzazione hardware. La sincronizzazione multicore (ad
esempio un semaforo o la FIFO multicore Pico SDK) deve garantire che:

1. Il Core 0 completi l'inizializzazione critica prima dell'avvio del Core 1.
2. Il Core 1 segnali al Core 0 che lo stack host PIO USB è pronto.
3. Il Core 0 non attivi GPIO18/VBUS prima di aver ricevuto quel segnale.

Sequenza sintetica:

```text
Core 0                              Core 1
storage_init()                      (in attesa)
config_load()                       (in attesa)
determina la modalità               (in attesa)
avvia Core 1 ----------------------> pio_usb_init()
attende il segnale                  tuh_init()
<---------------------------------- segnala pronto
attiva VBUS                         esegue il ciclo tuh_task()
continua il ciclo applicativo
```

Se il Core 1 non segnala che è pronto entro il timeout, il Core 0 deve
registrare l'errore, lasciare VBUS disabilitato e passare a uno stato di errore.

### 11. Riepilogo dei pin GPIO

Gli assegnamenti sono determinati dal PCB Adafruit Feather RP2040 USB Type-A
Host e dalla relativa documentazione dei pin.

| GPIO | Funzione | Direzione | Note |
| --- | --- | --- | --- |
| 16 | D+ host USB | PIO | Pico-PIO-USB; non riutilizzare |
| 17 | D- host USB | PIO | Pico-PIO-USB; non riutilizzare |
| 18 | Abilitazione VBUS host | Uscita | HIGH = 5 V su USB-A; LOW = VBUS spento |
| 20 | Alimentazione NeoPixel | Uscita | HIGH = NeoPixel alimentato |
| 21 | Dati NeoPixel | Uscita | Segnale WS2812 |

Nessun altro GPIO va usato per queste funzioni. Per inizializzare il NeoPixel
del Feather 5723, prima si abilita l'alimentazione su GPIO20, poi si inviano i
dati su GPIO21 tramite il programma PIO WS2812 o un driver equivalente:

```c
gpio_init(NEOPIXEL_POWER_PIN);              // GPIO 20
gpio_set_dir(NEOPIXEL_POWER_PIN, GPIO_OUT);
gpio_put(NEOPIXEL_POWER_PIN, 1);             // Alimenta il NeoPixel
```

GPIO20 deve essere HIGH prima di inviare dati su GPIO21, altrimenti il
NeoPixel non risponde.

## Specifica del protocollo MRX

Versione del documento: 1.1


### 1. Principi di progettazione

Il protocollo MRX definisce per intero la comunicazione tra l'applicazione PC
e il firmware Feather. I requisiti sono:

- **Binario, non testuale:** il parsing sull'RP2040 deve richiedere poco lavoro.
- **Versionato:** PC e firmware devono riconoscere un'incompatibilità e
  gestirla senza bloccarsi.
- **Auto-delimitante:** il ricevitore deve trovare l'inizio di un pacchetto in
  un flusso di byte anche senza sincronizzazione iniziale.
- **Con rilevamento errori:** ogni pacchetto contiene un checksum; quelli
  corrotti vanno rifiutati.
- **Solo richiesta/risposta:** nella versione 1 il protocollo è sincrono. Il PC
  invia una richiesta e attende la risposta; non ci sono richieste in parallelo
  né messaggi avviati dal dispositivo.
- **Estendibile:** si possono aggiungere comandi senza rompere i parser che non
  li conoscono, purché la struttura del pacchetto resti invariata.

### 2. Trasporto

MRX usa USB CDC (Communications Device Class). TinyUSB presenta il Feather al PC
come porta seriale virtuale. Questa scelta evita driver kernel dedicati su
Windows 10/11, Linux e macOS, è supportata da pyserial e fornisce un semplice
flusso di byte. Il livello applicativo non deve gestire dimensioni di
trasferimento USB specifiche.

CDC trasporta byte grezzi: USB CDC ignora il baud rate. L'app PC non deve
impostarlo né dipendere da un valore particolare.

#### 2.1 Identificazione del dispositivo USB

I descrittori USB previsti dal documento sono:

| Campo | Valore | Note |
| --- | --- | --- |
| `idVendor` | `0x1209` | VID open-source pid.codes |
| `idProduct` | `0x0001` | Segnaposto; da registrare su pid.codes |
| `bcdDevice` | `0x0100` | Versione dispositivo 1.0 |
| Manufacturer string | `MRX Loader Project` | Produttore dichiarato |
| Product string | `MRX Loader` | Nome prodotto |
| Serial number string | Derivato dal chip ID RP2040 | 16 caratteri esadecimali |

`0x1209` è il VID open-source di pid.codes. Prima di una release pubblica va
richiesto un PID univoco su [pid.codes](https://pid.codes); il segnaposto
`0x0001` non va usato in firmware distribuito.

#### 2.2 Rilevamento dal PC

L'app PC:

1. Elenca tutte le porte seriali tramite API del sistema operativo o pyserial.
2. Prova a collegarsi alle porte con VID `0x1209`; se il filtro VID/PID non è
   disponibile, prova tutte le porte.
3. Invia una richiesta `GET_INFO`.
4. Verifica i byte MAGIC e che il nome prodotto sia MRX Loader.
5. Usa la prima porta che supera il controllo.

Ogni tentativo per porta deve terminare entro 500 ms; in caso di handshake
fallito l'app non deve lasciare la porta aperta.

### 3. Struttura dei pacchetti

Richieste e risposte utilizzano la stessa struttura:

```text
Offset  Dim.    Campo       Descrizione
0       4       MAGIC       Byte fissi: 4D 52 58 21 ("MRX!")
4       1       CMD         Codice comando
5       1       FLAGS       Flag del pacchetto
6       2       SEQ         Numero sequenza uint16 little-endian
8       2       LENGTH      Dimensione DATA in byte, uint16 little-endian
10      N       DATA        Dati (da 0 a MRX_MAX_DATA_SIZE byte)
10+N    2       CRC16       CRC-16 dei byte da 0 a 10+N-1, uint16 little-endian
```

L'overhead totale è di 12 byte.

#### 3.1 Dimensioni massime

```c
// Byte MAGIC fissi
#define MRX_MAGIC_0  0x4D   // 'M'
#define MRX_MAGIC_1  0x52   // 'R'
#define MRX_MAGIC_2  0x58   // 'X'
#define MRX_MAGIC_3  0x21   // '!'

#define MRX_MAX_DATA_SIZE    4096U
#define MRX_MAX_PACKET_SIZE  (10U + MRX_MAX_DATA_SIZE + 2U)  // 4108 byte
#define MRX_PROTOCOL_VERSION 1U
```

Il campo DATA da 4096 byte corrisponde alla dimensione di un blocco LittleFS.
`UPLOAD_DATA` usa un'intestazione di 16 byte, quindi ogni pacchetto trasporta
fino a 4080 byte di payload.

#### 3.2 Byte FLAGS

Tutti i bit sono riservati e nella versione 1 devono valere zero. FLAGS è
presente per estensioni future; per compatibilità in avanti un ricevitore
dovrebbe evitare di rifiutare un pacchetto solo perché contiene un valore
non-zero non riconosciuto.

#### 3.3 Campo SEQ

Il PC parte da sequenza 0 e incrementa di uno per ogni nuova richiesta; dopo
65535 riparte da 0. Il Feather copia SEQ nella risposta corrispondente, così il
PC può associarla alla richiesta e rilevare risposte duplicate o fuori ordine.
Ogni pacchetto di una sequenza di upload `UPLOAD_DATA` ha un proprio SEQ.

#### 3.4 Calcolo CRC-16

Si usa CRC-16/CCITT-FALSE con polinomio `0x1021`, valore iniziale `0xFFFF`,
nessuna riflessione in ingresso o in uscita e XOR finale `0x0000`. Il CRC copre
i byte dal campo MAGIC fino all'ultimo byte DATA incluso; i due byte del CRC
non sono inclusi. Il valore è memorizzato little-endian: prima il byte basso,
poi quello alto.

Se il CRC ricevuto non coincide con quello calcolato, il pacchetto viene
rifiutato. Il ricevitore non invia NACK: scarta silenziosamente il pacchetto e
attende una ritrasmissione o il timeout del PC. Un NACK dovrebbe infatti usare
il SEQ del pacchetto corrotto, che potrebbe a sua volta essere spazzatura; il
timeout del PC gestisce il caso in modo più pulito.

#### 3.5 Distinzione tra richieste e risposte

Il bit 7 del byte CMD distingue la direzione:

- Richiesta: bit 7 uguale a 0, valori da `0x01` a `0x7F`.
- Risposta: bit 7 uguale a 1, valori da `0x81` a `0xFF`.

Il CMD della risposta è il CMD della richiesta con il bit 7 impostato:

```c
response_cmd = request_cmd | 0x80;
```

#### 3.6 Struttura DATA delle risposte

Il primo byte DATA di ogni risposta è `STATUS`; i byte seguenti, se presenti,
sono dati specifici del comando. Se STATUS non è `STATUS_OK`, la risposta può
non avere altri dati oppure può contenere un byte aggiuntivo con dettagli
sull'errore.

### 4. Tabelle dei codici

#### 4.1 Codici comando

| Codice | Nome | Direzione | Significato |
| --- | --- | --- | --- |
| `0x01` | `CMD_GET_INFO` | PC -> Feather | Informazioni su dispositivo e firmware |
| `0x02` | `CMD_GET_STATUS` | PC -> Feather | Stato corrente |
| `0x03` | `CMD_GET_LOG` | PC -> Feather | Voci recenti del log |
| `0x10` | `CMD_LIST_PAYLOADS` | PC -> Feather | Elenco dei payload salvati |
| `0x11` | `CMD_GET_PAYLOAD_INFO` | PC -> Feather | Metadati di un payload |
| `0x12` | `CMD_UPLOAD_BEGIN` | PC -> Feather | Avvio transazione di upload |
| `0x13` | `CMD_UPLOAD_DATA` | PC -> Feather | Invio di un blocco dati |
| `0x14` | `CMD_UPLOAD_END` | PC -> Feather | Verifica e commit dell'upload |
| `0x15` | `CMD_UPLOAD_ABORT` | PC -> Feather | Annullamento dell'upload |
| `0x16` | `CMD_DELETE_PAYLOAD` | PC -> Feather | Eliminazione di un payload |
| `0x17` | `CMD_SELECT_PAYLOAD` | PC -> Feather | Selezione del payload attivo |
| `0x18` | `CMD_GET_SELECTED` | PC -> Feather | Lettura del payload selezionato |
| `0x19` | `CMD_VERIFY_PAYLOAD` | PC -> Feather | Ricalcolo e confronto hash |
| `0x20` | `CMD_REBOOT` | PC -> Feather | Riavvio del dispositivo |
| `0x30` | `CMD_FW_UPDATE_BEGIN` | PC -> Feather | Riservato a una versione futura |
| `0x31` | `CMD_FW_UPDATE_DATA` | PC -> Feather | Riservato a una versione futura |
| `0x32` | `CMD_FW_UPDATE_END` | PC -> Feather | Riservato a una versione futura |

Il codice risposta è il codice comando corrispondente con il bit 7 impostato,
ovvero `CMD | 0x80`.

#### 4.2 Valori FLAGS

| Valore | Nome | Significato |
| --- | --- | --- |
| `0x00` | `FLAGS_NONE` | Pacchetto standard; usato da tutti i pacchetti v1 |

#### 4.3 Codici di stato

Il byte STATUS è il primo byte del campo DATA di ogni risposta. I codici sono:

| Codice | Nome | Significato |
| --- | --- | --- |
| `0x00` | `STATUS_OK` | Operazione completata |
| `0x01` | `STATUS_ERR_GENERAL` | Errore interno non specificato |
| `0x02` | `STATUS_ERR_NOT_FOUND` | Payload o risorsa inesistente |
| `0x03` | `STATUS_ERR_CORRUPT` | CRC o MAGIC dei dati salvati errati |
| `0x04` | `STATUS_ERR_FULL` | Spazio filesystem insufficiente |
| `0x05` | `STATUS_ERR_INVALID_PARAM` | Richiesta malformata o fuori intervallo |
| `0x06` | `STATUS_ERR_BUSY` | È già in corso un'altra operazione |
| `0x07` | `STATUS_ERR_NO_SELECTION` | Nessun payload selezionato |
| `0x08` | `STATUS_ERR_VERSION` | Versione protocollo o formato incompatibile |
| `0x09` | `STATUS_ERR_IO` | Errore I/O flash o filesystem |
| `0x0A` | `STATUS_ERR_INCOMPLETE` | Payload non marcato come valido |
| `0x0B` | `STATUS_ERR_ABORTED` | Operazione annullata esplicitamente |
| `0x0C` | `STATUS_ERR_UNKNOWN_CMD` | Comando non riconosciuto |

L'applicazione PC tratta i codici STATUS sconosciuti come
`STATUS_ERR_GENERAL`.

### 5. Specifiche dei comandi

Ogni comando definisce la disposizione DATA della richiesta e della risposta.
Gli interi sono little-endian se non indicato diversamente; le stringhe sono
UTF-8, gli array di byte contengono dati binari grezzi. `uint8`, `uint16`,
`uint32` e `uint64` sono interi senza segno della dimensione indicata;
`char[N]` è un array a dimensione fissa con terminatore NUL incluso e
`bytes[32]` è un array di 32 byte.

#### 5.1 `CMD_GET_INFO` (`0x01`)

È l'handshake di rilevamento: richiede identificazione del dispositivo,
versione firmware e versione protocollo. La richiesta non contiene DATA
(LENGTH = 0). Nella risposta `STATUS_OK` il campo DATA ha 86 byte:

| Offset | Dim. | Campo | Descrizione |
| --- | --- | --- | --- |
| 0 | 1 | STATUS | `0x00` = `STATUS_OK` |
| 1 | 1 | `protocol_version` | Versione MRX, attualmente 1 |
| 2 | 1 | `fw_version_major` | Versione firmware maggiore |
| 3 | 1 | `fw_version_minor` | Versione firmware minore |
| 4 | 1 | `fw_version_patch` | Versione firmware patch |
| 5 | 1 | `reserved` | `0x00` |
| 6 | 32 | `product_string` | `MRX Loader`, completata con NUL |
| 38 | 32 | `hw_string` | `Feather RP2040 USB Host`, completata con NUL |
| 70 | 16 | `device_id` | ID univoco a 128 bit derivato dal chip ID |

Il PC confronta `protocol_version` con la propria versione. Se non coincidono,
mostra un avviso e disabilita i comandi tranne `CMD_GET_INFO` e `CMD_REBOOT`.

#### 5.2 `CMD_GET_STATUS` (`0x02`)

La richiesta non ha DATA (LENGTH = 0). La risposta `STATUS_OK` è lunga 28 byte
e riporta:

| Offset | Dim. | Campo | Descrizione |
| --- | --- | --- | --- |
| 0 | 1 | STATUS | `0x00` = `STATUS_OK` |
| 1 | 1 | `device_state` | Stato corrente del firmware |
| 2 | 1 | `reserved` | `0x00` |
| 3 | 1 | `reserved` | `0x00` |
| 4 | 4 | `selected_payload_id` | ID selezionato; 0 = nessuna selezione |
| 8 | 4 | `payload_count` | Numero dei payload validi |
| 12 | 8 | `fs_free_bytes` | Spazio LittleFS libero, in byte |
| 20 | 8 | `fs_total_bytes` | Dimensione totale LittleFS, in byte |

Valori di `device_state`: `0x00` avvio in corso; `0x01` modalità PC;
`0x02` modalità standalone con host USB attivo; `0x03` trasferimento RCM in
corso; `0x04` errore non recuperabile, i cui dettagli sono nel log.

#### 5.3 `CMD_GET_LOG` (`0x03`)

Richiede le voci del buffer di log in RAM. Le voci non vengono conservate in
flash. La richiesta contiene un campo `max_bytes` di 2 byte little-endian,
limite massimo 4000. La risposta `STATUS_OK` contiene `text_length` (2 byte
little-endian) seguito da `log_text` lungo N byte, senza terminatore NUL e con
le righe separate da newline. Ogni riga ha formato:

```text
[LEVEL] testo del messaggio\n
```

`LEVEL` può essere `INFO`, `WARN`, `ERROR` o `DEBUG`. Se il buffer contiene
più testo di `max_bytes`, vengono omesse le voci più vecchie e si restituiscono
solo quelle recenti. Il testo inizia sempre al confine di una riga.

#### 5.4 `CMD_LIST_PAYLOADS` (`0x10`)

Restituisce gli ID di tutti i payload validi. Il PC usa il risultato per
popolare l'elenco, poi richiede i dettagli di ciascun ID con
`CMD_GET_PAYLOAD_INFO`. La richiesta non contiene DATA. La risposta OK contiene
STATUS (1 byte), `count` (2 byte, numero di ID) e `ids` (array di `count`
interi `uint32`). I payload con flag `UPLOADING` non sono inclusi; vengono
elencati solo quelli completi e validi.

#### 5.5 `CMD_GET_PAYLOAD_INFO` (`0x11`)

La richiesta contiene `payload_id` (4 byte, `uint32`). La risposta OK contiene:

| Offset | Dim. | Campo | Descrizione |
| --- | --- | --- | --- |
| 0 | 1 | STATUS | `0x00` = OK |
| 1 | 4 | `payload_id` | ID confermato |
| 5 | 4 | `flags` | Flag payload, definiti nella specifica storage, sezione 5.5 |
| 9 | 8 | `size` | Dimensione in byte, `uint64` |
| 17 | 32 | `sha256` | Hash SHA-256 del binario |
| 49 | 64 | `name` | Nome UTF-8, terminato da NUL |

La risposta DATA totale è di 113 byte. In caso d'errore restituisce STATUS
`STATUS_ERR_NOT_FOUND` o `STATUS_ERR_CORRUPT`.

#### 5.6 `CMD_UPLOAD_BEGIN` (`0x12`)

Avvia una transazione di upload, assegna un nuovo ID e crea lo storage
temporaneo. La richiesta DATA è composta da `expected_size` (8 byte, `uint64`),
`name_length` (1 byte, massimo 63) e `name` (N byte, senza terminatore NUL).
Il nome deve contenere solo ASCII stampabile da `0x20` a `0x7E`. Sono rifiutati
caratteri fuori intervallo, separatori `/` e `\` e caratteri riservati per i
nomi file (`: * ? " < > |`). Il firmware aggiunge internamente il terminatore.

La risposta OK contiene STATUS e `assigned_id` (4 byte, `uint32`). In caso
d'errore restituisce `STATUS_ERR_FULL`, `STATUS_ERR_BUSY` o
`STATUS_ERR_INVALID_PARAM`; BUSY significa che è già in corso un upload.

#### 5.7 `CMD_UPLOAD_DATA` (`0x13`)

Invia un blocco durante una transazione attiva. I blocchi devono partire
dall'offset zero e arrivare in sequenza senza salti. La richiesta DATA è:

| Offset | Dim. | Campo | Descrizione |
| --- | --- | --- | --- |
| 0 | 4 | `payload_id` | ID restituito da `CMD_UPLOAD_BEGIN` |
| 4 | 4 | `reserved` | Deve valere zero nella versione 1 |
| 8 | 8 | `chunk_offset` | Offset in byte all'interno del payload |
| 16 | N | `chunk_data` | Byte binari, N = LENGTH - 16, massimo 4080 |

I quattro byte riservati vanno rifiutati con `STATUS_ERR_INVALID_PARAM` se non
sono zero. L'intestazione è di 16 byte (ID da 4, riservati da 4, offset da 8),
perciò rimangono al massimo 4080 byte dati (4096 - 16). Questo chiarimento non
modifica versione wire né dimensione del pacchetto.

Il firmware verifica che `chunk_offset` coincida con il totale dei byte già
scritti. Se differisce, risponde `STATUS_ERR_INVALID_PARAM` e scarta subito
l'upload attivo. La risposta OK contiene `bytes_received` (8 byte, `uint64`),
totale ricevuto finora. Gli errori possibili sono NOT_FOUND, IO,
INVALID_PARAM o FULL. Qualsiasi risposta non OK significa che l'upload è già
stato annullato internamente e i dati parziali eliminati; il PC non deve inviare
anche `CMD_UPLOAD_ABORT`.

#### 5.8 `CMD_UPLOAD_END` (`0x14`)

Finalizza una transazione. Il firmware verifica dimensione totale e hash
SHA-256, rilegge dalla flash i dati scritti e conferma il payload. La richiesta
contiene `payload_id` (4 byte), `expected_size` (8 byte) e
`expected_sha256` (32 byte). La risposta OK contiene STATUS, ID confermato,
`actual_size` (8 byte) e `actual_sha256` (32 byte, verificato). Il PC dovrebbe
mostrare l'hash effettivo all'utente.

Gli errori sono `STATUS_ERR_CORRUPT` (hash diverso), `STATUS_ERR_IO`
(lettura di verifica fallita), `STATUS_ERR_INVALID_PARAM` (dimensione diversa)
o NOT_FOUND. Una risposta non OK significa che l'upload è già stato scartato e
il payload parziale o corrotto eliminato.

#### 5.9 `CMD_UPLOAD_ABORT` (`0x15`)

Annulla un upload attivo ed elimina i dati ricevuti. La richiesta contiene
`payload_id` (`uint32`, 4 byte); la risposta contiene STATUS OK o
`STATUS_ERR_NOT_FOUND`. NOT_FOUND indica che non esiste un upload attivo per
quell'ID, ad esempio perché è fallito o è già stato ripulito.

#### 5.10 `CMD_DELETE_PAYLOAD` (`0x16`)

Elimina il payload identificato da `payload_id` (`uint32`, 4 byte). Se era
quello selezionato, la selezione viene azzerata. La risposta OK contiene STATUS
e `selection_cleared` (1 byte: 1 se la selezione è stata cancellata, altrimenti
0). Se il valore è 1, l'interfaccia PC deve aggiornarsi e mostrare che non c'è
un payload selezionato. Gli errori sono NOT_FOUND e IO.

#### 5.11 `CMD_SELECT_PAYLOAD` (`0x17`)

Imposta il payload attivo. Il firmware convalida il payload prima di salvare la
selezione nella configurazione persistente. La richiesta contiene
`payload_id` (`uint32`, 4 byte). La risposta STATUS può essere OK, NOT_FOUND,
INCOMPLETE o IO. INCOMPLETE indica che il payload esiste ma non è marcato
valido, cioè manca il flag `VALID`.

#### 5.12 `CMD_GET_SELECTED` (`0x18`)

La richiesta non contiene DATA. La risposta OK contiene STATUS e
`selected_id` (`uint32`, 4 byte); zero significa che non è selezionato alcun
payload. Per la visualizzazione il PC dovrebbe trattare zero come
`STATUS_ERR_NO_SELECTION`.

#### 5.13 `CMD_VERIFY_PAYLOAD` (`0x19`)

Il firmware ricalcola dalla flash SHA-256 del payload e la confronta con
l'hash nei metadati. È un controllo in sola lettura. La richiesta contiene
`payload_id` (`uint32`, 4 byte). La risposta OK contiene STATUS, `match`
(1 byte: 1 se gli hash coincidono, 0 se differiscono), `stored_sha256` (32 byte
dai metadati) e `actual_sha256` (32 byte ricalcolati da `payload.bin`), per un
totale DATA di 66 byte.

STATUS OK con `match = 0` indica che l'operazione è riuscita ma i dati sono
corrotti; `match = 1` indica che sono integri. Se l'operazione non può essere
completata, gli errori sono NOT_FOUND, CORRUPT o IO. Su payload grandi il
calcolo può durare diversi secondi, perché legge tutto il file a blocchi:
l'app PC deve mostrare l'avanzamento e non andare in timeout troppo presto.

#### 5.14 `CMD_REBOOT` (`0x20`)

Riavvia l'RP2040. La richiesta contiene `reboot_mode` (1 byte): `0x00` per
avvio normale, `0x01` per riavvio nel bootloader USB BOOTSEL. La risposta è un
byte STATUS OK.

Il firmware invia la risposta prima del riavvio. Dopo averla ricevuta, il PC
dovrebbe chiudere immediatamente la connessione, che cade entro circa 100 ms.
Con `reboot_mode = 0x01`, `reset_usb_boot()` del Pico SDK avvia il bootloader
ROM USB Mass Storage, che consente di copiare un UF2 per l'aggiornamento.

Protocollo v1 implementa entrambe le modalità: valida la richiesta di un byte,
invia la risposta positiva, serve TinyUSB per almeno 100 ms per lasciare uscire
la risposta dal canale CDC e poi riavvia. Prima di chiedere BOOTSEL, l'app
convalida il UF2 per RP2040 e per la partizione firmware MRX da 1 MiB. Si copia
 poi il UF2 sull'unità `RPI-RP2`; la scrittura flash è eseguita dal bootloader
ROM, non dal firmware MRX in esecuzione.

#### 5.15 Comandi aggiornamento firmware

`CMD_FW_UPDATE_BEGIN`, `CMD_FW_UPDATE_DATA` e `CMD_FW_UPDATE_END`
(`0x30`, `0x31`, `0x32`) sono riservati nella versione 1 e devono restituire
`STATUS_ERR_UNKNOWN_CMD`. La versione 1 usa invece `CMD_REBOOT` con
`reboot_mode = 0x01` e il bootloader di massa UF2 nella ROM RP2040. Un
dispositivo firmware 1.x deve quindi rispondere UNKNOWN_CMD a quei tre comandi.

### 6. Sequenze con più passaggi

#### 6.1 Sequenza di upload

Una transazione completa segue questo ordine:

```text
PC                                  Feather
 |-- CMD_UPLOAD_BEGIN ------------->|
 |<- ACK (restituisce assigned_id)--|
 |-- CMD_UPLOAD_DATA [blocco 0] --->|
 |<- ACK ---------------------------|
 |-- CMD_UPLOAD_DATA [blocco 1] --->|
 |<- ACK ---------------------------|
 |              ...                 |
 |-- CMD_UPLOAD_END --------------->| expected_size + expected_sha256
 |<- ACK ---------------------------| actual_size + actual_sha256
```

In caso di errore `CMD_UPLOAD_DATA`, il firmware ha già annullato l'upload: il
PC registra l'errore e non invia `CMD_UPLOAD_END`. In caso di errore
`CMD_UPLOAD_END`, i dati parziali sono già stati eliminati. In entrambi i casi
non va inviato anche `CMD_UPLOAD_ABORT`. Il PC invia `CMD_UPLOAD_ABORT` solo se
l'utente annulla prima di inviare `CMD_UPLOAD_END` e il firmware non ha già
segnalato un errore.

#### 6.2 Flusso consigliato per verificare e selezionare

Dopo l'upload, prima di consentire la configurazione standalone, il PC dovrebbe
eseguire `CMD_VERIFY_PAYLOAD [id]` e verificare `match = 1`, inviare
`CMD_SELECT_PAYLOAD [id]` e verificare `STATUS_OK`, quindi inviare
`CMD_GET_SELECTED` e verificare che `selected_id` corrisponda all'ID. L'ultimo
scambio conferma che la selezione è persistita in flash prima di chiudere la
sessione.

#### 6.3 Aggiornamento tramite BOOTSEL

La versione 1 usa la ROM RP2040 anziché scrivere la partizione attiva dal
firmware:

1. Il PC convalida tutti i blocchi UF2, il family ID RP2040 e che ogni indirizzo
   di destinazione ricada tra `0x10000000` e `0x100FFFFF`.
2. Chiede conferma all'utente.
3. Invia `CMD_REBOOT` con `reboot_mode = 0x01` e attende l'ACK.
4. Il Feather scompare dalla porta CDC e appare l'unità mass-storage
   `RPI-RP2`.
5. L'utente copia il UF2 convalidato nella radice dell'unità. L'unità scompare
   dopo che la ROM ha accettato il file e riavviato la scheda.
6. L'app riscopre il Feather e legge la versione firmware tramite `GET_INFO`.

Se l'unità non appare o la copia non riesce, lasciare la scheda in BOOTSEL e
seguire il recupero UF2 manuale. Protocollo v1 non ha rollback a doppio slot;
si affida al bootloader ROM RP2040, disponibile indipendentemente dal firmware.

### 7. Ciclo di vita della sessione

#### 7.1 Connessione

Collegare il Feather via USB-C, attendere la porta seriale del sistema
operativo, farla rilevare dall'app, inviare `CMD_GET_INFO` e ricevere i dati
del dispositivo. Il PC confronta `protocol_version`; se non coincide mostra
un avviso e limita i comandi disponibili. A quel punto inizia il normale
funzionamento.

#### 7.2 Disconnessione

Se USB-C viene scollegata durante una transazione, il Feather non riceve
`CMD_UPLOAD_ABORT` né `CMD_UPLOAD_END`; l'upload è abbandonato. Al successivo
avvio (o se il Feather resta alimentato) la pulizia trova e scarta l'upload
incompleto. Il Feather non attende il PC; se era in standalone torna a quella
modalità. L'app PC deve gestire la disconnessione senza arrestarsi, mostrarne
l'avviso e tentare la riconnessione automatica.

#### 7.3 Timeout

Questi valori guidano il comportamento PC; il firmware non li impone:

| Situazione | Timeout | Azione PC alla scadenza |
| --- | --- | --- |
| Handshake di rilevamento, per porta | 500 ms | Chiude la porta e prova la successiva |
| Risposta a un comando standard | 5 s | Registra l'errore e chiude la connessione |
| Risposta a ogni blocco `UPLOAD_DATA` | 10 s | Annulla l'upload e registra l'errore |
| Risposta `UPLOAD_END` | 60 s | Registra l'errore e chiude la connessione |
| Risposta `VERIFY_PAYLOAD` | 120 s | Registra l'errore e chiude la connessione |
| Riconnessione dopo reboot | 5 s | Registra l'errore, senza riprovare |

`VERIFY_PAYLOAD` e `UPLOAD_END` hanno timeout lunghi perché leggono tutto il
payload e ne calcolano SHA-256; durante queste operazioni il PC mostra
l'avanzamento.

### 8. Gestione degli errori

#### 8.1 Pacchetto corrotto (CRC errato)

Il Feather scarta silenziosamente il pacchetto e il timeout del comando sul PC
gestisce il recupero. Se è la risposta ad avere CRC errato, il PC la scarta,
attende una ritrasmissione e infine applica il timeout.

#### 8.2 Comando sconosciuto

Per un codice CMD non riconosciuto, il Feather risponde con il codice
sconosciuto con bit 7 impostato e STATUS `STATUS_ERR_UNKNOWN_CMD`. Se il codice
risposta risulta anch'esso indefinito, usa CMD `0xFF` come risposta generica.

#### 8.3 Dispositivo occupato

Durante un upload, a ogni comando diverso da `CMD_UPLOAD_DATA`,
`CMD_UPLOAD_END` o `CMD_UPLOAD_ABORT` per quell'upload, il Feather risponde
`STATUS_ERR_BUSY`. Il PC non deve inviare altri comandi a un dispositivo
occupato, salvo i tre indicati.

#### 8.4 Recupero della sincronizzazione

CDC è un flusso di byte; un pacchetto ricevuto solo in parte, ad esempio dopo
un reset software durante la trasmissione, lascia il ricevitore fuori
sincronizzazione. Il recupero cerca nel flusso la sequenza MAGIC `4D 52 58 21`,
legge il resto dell'intestazione, DATA e CRC, quindi se il CRC fallisce scarta
il pacchetto e riprende la scansione. In questo modo il ricevitore si riallinea
senza un comando reset esplicito.

### 9. Versionamento del protocollo

La versione è un byte `protocol_version` restituito da `CMD_GET_INFO` e parte
da 1. Un'app compilata per la versione N rifiuta un dispositivo che dichiara
N+1 o superiore e opera con un dispositivo N solo quando la versione coincide.
Un firmware N accetta i comandi delle app N e risponde ai comandi sconosciuti
con `STATUS_ERR_UNKNOWN_CMD`, senza bloccarsi.

Versioni future possono aggiungere comandi. Il PC controlla la versione prima
di usare comandi non presenti nelle versioni precedenti. Campi opzionali
possono essere aggiunti alla fine di DATA in una revisione minore senza
incrementare la versione, se LENGTH permette al ricevitore di ignorarli in
modo sicuro. Le modifiche strutturali obbligatorie ai comandi esistenti
richiedono invece un incremento di versione.

### 10. Riferimento delle costanti del protocollo

I nomi e i valori numerici delle macro C sono parte dell'interfaccia e restano
in forma originale:

```c
#define MRX_MAGIC_0               0x4D
#define MRX_MAGIC_1               0x52
#define MRX_MAGIC_2               0x58
#define MRX_MAGIC_3               0x21
#define MRX_PROTOCOL_VERSION      1U
#define MRX_MAX_DATA_SIZE         4096U
#define MRX_MAX_PACKET_SIZE       4108U
#define MRX_HEADER_SIZE           10U
#define MRX_CRC_SIZE              2U

#define MRX_CMD_GET_INFO          0x01
#define MRX_CMD_GET_STATUS        0x02
#define MRX_CMD_GET_LOG           0x03
#define MRX_CMD_LIST_PAYLOADS     0x10
#define MRX_CMD_GET_PAYLOAD_INFO  0x11
#define MRX_CMD_UPLOAD_BEGIN      0x12
#define MRX_CMD_UPLOAD_DATA       0x13
#define MRX_CMD_UPLOAD_END        0x14
#define MRX_CMD_UPLOAD_ABORT      0x15
#define MRX_CMD_DELETE_PAYLOAD    0x16
#define MRX_CMD_SELECT_PAYLOAD    0x17
#define MRX_CMD_GET_SELECTED      0x18
#define MRX_CMD_VERIFY_PAYLOAD    0x19
#define MRX_CMD_REBOOT            0x20
#define MRX_CMD_FW_UPDATE_BEGIN   0x30
#define MRX_CMD_FW_UPDATE_DATA    0x31
#define MRX_CMD_FW_UPDATE_END     0x32

#define MRX_RESPONSE_BIT          0x80
#define MRX_STATUS_OK             0x00
#define MRX_STATUS_ERR_GENERAL    0x01
#define MRX_STATUS_ERR_NOT_FOUND  0x02
#define MRX_STATUS_ERR_CORRUPT    0x03
#define MRX_STATUS_ERR_FULL       0x04
#define MRX_STATUS_ERR_INVALID    0x05
#define MRX_STATUS_ERR_BUSY       0x06
#define MRX_STATUS_ERR_NO_SEL     0x07
#define MRX_STATUS_ERR_VERSION    0x08
#define MRX_STATUS_ERR_IO         0x09
#define MRX_STATUS_ERR_INCOMPLETE 0x0A
#define MRX_STATUS_ERR_ABORTED    0x0B
#define MRX_STATUS_ERR_UNKNOWN    0x0C

#define MRX_STATE_BOOTING         0x00
#define MRX_STATE_PC_MODE         0x01
#define MRX_STATE_STANDALONE      0x02
#define MRX_STATE_INJECTING       0x03
#define MRX_STATE_ERROR           0x04
#define MRX_REBOOT_NORMAL         0x00
#define MRX_REBOOT_BOOTLOADER     0x01
```


### Disposizione esatta dei campi DATA dei comandi

Gli offset che seguono sono relativi al campo DATA del pacchetto MRX. Gli interi
sono little-endian. Ogni risposta indicata come OK inizia con STATUS `0x00`.

#### `CMD_LIST_PAYLOADS`

Richiesta: nessun dato (`LENGTH = 0`). Risposta:

| Offset | Dim. | Campo | Descrizione |
| --- | --- | --- | --- |
| 0 | 1 | STATUS | `0x00` = OK |
| 1 | 2 | `count` | Numero di ID successivi, `uint16` |
| 3 | `4*N` | `ids` | Array di ID `uint32`; N = `count` |

#### `CMD_GET_PAYLOAD_INFO`

Richiesta: `payload_id`, offset 0, 4 byte (`uint32`). Risposta OK: STATUS a
offset 0, dimensione 1; ID confermato a offset 1, dimensione 4; `flags` a
offset 5, dimensione 4; dimensione payload `size` a offset 9, dimensione 8;
SHA-256 a offset 17, dimensione 32; nome UTF-8 terminato da NUL a offset 49,
dimensione 64. DATA totale: 113 byte. Gli errori possibili sono NOT_FOUND e
CORRUPT.

#### `CMD_UPLOAD_BEGIN`

La richiesta ha `expected_size` (offset 0, 8 byte, `uint64`), `name_length`
(offset 8, 1 byte, massimo 63) e `name` (offset 9, N byte, senza terminatore).
La risposta OK contiene STATUS a offset 0, 1 byte, e `assigned_id` a offset 1,
4 byte (`uint32`). Gli errori sono FULL, BUSY o INVALID_PARAM.

#### `CMD_UPLOAD_DATA`

La richiesta è composta da `payload_id` (offset 0, 4 byte), `reserved` (offset
4, 4 byte, zero), `chunk_offset` (offset 8, 8 byte) e `chunk_data` (offset 16,
N byte, grezzi, N = LENGTH - 16 e massimo 4080). La risposta OK contiene
STATUS a offset 0, 1 byte, e `bytes_received` a offset 1, 8 byte (`uint64`).
Gli errori sono NOT_FOUND, IO, INVALID_PARAM o FULL; un errore annulla e
scarta già la transazione attiva.

#### `CMD_UPLOAD_END`

La richiesta contiene ID a offset 0 per 4 byte, dimensione prevista a offset 4
per 8 byte e SHA-256 prevista a offset 12 per 32 byte. La risposta OK contiene
STATUS a offset 0, ID a offset 1 per 4 byte, `actual_size` a offset 5 per 8
byte e `actual_sha256` a offset 13 per 32 byte. Gli errori sono CORRUPT per
hash diverso, IO per errore rilettura, INVALID_PARAM per dimensione diversa o
NOT_FOUND. Qualunque risposta non OK significa che i dati parziali sono già
stati eliminati.

#### `CMD_UPLOAD_ABORT`

Richiesta: ID a offset 0, 4 byte. Risposta: STATUS a offset 0, 1 byte, OK o
NOT_FOUND. NOT_FOUND indica che non c'è un upload attivo per quell'ID.

#### `CMD_DELETE_PAYLOAD`

Richiesta: ID a offset 0, 4 byte. Risposta OK: STATUS a offset 0, 1 byte;
`selection_cleared` a offset 1, 1 byte (1 se la selezione è stata cancellata,
altrimenti 0). Errori: NOT_FOUND o IO. La GUI aggiorna lo stato se la selezione
è stata cancellata.

#### `CMD_SELECT_PAYLOAD`

Richiesta: ID a offset 0, 4 byte. Risposta: STATUS a offset 0, 1 byte; i valori
possibili sono OK, NOT_FOUND, INCOMPLETE e IO. INCOMPLETE segnala un payload
senza flag VALID.

#### `CMD_GET_SELECTED`

Richiesta: nessun dato. Risposta OK: STATUS a offset 0, 1 byte; `selected_id`
a offset 1, 4 byte (`uint32`), zero se nessuno è selezionato. Per la GUI zero
equivale a NO_SELECTION.

#### `CMD_VERIFY_PAYLOAD`

Richiesta: ID a offset 0, 4 byte. Risposta OK: STATUS a offset 0, 1 byte;
`match` a offset 1, 1 byte; hash dai metadati a offset 2, 32 byte; hash
ricalcolato a offset 34, 32 byte. DATA totale: 66 byte. Una risposta STATUS OK
con `match = 0` significa che gli hash differiscono. In caso di mancato
completamento gli errori sono NOT_FOUND, CORRUPT e IO.

#### `CMD_REBOOT`

Richiesta: `reboot_mode` a offset 0, 1 byte (`0x00` avvio normale, `0x01`
BOOTSEL). Risposta: STATUS OK a offset 0, 1 byte. Il firmware invia la
risposta, serve TinyUSB per almeno 100 ms e poi riavvia; il PC chiude la
connessione dopo l'ACK.

#### `CMD_GET_LOG` - disposizione dei campi

Richiesta DATA: `max_bytes` all'offset 0, dimensione 2 byte (`uint16`), cioè
massimo numero di byte di testo da restituire (limite massimo 4000). Risposta
OK: STATUS all'offset 0, dimensione 1; `text_length` all'offset 1, dimensione
2 byte (`uint16`); `log_text` all'offset 3, N byte, righe separate da newline
e senza terminatore NUL. Se STATUS non è OK, la risposta può non contenere
PAYLOAD aggiuntivo oppure includere un byte che descrive l'errore, a seconda
del comando.

## Specifica dell'architettura storage

Versione del documento: 1.0


### 1. Principi di progettazione

Il sottosistema di memorizzazione rispetta tre regole inderogabili:

1. **Il dispositivo non deve presumere di essere collegato a un PC.** Tutti i
   dati persistenti devono restare leggibili e utilizzabili dopo un ciclo di
   alimentazione, anche senza PC.
2. **Una perdita di alimentazione non deve lasciare il dispositivo
   permanentemente danneggiato.** Una scrittura incompleta deve poter essere
   rilevata e recuperata al successivo avvio.
3. **Il livello storage è un confine di astrazione.** Il codice superiore non
   dipende da LittleFS, offset flash, settori o pagine. Sostituendo LittleFS
   con un altro filesystem, il codice sopra l'API storage non dovrebbe cambiare.

### 2. Architettura a livelli

```text
Codice applicativo
  -> Payload Manager (payload_manager.h)
  -> Storage Manager (storage.h)
  -> LittleFS
  -> driver block device (rp2040_flash_bd.c)
  -> hardware/flash.h (Pico SDK)
  -> controller QSPI RP2040
  -> flash QSPI esterna da 8 MiB
```

Il codice RCM accede ai payload esclusivamente tramite Payload Manager, mai
direttamente tramite Storage Manager. La configurazione viene gestita solo dal
modulo config (`config.h`), che internamente usa Storage Manager.

### 3. Struttura delle directory LittleFS

La radice LittleFS inizia all'offset flash `0x100000` e contiene:

```text
/
+-- config/device.bin
+-- payloads/
|   +-- 00000001/metadata.bin
|   |             payload.bin
|   +-- 00000002/metadata.bin
|   |             payload.bin
|   +-- ...
+-- system/filesystem.version
```

#### 3.1 Convenzioni di denominazione

Le directory payload usano l'ID formattato come esadecimale minuscolo di
8 caratteri, riempito con zeri iniziali. Esempi: ID 1 -> `00000001`, ID 2 ->
`00000002`, ID 255 -> `000000ff`. La convenzione rende l'ordinamento coerente
ed evita ambiguità.

#### 3.2 File temporanei di upload

Durante una transazione attiva possono esistere
`metadata.bin.uploading` e `payload.bin.uploading` nella directory del payload.
Il suffisso `.uploading` è un indicatore: la presenza anche di uno solo di
questi file significa che l'upload è incompleto e che l'intera directory va
scartata al successivo mount. I payload finali non hanno mai tale suffisso.

### 4. Formato binario della configurazione dispositivo

Percorso: `/config/device.bin`. Contiene lo stato essenziale e persistente,
viene scritto quando questo cambia, e deve sopravvivere e poter essere
recuperato da una perdita di alimentazione. Gli interi con più byte sono
little-endian.

| Offset | Dim. | Tipo | Campo | Descrizione |
| --- | --- | --- | --- | --- |
| 0 | 4 | `uint8_t[4]` | `magic` | Byte ASCII `MRX!` |
| 4 | 4 | `uint32_t` | `format_version` | Versione schema, attualmente 1 |
| 8 | 4 | `uint32_t` | `config_write_count` | Incrementato ad ogni salvataggio |
| 12 | 16 | `uint8_t[16]` | `device_id` | ID dispositivo univoco a 128 bit |
| 28 | 4 | `uint32_t` | `selected_payload_id` | ID attivo; 0 = nessuna selezione |
| 32 | 1 | `uint8_t` | `boot_mode` | Modalità di avvio |
| 33 | 3 | `uint8_t[3]` | `reserved_0` | Riservato, deve essere 0 |
| 36 | 4 | `uint32_t` | `next_payload_id` | Prossimo ID da assegnare |
| 40 | 4 | `uint32_t` | `flags` | Flag configurazione |
| 44 | 4 | `uint8_t[4]` | `reserved_1` | Riservato, deve essere 0 |
| 48 | 4 | `uint32_t` | `crc32` | CRC-32 dei byte 0-47 |

Dimensione totale: 52 byte.

#### 4.1 Valore MAGIC

La sequenza è fissa `4D 52 58 21` (`MRX!`). Anche se il valore simbolico è
`0x4D525821`, i byte MAGIC sono memorizzati in ordine ASCII; gli altri interi
multibyte restano little-endian. Questo evita di invertire la sequenza quando
si serializza MAGIC come intero little-endian.

```c
#define MRX_CONFIG_MAGIC  0x4D525821UL
```

Se MAGIC non coincide, la configurazione è assente o corrotta e il firmware
usa i valori predefiniti.

#### 4.2 Versione formato

```c
#define MRX_CONFIG_FORMAT_VERSION  1U
```

Se `format_version` differisce da quella con cui è stato compilato il firmware,
la configurazione va rifiutata come incompatibile, non interpretata. Versioni
future potranno aggiungere una procedura di migrazione.

#### 4.3 Modalità di avvio

```c
#define MRX_BOOT_MODE_NORMAL        0x00   // Rileva automaticamente PC o standalone
#define MRX_BOOT_MODE_STANDALONE    0x01   // Salta la modalità PC e avvia l'host USB
```

Versioni future del formato possono aggiungere altre modalità.

#### 4.4 Flag configurazione

```c
#define MRX_CONFIG_FLAG_FS_INIT     (1UL << 0)  // Filesystem inizializzato
```

I bit rimanenti sono riservati e devono valere zero.

#### 4.5 ID del payload selezionato

Il valore 0 significa che non c'è payload selezionato; il dispositivo non
esegue l'iniezione standalone in tale stato. Un valore non zero è attendibile
solo se la directory corrispondente esiste, contiene `metadata.bin` valido e i
metadati indicano un payload completo. All'avvio il firmware deve verificare
questa referenza, non fidarsi ciecamente del valore.

#### 4.6 Prossimo ID payload

`next_payload_id` parte da 1 e cresce quando viene creato un nuovo payload con
successo; non viene decrementato. Così gli ID restano univoci per tutta la
vita del dispositivo, anche dopo eliminazioni.

#### 4.7 CRC-32

Si usa il polinomio standard `0xEDB88320`, riflesso, compatibile con Ethernet e
ZIP. Il CRC copre i byte da 0 a 47, cioè l'intera struttura escluso il campo
CRC. Se non coincide, la configurazione è corrotta e il firmware passa ai
valori predefiniti.

#### 4.8 Configurazione iniziale predefinita

In assenza di configurazione valida:

```c
DeviceConfig defaults = {
    .magic               = MRX_CONFIG_MAGIC,
    .format_version      = MRX_CONFIG_FORMAT_VERSION,
    .config_write_count  = 0,
    .device_id           = { /* generato dal chip ID RP2040 al primo avvio */ },
    .selected_payload_id = 0,
    .boot_mode           = MRX_BOOT_MODE_NORMAL,
    .reserved_0          = { 0, 0, 0 },
    .next_payload_id     = 1,
    .flags               = MRX_CONFIG_FLAG_FS_INIT,
    .reserved_1          = { 0, 0, 0, 0 },
    .crc32               = /* calcolato */
};
```

Il chip RP2040 fornisce un ID univoco a 64 bit tramite
`pico_unique_board_id`. I primi 8 byte di `device_id` contengono questo ID; gli
altri 8 sono zero.

### 5. Formato binario dei metadati payload

Percorso: `/payloads/<hex-id>/metadata.bin`. I metadati permettono al firmware
di elencare payload, verificare selezioni e inviare informazioni all'app senza
leggere prima il binario completo. Gli interi multibyte sono little-endian.

| Offset | Dim. | Tipo | Campo | Descrizione |
| --- | --- | --- | --- | --- |
| 0 | 4 | `uint8_t[4]` | `magic` | Byte ASCII `MRXz` |
| 4 | 4 | `uint32_t` | `format_version` | Versione schema, attualmente 1 |
| 8 | 4 | `uint32_t` | `payload_id` | ID, uguale al nome della directory |
| 12 | 4 | `uint32_t` | `flags` | Flag payload |
| 16 | 8 | `uint64_t` | `payload_size` | Dimensione di `payload.bin` |
| 24 | 32 | `uint8_t[32]` | `sha256` | Hash SHA-256 del binario |
| 56 | 64 | `char[64]` | `name` | Nome UTF-8 terminato da NUL |
| 120 | 4 | `uint32_t` | `reserved_0` | Riservato, deve essere 0 |
| 124 | 4 | `uint32_t` | `crc32` | CRC-32 dei byte 0-123 |

Dimensione totale: 128 byte. MAGIC è la sequenza fissa `4D 52 58 7A`
(`MRXz`), memorizzata in ordine ASCII, mentre gli altri interi multibyte sono
little-endian.

```c
#define MRX_META_MAGIC         0x4D52587AUL
#define MRX_META_FORMAT_VERSION 1U
#define MRX_PAYLOAD_FLAG_VALID      (1UL << 0)  // Completo e verificato
#define MRX_PAYLOAD_FLAG_UPLOADING  (1UL << 1)  // Upload in corso; non usare
```

Un payload è utilizzabile solo se `VALID` è impostato e `UPLOADING` è
disattivato.

| VALID | UPLOADING | Significato |
| --- | --- | --- |
| 0 | 1 | Upload in corso, normale durante la transazione |
| 1 | 0 | Completo e utilizzabile |
| 0 | 0 | Non dovrebbe esistere; trattare come corrotto |
| 1 | 1 | Non dovrebbe esistere; trattare come corrotto |

SHA-256 è calcolato sull'intero payload e memorizzato dopo la verifica finale
dell'upload. Serve per confermare l'integrità dopo l'upload, per il comando
`VERIFY_PAYLOAD` e, facoltativamente, durante la verifica della selezione
all'avvio. Va calcolato a flusso e a blocchi, senza caricare l'intero binario
nella SRAM, che sull'RP2040 è 264 KiB.

`name` è una stringa UTF-8 terminata da NUL con massimo 63 caratteri
stampabili. L'ultimo byte è sempre `0x00`. Deriva dal nome file dell'upload ed
è ripulito prima di essere salvato: caratteri non ASCII stampabili sono
sostituiti con `_`; separatori di percorso e caratteri speciali non sono
ammessi.

### 6. File versione del filesystem

Percorso: `/system/filesystem.version`. Indica che il filesystem è stato
inizializzato da una versione nota del firmware e memorizza la versione schema,
così firmware futuri possono rilevare e gestire filesystem precedenti.

| Offset | Dim. | Tipo | Campo | Descrizione |
| --- | --- | --- | --- | --- |
| 0 | 4 | `uint8_t[4]` | `magic` | Byte ASCII `MRXf` |
| 4 | 4 | `uint32_t` | `fs_schema_version` | Versione layout, attualmente 1 |
| 8 | 4 | `uint32_t` | `reserved` | Deve essere 0 |
| 12 | 4 | `uint32_t` | `crc32` | CRC-32 dei byte 0-11 |

Dimensione totale: 16 byte. MAGIC è `4D 52 58 66` (`MRXf`), memorizzato in
ordine ASCII; gli altri interi multibyte sono little-endian.

```c
#define MRX_FS_VERSION_MAGIC   0x4D525866UL
#define MRX_FS_SCHEMA_VERSION  1U
```

### 7. Transazione di upload

L'upload è transazionale: un payload diventa attivo e utilizzabile solo quando
tutti i passaggi si sono conclusi correttamente.

#### 7.1 Stati della transazione

```text
IDLE
  -> ricevuto UPLOAD_BEGIN
UPLOAD_ACTIVE
  -> ricevuti tutti i blocchi
  -> ricevuto UPLOAD_END
VERIFYING
  -> verifica fallita -> CLEANUP -> IDLE (errore segnalato)
  -> verifica riuscita -> COMMITTING -> IDLE (successo segnalato)
```

#### 7.2 Procedura dettagliata

**Passaggio 1: `UPLOAD_BEGIN`.** Il PC invia dimensione prevista e nome. Il
livello storage convalida il nome, legge `next_payload_id` dalla
configurazione, crea `/payloads/<hex-id>/`, crea `metadata.bin.uploading` con
`flags = MRX_PAYLOAD_FLAG_UPLOADING`, dimensione zero e SHA-256 tutta zero,
completa gli altri campi con nome e ID assegnato, apre
`payload.bin.uploading` in scrittura e restituisce l'ID. Se si perde
l'alimentazione a questo punto, al riavvio i file `.uploading` vengono
riconosciuti e scartati.

**Passaggio 2: `UPLOAD_DATA`, ripetuto per ogni blocco.** Il PC invia blocchi
sequenziali, che vengono scritti in `payload.bin.uploading`; il contesto SHA-256
aggiornato a ogni blocco resta in SRAM. Se una scrittura fallisce, il firmware
chiude ed elimina il file payload temporaneo, elimina i metadati temporanei e
la directory, poi segnala l'errore al PC.

**Passaggio 3: `UPLOAD_END`.** Il PC comunica che i dati sono terminati e
fornisce dimensione e SHA-256 attese. Il firmware scarica e chiude il file,
confronta dimensione effettiva e prevista, finalizza l'hash incrementale e lo
confronta con quello atteso. Se dimensione o hash non coincidono elimina tutti
i file e la directory e restituisce un errore. Poi rilegge dalla flash il
payload a blocchi e ricalcola l'hash per verificare la scrittura. Se la lettura
non coincide, elimina di nuovo la directory e segnala errore. Se la verifica
riesce, rinomina atomicamente `payload.bin.uploading` in `payload.bin`, imposta
nei metadati il flag `VALID`, dimensione reale, hash verificato e CRC-32
aggiornato, quindi rinomina atomicamente `metadata.bin.uploading` in
`metadata.bin`. Incrementa `next_payload_id`, salva la configurazione e
comunica ID, dimensione e hash al PC.

#### 7.3 Perdita di alimentazione durante l'upload

Al successivo avvio può verificarsi uno di questi casi:

- La directory non esiste: non serve alcuna pulizia.
- La directory contiene file `.uploading`: l'upload incompleto viene rilevato
  e scartato.
- `payload.bin` esiste ma `metadata.bin.uploading` esiste ancora, cioè si è
  interrotta l'alimentazione tra le due rinomine: la directory non è completa
  e viene scartata.
- Esistono `payload.bin` e `metadata.bin`: il commit è terminato e il payload
  è valido.

#### 7.4 Pulizia all'avvio

Ad ogni mount, prima di qualsiasi altra operazione, il livello storage elimina
`/config/device.bin.tmp` se presente: è il residuo di una scrittura
configurazione interrotta, mentre `device.bin` esistente resta valido. Poi
esamina `/payloads/` e rimuove ricorsivamente ogni directory che contiene un
file con suffisso `.uploading`.

### 8. Transazione di selezione

La selezione modifica `selected_payload_id` nella configurazione. Il firmware
riceve `SELECT_PAYLOAD`, verifica che la directory esista, legge e interpreta
`metadata.bin`, controlla MAGIC, versione formato e CRC-32, verifica che
`VALID` sia impostato e `UPLOADING` no, aggiorna il valore in memoria, salva
`/config/device.bin` e segnala il successo al PC.

Il salvataggio usa un file temporaneo e una rinomina esplicita invece di
sovrascrivere direttamente `device.bin`. Questo rende l'atomicità esplicita
anche se LittleFS usa internamente copy-on-write:

```text
1. Serializzare la nuova configurazione in un buffer.
2. Calcolare il CRC-32 del buffer.
3. Scrivere /config/device.bin.tmp.
4. Eseguire flush e chiudere il file.
5. Rileggere il file temporaneo e verificare il CRC-32.
6. Rinominarlo in /config/device.bin (operazione atomica LittleFS).
```

Prima del passaggio 6, in caso di perdita di alimentazione, `device.bin.tmp`
resta e `device.bin` conserva l'ultima configurazione valida; il file
temporaneo viene eliminato al mount. Durante la rinomina, LittleFS garantisce
che resti valido il vecchio o il nuovo nome, mai nessuno dei due.

#### 8.1 Convalida prima della selezione

Prima del commit, l'operazione verifica esplicitamente la validità del payload.
Un payload presente sul disco ma incompleto o con CRC metadati errato non può
essere selezionato.

#### 8.2 Convalida della selezione all'avvio

Dopo aver caricato la configurazione, ad ogni avvio il firmware controlla se
`selected_payload_id` è zero. Se è zero non c'è selezione. Altrimenti verifica
che l'ID esista, che `metadata.bin` sia leggibile e abbia CRC corretto e che il
flag `VALID` sia impostato. Se un controllo fallisce, azzera
`selected_payload_id`, salva la configurazione e registra un avviso. Senza
nuova selezione non può quindi avviare un'iniezione con un riferimento assente
o corrotto.

### 9. Transazione di eliminazione

Per `DELETE_PAYLOAD` il firmware verifica che il payload esista, annota se è
selezionato, elimina `payload.bin`, poi `metadata.bin` e la directory. Se era
selezionato, imposta `selected_payload_id = 0` e salva la configurazione.
Comunica al PC l'esito e se la selezione è stata cancellata.

Il dispositivo non seleziona automaticamente un altro payload: potrebbe
avviare quello sbagliato. L'utente deve sceglierne uno dalla GUI prima che
l'iniezione standalone sia di nuovo disponibile.

Se manca l'alimentazione prima dell'eliminazione del primo file, lo stato non
cambia. Se si interrompe tra eliminazione dei file e della directory, al
riavvio metadati mancanti o corrotti rendono il payload non valido e l'entry
viene ripulita. Se la directory è stata rimossa ma la configurazione la
referenzia ancora, la convalida all'avvio cancella e salva la selezione.

### 10. Verifica dell'integrità

Il comando `VERIFY_PAYLOAD` legge i metadati, ne convalida CRC-32, legge
`payload.bin` a blocchi, calcola SHA-256 del contenuto e lo confronta con
l'hash nei metadati, poi comunica il risultato al PC. La dimensione del blocco
dipende dal buffer SRAM disponibile e deve essere molto inferiore alla SRAM
totale; 4096 byte (un blocco LittleFS) sono adeguati.

La verifica non modifica la flash. In caso di errore non azzera
automaticamente `VALID`: segnala la lettura fallita o la corruzione e attende
un'azione esplicita dell'utente, come eliminare o caricare di nuovo il payload.

### 11. Stati di errore dello storage

Il livello storage restituisce codici espliciti, che il codice superiore deve
gestire. Il livello protocollo li traduce in codici protocollo e non espone al
PC i codici interni LittleFS.

| Codice | Significato |
| --- | --- |
| `STORAGE_OK` | Operazione riuscita |
| `STORAGE_ERR_NOT_MOUNTED` | LittleFS non è montato |
| `STORAGE_ERR_NOT_FOUND` | File o directory inesistente |
| `STORAGE_ERR_CORRUPT` | MAGIC o CRC non valido |
| `STORAGE_ERR_VERSION` | `format_version` non supportata |
| `STORAGE_ERR_IO` | Operazione I/O LittleFS fallita |
| `STORAGE_ERR_FULL` | Filesystem senza spazio |
| `STORAGE_ERR_INVALID_ARG` | Argomento non valido |
| `STORAGE_ERR_BUSY` | È già in corso una transazione upload |
| `STORAGE_ERR_NO_SELECTION` | Nessun payload selezionato |
| `STORAGE_ERR_INCOMPLETE` | Payload non marcato valido |

### 12. Specifica API storage

Queste dichiarazioni definiscono il contratto dell'API offerta al resto del
firmware, non i dettagli di implementazione.

```c
// Ciclo di vita filesystem
storage_err_t storage_init(void);       // Inizializza storage e block device
storage_err_t storage_mount(void);      // Monta/formattazione iniziale; pulisce upload interrotti
storage_err_t storage_unmount(void);    // Smonta prima di sleep profondo o reset

// Configurazione dispositivo
storage_err_t config_load(DeviceConfig *config_out); // Valori default se file manca o è corrotto
storage_err_t config_save(const DeviceConfig *config); // Salvataggio atomico

// Ciclo di vita payload
storage_err_t payload_begin_upload(const char *name, uint64_t expected_size,
                                   uint32_t *id_out);
storage_err_t payload_write_chunk(uint32_t id, uint64_t offset,
                                  const void *data, size_t length,
                                  uint64_t *bytes_received_out);
storage_err_t payload_finalize_upload(uint32_t id, uint64_t expected_size,
                                      const uint8_t expected_sha256[32]);
storage_err_t payload_abort_upload(uint32_t id);

// Accesso ai payload
storage_err_t payload_exists(uint32_t id, bool *exists_out);
storage_err_t payload_get_info(uint32_t id, PayloadInfo *info_out);
storage_err_t payload_list(uint32_t *id_array, size_t max_count,
                           size_t *count_out);
storage_err_t payload_read_chunk(uint32_t id, uint64_t offset, void *buffer,
                                 size_t length, size_t *bytes_read_out);
storage_err_t payload_delete(uint32_t id, bool *selection_cleared_out);
storage_err_t payload_verify(uint32_t id, bool *matches_out,
                             uint8_t actual_sha256_out[32]);

// Gestione selezione
storage_err_t payload_set_selected(uint32_t id);
storage_err_t payload_get_selected(uint32_t *id_out);
storage_err_t payload_clear_selected(void);
```

`storage_init` va chiamata prima delle altre funzioni; `storage_mount` crea e
formatta al primo avvio e ripulisce upload incompleti. `config_load` usa valori
predefiniti se la configurazione manca o è corrotta. `payload_begin_upload`
assegna l'ID e crea la struttura temporanea; i blocchi di
`payload_write_chunk` devono essere contigui. `payload_finalize_upload`
verifica dimensione e hash e scarta l'upload se fallisce. `payload_read_chunk`
è usata dal livello RCM per leggere dalla flash; `payload_verify` calcola
l'hash e lo confronta con quello nei metadati. `payload_set_selected` valida il
payload prima di modificare la configurazione e `payload_get_selected`
restituisce `STORAGE_ERR_NO_SELECTION` se non c'è una selezione.

#### 12.1 Struttura `PayloadInfo`

```c
typedef struct {
    uint32_t id;
    uint32_t flags;
    uint64_t size;
    uint8_t  sha256[32];
    char     name[64];
} PayloadInfo;
```

Espone i campi dei metadati utili ai livelli superiori, non i dettagli del
formato binario come MAGIC, CRC o versione.

#### 12.2 Struttura `DeviceConfig`

```c
typedef struct {
    uint32_t selected_payload_id;
    uint32_t next_payload_id;
    uint32_t config_write_count;
    uint8_t  device_id[16];
    uint8_t  boot_mode;
    uint32_t flags;
} DeviceConfig;
```

Espone solo i campi usati dal firmware; MAGIC, versione e CRC sono gestiti
internamente dal modulo config.

### 13. Riepilogo del recupero dopo una perdita di alimentazione

| Evento | Rilevamento | Recupero |
| --- | --- | --- |
| Perdita durante upload | File `.uploading` al mount | Elimina l'intera directory payload |
| Perdita durante selezione | CRC configurazione non valido | Ripristina configurazione precedente o default |
| Perdita durante eliminazione | Payload referenziato mancante all'avvio | Azzera selezione e salva configurazione |
| File configurazione mancante | `/config/device.bin` assente | Scrive la configurazione predefinita |
| CRC metadati errato | Controllo CRC alla lettura | Payload non valido, non utilizzarlo |
| Filesystem non formattato | Mount LittleFS fallito | Formatta e reinizializza |

In tutti questi casi il firmware continua a funzionare. Solo l'assenza di un
payload valido selezionato impedisce l'iniezione standalone; il dispositivo
resta in attesa sicura finché non viene ricollegato al PC e riconfigurato.

### 14. Capacità indicativa

Con 7 MiB LittleFS e dimensioni payload tipiche:

| Dimensione payload | Numero massimo approssimativo |
| --- | --- |
| 500 KB | circa 14 |
| 1 MB | circa 7 |
| 2 MB | circa 3 |

L'overhead LittleFS per payload è contenuto: una directory e due file, circa
5-10 settori considerando il livellamento dell'usura. Prima di un upload l'app
PC dovrebbe interrogare lo spazio libero e segnalare lo spazio insufficiente,
anziché rilevare il problema a trasferimento già iniziato.

## Stato di verifica

### Risultati lato host e lato compilazione

Eseguiti dalla radice del progetto:

```bash
python tools/test_mrx_protocol.py
python tools/test_mrx_gui_pty.py
cmake --build firmware/build --parallel
```

- Suite di conformità host: **18/18 test superati**. Copre framing del
  protocollo firmware e dispatch del riavvio; encoder di pacchetti, CRC, parser
  di stream e validazione UF2 lato desktop; storage, configurazione e gestione
  dei payload contro una flash NOR simulata da 8 MiB; richieste non valide e
  incomplete; e la regione di linker da 1 MiB generata. La suite compila
  direttamente i sorgenti C del firmware.
- Il test di recupero di LittleFS stampa due messaggi `Corrupted dir pair`
  previsti mentre verifica il recupero da uno stato del filesystem
  deliberatamente danneggiato; il test passa dopo l'esecuzione di quel percorso
  di recupero.
- Suite di rilevamento desktop: **3/3 test superati** su Linux, esercitando il
  vero worker di rilevamento contro un dispositivo MRX finto servito su uno
  pseudo-terminale. Viene saltata su Windows, dove gli pseudo-terminali non
  esistono.
- Compilazione out-of-tree pulita con Pico SDK: **superata**, con generazione
  di `mrx_loader.elf`, `.bin`, `.hex` e `.uf2`.
- Dimensione del binario firmware: **116.824 byte**, sotto la partizione
  firmware da 1 MiB.
- Output UF2: `firmware/build/mrx_loader.uf2`, 233.984 byte, accettato dal
  validatore desktop come 457 blocchi della famiglia RP2040 all'interno della
  partizione firmware.
- L'applicazione desktop è stata costruita con successo sia su Windows sia su
  WSL2 (Ubuntu 26.04, Python 3.14.4, PySide6 6.9.3, pyserial 3.5) e
  l'enumerazione delle porte seriali ha funzionato su entrambe le piattaforme.

Si tratta di risultati lato host e lato compilazione. Non sostituiscono le
prove su hardware.

### Registro della compilazione

Il firmware è stato compilato da una directory CMake pulita usando la
definizione scheda Feather RP2040 USB Host, con Pico SDK 2.3.1
(`079c6f39023649b154152db30f1d781e884879bc`), ARM GNU Toolchain 15.2.1 (build
`15.2.1 20251203`), TinyUSB 0.18.0
(`86ad6e56c1700e85f1c5678607a762cfe3aa2f47`), Pico-PIO-USB 0.7.2 (commit
`3c1eec3`), LittleFS 2.11.3, picotool 2.3.1, CMake 3.29.2 e Ninja 1.12.0.
Lo snapshot di LittleFS non conserva i metadati del commit upstream.

Il `.bin` risultante è di 116.824 byte. Lo script di linker limita la regione
FLASH del firmware a 1 MiB. Il UF2 è di 233.984 byte e contiene 457 blocchi; il
validatore desktop ha accettato il family ID RP2040 e tutti gli indirizzi di
destinazione entro la partizione firmware.

## Limitazioni note

- Il protocollo v1 aggiorna il firmware tramite il bootloader mass storage
  BOOTSEL dell'RP2040. Non fornisce rollback a doppio slot né copia automatica
  del UF2.
- L'applicazione desktop chiede all'utente di copiare il UF2 validato
  sull'unità `RPI-RP2` e non scrive su unità rimovibili arbitrarie.
- L'attuale identità USB `0x1209:0x0001` è valida solo per lo sviluppo.
- USB non può confermare che un payload sia partito. Con Hekate, la conferma
  visibile del successo è la sua schermata.
- L'applicazione desktop si avvia manualmente con
  `python -m tools.mrx_gui.app`: non ci sono servizi in background,
  installatori di sistema o voci di avvio automatico al login.
- Convenzione del repository: i sorgenti mantenuti sono tenuti privi di
  commenti, quindi la logica è documentata qui e non nel codice.
  `firmware/third_party/` è codice upstream vendorizzato ed è conservato
  integralmente, licenze comprese.

## Avvisi di licenza di terze parti

I testi delle licenze seguenti sono mantenuti nell'originale inglese per non
alterarne i termini legali. Sono incluse le licenze delle librerie vendorizzate
e delle componenti del Pico SDK usate per compilare il firmware.

Le dipendenze desktop PySide6 e pyserial vengono installate da PyPI e non sono
incluse nel repository come librerie vendorizzate né nel UF2. Se in futuro verrà
distribuito un eseguibile desktop impacchettato, occorrerà controllare le
versioni esatte incluse e aggiungere gli avvisi richiesti per quelle dipendenze.

#### littlefs (Arm BSD 3-Clause)

Copyright (c) 2022, The littlefs authors.
Copyright (c) 2017, Arm Limited. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
- Neither the name of ARM nor the names of its contributors may be used to
  endorse or promote products derived from this software without specific prior
  written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Traduzione informativa italiana della licenza precedente (il testo legale
originale inglese riportato sopra resta incluso):

Copyright (c) 2022, autori di littlefs. Copyright (c) 2017, Arm Limited. Tutti
i diritti riservati.

La ridistribuzione e l'uso in forma sorgente o binaria, con o senza modifiche,
sono permessi se vengono rispettate queste condizioni: le ridistribuzioni del
codice sorgente conservano l'avviso di copyright, l'elenco delle condizioni e
la presente esclusione di garanzie; le ridistribuzioni binarie riproducono gli
stessi avvisi nella documentazione o negli altri materiali forniti insieme al
prodotto; i nomi ARM e dei suoi collaboratori non sono usati per approvare o
promuovere prodotti derivati senza previo permesso scritto.

Il software è fornito "così com'è". I titolari del copyright e i collaboratori
escludono ogni garanzia espressa o implicita, incluse commerciabilità e idoneità
a uno scopo particolare. Non sono responsabili per danni diretti, indiretti,
incidentali, speciali, esemplari o consequenziali, inclusi acquisto di beni o
servizi sostitutivi, perdita d'uso, dati o profitti o interruzione dell'attività,
qualunque sia la causa o il fondamento giuridico, anche se informati della
possibilità di tali danni.

#### Pico-PIO-USB (MIT)

Copyright (c) 2021 sekigon-gonnoc

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

#### TinyUSB (MIT)

Copyright (c) 2018, hathach (tinyusb.org)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

#### Raspberry Pi Pico SDK (BSD 3-Clause)

Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
