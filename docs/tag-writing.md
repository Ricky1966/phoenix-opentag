# Scrittura OpenPrintTag — prima implementazione

Firmware `0.6.0-dev`, sperimentale fino alla prova su ESP32 + PN5180.
Supporto iniziale: **NXP ICODE SLIX2, UID E00401…, 80 blocchi da 4 byte (320 byte)**.
Pin, istanza PN5180 e inizializzazione hardware restano quelli del lettore collaudato.

## Contratto e limiti

Il firmware riceve **l'immagine NFC-V completa**: Capability Container, TLV,
NDEF e payload OpenPrintTag. Non ricostruisce il tag dai pochi campi che espone
il lettore: campi aggiuntivi, regioni e altri record presenti nell'immagine
accettata vengono conservati byte per byte. Il futuro fork di Spoolman dovrà
produrre questa immagine con l'encoder/schema ufficiale dopo la compilazione
dei campi. Questa PR non aggiunge ancora il form, l'encoder o il pulsante web.

Si tratta di **inizializzazione/reinizializzazione completa**, non di aggiornamento
del peso residuo o di una singola regione. Con `replace` si autorizza a sostituire
anche i dati precedenti sconosciuti al lettore. Non usare questo comando per
aggiornamenti automatici della regione ausiliaria: la specifica richiede di
preservare offset, metadati, campi ignoti e regioni non coinvolte.

Layout accettato: CC di 4 byte `E1 40 <capacità/8> 01`, primo TLV NDEF in byte 4,
lunghezza corta o estesa, terminatore `FE`, eventuale padding zero. L'immagine
deve essere accettata dal parser FFF del progetto e stare nei 320 byte fisici;
i byte fisici non forniti sono azzerati. Il parser verifica struttura e campi
che conosce, **non sostituisce la validazione completa dello schema ufficiale**
su tutti i campi extra. Immagini non supportate vengono rifiutate prima di scrivere.
Il file ufficiale `tag_03.bin` è un caso malformato, intenzionalmente rifiutato.

Non si impostano password, blocchi permanenti, AFI o DSFID. I blocchi da modificare
devono risultare scrivibili; se non è possibile leggere lo stato di protezione
si interrompe l'operazione. Tag protetti da password non sono supportati.
Un tag è considerato vergine solo se tutti i 320 byte sono zero: un tag già
formattato richiede `replace` anche se non contiene una bobina.

Riferimenti: [specifica ufficiale](https://github.com/OpenPrintTag/openprinttag-specification),
[regole NFC e aggiornamento delle regioni](https://github.com/OpenPrintTag/openprinttag-specification/blob/main/docs_src/nfc_technical_details.md),
[encoder di riferimento](https://github.com/OpenPrintTag/openprinttag-specification/blob/main/utils/nfc_initialize.py).

## Algoritmo

1. Legge UID, geometria e tutti i blocchi, conservando uno snapshot.
2. Prima del commit verifica l'immagine, rilegge il tag e confronta UID e contenuto
   con lo snapshot. Un tag cambiato nel frattempo viene rifiutato.
3. Richiede autorizzazione esplicita alla sostituzione se esistono dati. Se i byte
   sono già identici risponde `unchanged`, senza alcuna scrittura.
4. Legge le protezioni; scrive un TLV NDEF vuoto nel blocco 1, poi solo i blocchi
   differenti e il CC. Ogni blocco scritto viene immediatamente riletto.
5. Confronta tutta l'immagine preparata, pubblica la lunghezza NDEF nel blocco 1
   per ultimo e confronta nuovamente tutti i 320 byte.

Tutti i comandi NFC di lettura/scrittura sono indirizzati all'UID previsto.
Non ci sono retry automatici delle scritture né rollback. L'ordine di pubblicazione
riduce il rischio di esporre dati incompleti, ma **non rende atomica la EEPROM**:
un'interruzione può lasciare un tag vuoto, parziale o già scritto senza conferma.
Solo `verified`/`unchanged` con `verified: true` confermano l'esito. In caso di
risposta persa o `may_have_changed: true`, ispezionare e confrontare prima di decidere
un nuovo tentativo esplicito. Il comando `write-info` restituisce il dump per farlo;
il client lo salva da solo prima di ogni sostituzione (vedi "Copia di sicurezza prima di sostituire").

## Protocollo seriale, 115200 baud

Un solo proprietario della porta; non aprire contemporaneamente monitor, CLI e
demone. Nessun endpoint HTTP di scrittura viene esposto in questa versione.
I comandi terminano con newline. Le risposte iniziano con `PHOENIX-WRITE `,
seguito da JSON; log e righe `PHOENIX-TAG` non sono risposte del writer.

- `write-info`: entra in modalità scrittura, sospende gli invii e legge lo snapshot.
  Risponde `status: ready`, `ticket` (16 cifre hex), `expires_ms: 60000`, UID canonico
  MSB-first senza separatori, `capacity: 320`, `block_size: 4`, `blank`, `memory_hex`.
  In assenza di tag risponde con errore e non autorizza alcuna scrittura.
- `write-commit <ticket> <new|replace> <image_hex>`: ticket valido monouso, immagine
  da 10 a 320 byte. `new` rifiuta tag occupati; `replace` è il consenso esplicito a
  sostituirne tutto il contenuto. Il ticket viene consumato anche se il commit fallisce.
- `write-cancel`: elimina il ticket. Non interrompe una scrittura già in corso:
  il firmware esegue il commit in modo sincrono prima di elaborare altri comandi.

Il ticket scade dopo 60 secondi, non sopravvive al riavvio e non è una credenziale
di autenticazione. L'accesso alla seriale deve essere riservato al bridge locale.
Le righe oltre 768 caratteri vengono scartate integralmente.

Risposta di commit: `status`, `verified`, `may_have_changed`, `block` (-1 se non
applicabile), `writes` (tentativi di scrittura). Gli errori di protocollo possono
contenere soltanto `status` e `verified: false`.
Stati: `verified`, `unchanged`, `no_tag_or_io_error`, `unsupported_tag`, `read_failed`,
`tag_changed`, `invalid_image`, `overwrite_required`, `security_status_unavailable`,
`locked`, `write_failed`, `verify_failed`; inoltre errori di ticket/comando/formato.

Dopo successo, errore o annullamento, le letture normali restano sospese finché
il campo non risulta vuoto per tre rilevamenti consecutivi (circa 4,5 s).
Con ticket ancora valido la sessione rimane aperta fino a commit, annullamento
o scadenza. Togliere il tag per riprendere il normale funzionamento.
Questo permette al futuro bridge di associare l'UID alla bobina prima di una
nuova lettura. I dati della bobina attiva in Moonraker non vengono cambiati dal writer.
Non avviare OTA durante una scrittura; il protocollo seriale rimane disponibile
anche se il WiFi è assente.

## Prova manuale su Phoenix

Prima installare il firmware di questo branch (USB oppure OTA con la procedura
esistente). Copiare `host/phoenix_opentag_write.py` accanto al demone Python;
usa solo la libreria standard, compatibile con Python 3.9 della CB1.

Fermare **solo il demone OpenTag** per liberare la seriale:

```bash
sudo systemctl stop phoenix-opentag
```

Appoggiare un solo tag di prova e ispezionarlo:

```bash
cd ~/phoenix-opentag/host
python3 phoenix_opentag_write.py \
  --serial /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

L'ispezione non scrive memoria. Annotare l'UID. Per scrivere un'immagine preparata
per quella bobina (sostituire il percorso e l'UID di esempio):

```bash
python3 phoenix_opentag_write.py \
  --serial /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 \
  --image /percorso/bobina.bin --expect-uid E004010866073472
```

Aggiungere `--replace` solo se si vuole cancellare/sostituire il contenuto esistente.
Il client verifica UID e conferma firmware, non ritenta un commit fallito.

### Copia di sicurezza prima di sostituire

Il firmware non ha rollback: se una scrittura si interrompe, il contenuto precedente
del tag è perso. Per questo, quando il tag **non è vuoto**, il client salva la sua
memoria (320 byte esatti, dal `memory_hex` di `write-info`) in un file **prima**
di inviare il commit:

- cartella predefinita `~/phoenix-tag-backups` (`--backup-dir` per cambiarla, creata con permessi `0700`);
- nome `<UID>-<AAAAMMGG>-<HHMMSS>.bin`, permessi `0600`, scritto con `fsync` e riletto per verifica;
- non sovrascrive mai un backup esistente (aggiunge un suffisso `-2`, `-3`…);
- **se la copia non riesce** (disco pieno, cartella non scrivibile, dump non fornito dal
  firmware) il client annulla la sessione con `write-cancel` **senza inviare il commit**:
  il tag resta intatto;
- il percorso compare nel risultato (`backup`) e nel messaggio d'errore se la scrittura
  non è confermata. Un tag vuoto non ne ha bisogno e non produce alcun file.

Per ripristinare il contenuto precedente, con l'UID del tag e dopo aver tolto e rimesso il tag:

```bash
python3 phoenix_opentag_write.py \
  --serial /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 \
  --image ~/phoenix-tag-backups/E004010866073472-20260930-201500.bin \
  --expect-uid E004010866073472 --replace
```

Il ripristino passa dalle stesse regole di qualsiasi scrittura: il backup deve essere
un'immagine OpenPrintTag valida per il firmware (CC, TLV NDEF, terminatore, resto a zero).
Se il tag conteneva altro, o dati già corrotti, il file resta comunque la copia fedele dei
byte da cui partire. Dopo un ripristino non riuscito o un tag lasciato con NDEF vuoto
(`03 00 FE 00` nel blocco 1) serve `--replace`, perché il tag non è più vuoto.
Non usare i fixture ufficiali su bobine reali: contengono identità/dati di esempio.
Non copiare lo stesso `instance_uuid` su più bobine: l'encoder deve gestire l'identità
secondo la specifica. Usare esclusivamente la porta dell'ESP32, mai una MCU Klipper.

Dopo la prova, anche in caso di errore, togliere il tag e riavviare il bridge:

```bash
sudo systemctl start phoenix-opentag
```

## Collegamento futuro al fork Spoolman

Il bridge dovrà gestire lettura e scrittura sulla stessa connessione, serializzando
le operazioni. Il flusso del pulsante sarà: salva bobina → genera/valida immagine →
entra in sessione → mostra UID e anteprima → conferma eventuale sostituzione →
commit → rilettura verificata → associa UID alla bobina salvata → chiedi rimozione.
Bloccare la gestione ordinaria degli eventi del lettore prima della scansione di
scrittura, così un evento precedente non crea/attiva una bobina durante il flusso.
Non associare l'UID dopo un timeout o una verifica fallita. La futura API web dovrà
applicare autenticazione e autorizzazioni: il ticket seriale non le sostituisce.

Test automatici: `bash tests/run_host.sh` e
`python3 -m unittest discover -s tests -p 'test_*.py'`. Le simulazioni includono
interruzioni ad ogni scrittura, verifiche fallite, protezioni, cambio tag, ticket
scaduti/riutilizzati e sospensione degli invii. La prova fisica resta necessaria.

## Bridge per il pulsante Spoolman «Scrivi tag»

Il branch `codex/spoolman-write-bridge` aggiunge al demone l'API autenticata
`/writer/{arm,prepare,commit,finish,cancel,reset}`. È disattivata finché non è
configurata la variabile `PHOENIX_WRITER_TOKEN` (almeno 32 caratteri casuali).
Richiede `--serial`: il thread seriale esistente è l'unico proprietario della
porta, anche quando le letture normali arrivano via WiFi. Non avviare in parallelo
il monitor seriale o il writer manuale.

Distribuire insieme `host/phoenix_opentag_daemon.py`,
`host/phoenix_opentag_write.py` e `host/phoenix_writer_bridge.py`. Il firmware
0.6.0-dev già provato sul banco non richiede modifiche per questo bridge.
Il servizio systemd mantiene i suoi argomenti; impostare il token tramite
ambiente del servizio. Non salvarlo nel repository. `PHOENIX_TAG_BACKUP_DIR`
consente di cambiare la directory dei backup (default `~/phoenix-tag-backups`).

Il fork `Ricky1966/Spoolman`, branch `codex/openprinttag-write-ui`, richiede
`SPOOLMAN_WRITER_TOKEN` con lo stesso valore e
`SPOOLMAN_WRITER_URL=http://192.168.1.36:7913` (indirizzo raggiungibile dal
container). Configurare un solo processo Spoolman e un solo lettore USB.
La documentazione completa è `docs/phoenix-writer.md` nel fork.

L'apertura del form riserva il lettore **prima** che l'utente appoggi il tag:
il demone sospende l'elaborazione automatica delle letture USB e HTTP, evitando
creazioni o cambi bobina durante la scrittura. La preparazione acquisisce UID,
ticket firmware e memoria originale. Il commit consuma la sessione, salva il
backup prima di una sostituzione e non ritenta dopo errori o risposte perse.
Spoolman associa l'UID solo dopo verifica positiva del firmware. Rimuovere il
tag e chiudere il dialogo per rilasciare il lettore.

Dopo una pagina chiusa o un riavvio, rimuovere il tag e utilizzare il recupero
esplicito della sessione nel form. Un recupero non ripete una scrittura né
ripristina la memoria. L'API rifiuta richieste browser dirette e richiede Bearer
auth; mantenere i servizi su una rete fidata o usare TLS tra macchine.

Validazione del bridge: test unitari e prova locale con HTTP reale, database
SQLite Spoolman, browser e seriale emulata. Il flusso web completo deve ancora
essere provato su Phoenix. La primitiva firmware è stata provata dall'utente
il 1 ottobre 2026: SLIX2 `E004010866073075`, recupero dopo interruzione con
`--replace`, verifica positiva e rilettura corretta del campione Prusament.
