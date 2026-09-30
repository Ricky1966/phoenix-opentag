# Phoenix OpenTag

Lettore NFC minimale per tag **OpenPrintTag** (standard Prusa Research), basato su **ESP32 + PN5180**, pensato per essere collegato alla Sovol SV08 "Phoenix".

Nato come versione ridotta di [Odin-ESP32](https://github.com/Ricky1966/Odin-ESP32) (stazione di essiccazione filamenti): inizializzazione hardware proveniente dal branch `develop` di Odin, con parser separato e controlli sulle letture NFC, ma senza dashboard web, riscaldatore, sensori ambiente o gestione multi-bobina — lettura validata e integrazione con Spoolman/Moonraker.

---

## Cosa fa

- Si connette al WiFi di casa (stazione, nessun Access Point), usato per l'invio in modalità `wifi`/`auto` e per l'aggiornamento OTA.
- Legge in loop i tag ISO15693/NFC-V vicini al PN5180 (il protocollo usato da OpenPrintTag).
- Quando riconosce un OpenPrintTag, stampa su seriale: brand, materiale, colore, diametro, densità, range temperature ugello/piatto, peso, paese.
- Stampa i dati al cambio del tag o al recupero di una lettura valida. Invia solo dati validati.
- Invia ogni nuova lettura al demone su Phoenix, via **USB** o **WiFi** a scelta (vedi sotto). Il demone la passa a Spoolman e imposta la bobina attiva in Moonraker.

## Hardware

- ESP32 DevKit 38 pin (AZ-Delivery ESP32-WROOM-32)
- Modulo PN5180 (breakout NXP/AZ-Delivery)

Cablaggio punto-punto:

| Segnale PN5180 | GPIO ESP32 |
|---|---|
| RST | 17 |
| NSS (CS) | 5 |
| MOSI | 23 |
| MISO | 19 |
| SCK / CLK | 18 |
| BUSY | 16 |
| 5V | 5V |
| 3V3 | 3V3 |
| GND | GND |

## Setup

```bash
cp src/secrets.h.example src/secrets.h
# modifica src/secrets.h con SSID/password WiFi reali
pio run --target upload
pio device monitor
```

`src/secrets.h` non è versionato (vedi `.gitignore`).

## Aggiornamento via WiFi (OTA)

Dalla 0.4.0 il firmware si aggiorna anche via WiFi, senza staccare l'ESP32 da Phoenix. Il WiFi resta sempre acceso, anche in modalità `usb`, perché serve all'OTA.

1. In `src/secrets.h` aggiungi la password OTA (vedi `secrets.h.example`); senza questa riga l'OTA resta disattivato:
   ```c
   #define OTA_PASSWORD "scegli-una-password"
   ```
2. **Una sola volta, via USB**, flasha il primo firmware con OTA: `pio run -t upload --upload-port <porta>`. All'avvio deve comparire `OTA attivo: phoenix-opentag.local (<IP>)`.
3. Da quel momento, dal PC:
   ```bash
   PHOENIX_OTA_PASSWORD='scegli-una-password' pio run -e esp32dev_ota -t upload
   ```
   Se `phoenix-opentag.local` non si risolve, aggiungi `--upload-port <IP dell'ESP32>` (lo stampa `show`).

Usa per `OTA_PASSWORD` solo lettere e numeri: PlatformIO la inserisce nel comando senza virgolette, quindi `&`, `;`, `$` o spazi lo rompono. `espota.py --debug` stampa la password in chiaro: non usarlo. La password è compilata nel firmware, quindi cambiarla richiede un nuovo flash via USB.

Durante l'aggiornamento la lettura NFC è sospesa; al termine l'ESP32 si riavvia e il demone su Phoenix riprende a leggere la seriale da solo.

## Invio a Phoenix: USB o WiFi

La modalità si sceglie da seriale (`pio device monitor`, 115200) e resta salvata in flash, senza ricompilare:

| Comando | Effetto |
|---|---|
| `show` | impostazioni correnti |
| `mode usb` | ogni lettura esce sulla seriale come riga `PHOENIX-TAG {json}`, letta dal demone (predefinita) |
| `mode wifi` | HTTP `POST <host>/tag` con lo stesso JSON |
| `mode auto` | WiFi se connesso e il POST riesce, altrimenti USB |
| `host http://192.168.1.36:7913` | indirizzo del demone su Phoenix |
| `reader <id>` | `reader_id` mostrato in Spoolman (predefinito `phoenix-opentag`) |
| `test` | rinvia l'ultimo tag letto |
| `reboot` | riavvio, necessario dopo `mode` |

I valori iniziali sono in `src/config.h` (`TRANSPORT_DEFAULT_*`). JSON v1 inviato dalla versione 0.5 (i campi assenti dal tag vengono omessi; il demone accetta anche i nomi della 0.4):

```json
{"schema_version":1,"event_id":"12ab34cd-00000001","uid":"E004010812345678","reader_id":"phoenix-opentag","firmware":"0.5.0-dev","brand_name":"Prusament","material_name":"PLA Galaxy Black","color_rgba":"#3D3E3DFF","filament_diameter":1.75,"density":1.24,"nominal_netto_full_weight":1000}
```

Il firmware conserva solo l'ultima lettura valida: ritenta dopo 5, 10, 20, 40 e 60 secondi (massimo), sospende gli invii su errore NFC e annulla l'evento alla rimozione confermata. In USB il demone risponde `ack <event_id>` solo dopo la gestione riuscita; in WiFi un errore di Moonraker produce HTTP 502. I due trasporti condividono lo stesso evento in modalità auto. I POST sono sincroni e possono ritardare temporaneamente polling NFC e OTA.

L'attivazione rifiutata durante `printing`/`paused` è un esito definitivo: non viene accodata per la fine stampa. Dopo la stampa occorre una nuova lettura o `test`. I pesi delle bobine già registrate non vengono aggiornati dal tag.

**Aggiornamento dalla 0.4:** installare prima il nuovo demone (compatibile con entrambi i formati), poi il firmware. Riavviare soltanto `phoenix-opentag` dopo aver aggiornato i file. Verificare con una bobina nota, a stampante ferma. Conservare `src/secrets.h`, impostazioni NVS e parametri `--serial` del servizio esistente.


## Demone su Phoenix

`host/phoenix_opentag_daemon.py` gira sulla CB1 con il `python3` di sistema (nessuna dipendenza). Per ogni lettura:

1. chiama `POST /api/v1/tag/scan` su Spoolman (≥ 0.27.0), che risolve l'UID nella bobina associata;
2. se l'UID è sconosciuto e il tag riporta almeno `material` e `density`, crea la bobina, riusando fornitore (`brand`) e filamento (nome, fornitore, colore, diametro) se esistono già, e le associa il tag;
3. imposta la bobina attiva con `POST /server/spoolman/spool_id` su Moonraker, **ma non se la stampante è in `printing` o `paused`**.

Solo eventi consecutivi identici gestiti con successo entro 5 s vengono deduplicati; A → B → A riattiva A. Errori temporanei non vengono memorizzati come successi. Opzioni utili: `--no-create`, `--no-activate`, `-v`.

```bash
# prova manuale su Phoenix
python3 ~/phoenix-opentag/host/phoenix_opentag_daemon.py --listen 0.0.0.0:7913 \
    --serial /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0   # solo per usb/auto
curl -s http://localhost:7913/health

# servizio permanente
sudo cp ~/phoenix-opentag/host/phoenix-opentag.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now phoenix-opentag
journalctl -u phoenix-opentag -f
```

Per passare il servizio alla modalità USB, aggiungi `--serial` alla riga di `--listen` e riavvialo:

```bash
sudo sed -i 's|--listen 0.0.0.0:7913 \\|--listen 0.0.0.0:7913 --serial /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 \\|' /etc/systemd/system/phoenix-opentag.service
sudo systemctl daemon-reload && sudo systemctl restart phoenix-opentag
```

Per dare comandi all'ESP32 quando è collegato a Phoenix, ferma il demone (`sudo systemctl stop phoenix-opentag`: la porta non va letta in due) e usa il monitor seriale incluso in Klipper. Si esce con `Ctrl+]`, che con la tastiera italiana è `Ctrl+5`:

```bash
~/klippy-env/bin/python -m serial.tools.miniterm -e /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 115200
```

Con `--serial` usa sempre il percorso `/dev/serial/by-id/...` dell'ESP32. Il demone rifiuta di aprire una porta che compare come `serial:` nei `.cfg` di Klipper (`--klipper-config`, predefinito `~/printer_data/config`).

Il demone e Spoolman non hanno autenticazione: vanno esposti solo sulla rete locale.

## Validazione

Test sul PC: parser, consegna, driver ISO15693, 40.000 input alterati, sei tag ufficiali OpenPrintTag e regressioni del demone (vedi "Test sul PC").

**30 settembre 2026, versione consolidata `0.5.0-dev`: PASS su hardware reale.** Flash via USB, poi **aggiornamento OTA via WiFi** riuscito con la password definitiva. All'avvio: WiFi connesso, PN5180 3.5 OK, `OTA attivo`. Il tag ODIN di prova viene letto e inviato come riga `PHOENIX-TAG` (JSON v1). Il demone di questo ramo, installato su Phoenix al posto della versione precedente, in modalità USB ritrova la bobina 2 (`created: false`) e la attiva in Moonraker. In locale, con Spoolman 0.27.0, il demone crea fornitore, filamento, bobina e associazione dalla riga del firmware, risponde `ack <event_id>`, ignora l'`event_id` ripetuto e non attiva la bobina con la stampante in `printing`.

Da verificare: che l'ESP32 riceva davvero l'ACK dal demone, le modalità `wifi` e `auto` con il firmware consolidato, e la lettura di tag non scritti da Odin.

Prove precedenti, sulla versione 0.4 (demone della 0.4):

**29 settembre 2026, modalità WiFi: PASS su hardware reale.** ESP32 DevKit (CH340) + PN5180 3.5, tag OpenPrintTag di prova (UID `E0:04:01:08:66:07:15:E3`, ODIN Test PLA). Phoenix: Spoolman 0.27.0 in Docker, demone avviato a mano con `--listen 0.0.0.0:7913`.

- Prima lettura: il demone ha creato fornitore ODIN, filamento "Test PLA" e la bobina 2 con il tag associato, poi l'ha resa attiva in Moonraker (`GET /server/spoolman/spool_id` → `2`). Tempo dalla lettura all'attivazione: circa 1 s.
- Seconda lettura, dopo aver tolto il tag per più di 5 s: bobina 2 ritrovata (`created: false`) e riattivata, nessun duplicato.

- Demone installato come servizio (`host/phoenix-opentag.service`, `enable --now`): la stessa lettura riattiva la bobina 2; un secondo tag (UID `E0:04:01:08:66:07:19:17`, BravaSoft Test PLA) crea fornitore BravaSoft, un filamento separato e la bobina 3, che diventa quella attiva.

- **Modalità USB: PASS.** ESP32 collegato a una porta USB di Phoenix (`/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`, accanto alla MCU Klipper `usb-Klipper_stm32f103xe_...`), servizio avviato con `--serial` su quel percorso. La lettura arriva come `[usb]` e riattiva la bobina 2.


## Test sul PC

`bash tests/run_host.sh` (serve solo `g++`) compila con AddressSanitizer e prova: il parser e la consegna, il driver ISO15693 e i **sei tag di esempio ufficiali** della specifica OpenPrintTag (`tests/fixtures/official/`, licenza MIT di Prusa Research in `tests/fixtures/LICENSE.openprinttag`; valori attesi generati con la loro implementazione di riferimento in Python), più troncamenti e byte alterati a caso senza accessi fuori dai limiti. Il sesto tag ha la temperatura massima dell'ugello sotto la minima ed è un caso volutamente non valido: il parser lo rifiuta. Se un tag non indica il diametro, il parser assume 1,75 mm come prevede la specifica. Il demone si prova con `python3 -m unittest discover -s tests -p 'test_daemon.py'`. La CI di GitHub fa gli stessi controlli e compila il firmware.

Il parser della versione 0.4 non leggeva i tag ufficiali (non gestiva la sezione meta, le mappe CBOR a lunghezza indefinita e i record NDEF lunghi): funzionava solo con i tag scritti da Odin.

## Prossimi passi

- Mandare a Spoolman la memoria grezza del tag (`payload_b64` di `POST /tag/scan`, che Spoolman oggi ritrasmette senza decodificare) e spostare lì la decodifica e la scrittura dei tag, con un fork di Spoolman. La specifica e l'implementazione di riferimento sono nel repository `prusa3d/OpenPrintTag` (licenza MIT).
- Provare tag OpenPrintTag di produttori diversi.

## Struttura

```
src/
  config.h        Pin PN5180, timing, include di secrets.h
  secrets.h       Credenziali WiFi (non versionato, vedi secrets.h.example)
  nfc.h/.cpp      Lettura PN5180 con gestione errori
  tag_parser.*    Parser NDEF/CBOR validato, testabile senza hardware
  delivery.*      JSON v1 e stato dei tentativi di consegna
  transport.h/.cpp  Invio letture a Phoenix (USB/WiFi), comandi seriali, impostazioni in flash
  main.cpp        setup() + loop(), WiFi e OTA
tests/            Test sul PC (`bash tests/run_host.sh`): parser, consegna, ISO15693, tag ufficiali in fixtures/official; test del demone
docs/http-contract.md  Contratto lettore -> demone (JSON v1, ACK, ritentativi)
.github/workflows/check.yml  CI: test, demone su Python 3.9, compilazione con e senza OTA
host/
  phoenix_opentag_daemon.py  Demone per Phoenix: seriale/HTTP -> Spoolman -> Moonraker
  phoenix-opentag.service    Unit systemd
lib/PN5180-Library/  Libreria PN5180 vendorizzata (da Odin-ESP32)
```

## Riferimenti

- [OpenPrintTag](https://openprinttag.org/) — standard NFC open-source di Prusa Research
- [Odin-ESP32](https://github.com/Ricky1966/Odin-ESP32) — progetto sorgente (essiccatore filamenti)

## Licenza

Distribuito con licenza GNU Affero General Public License v3.0.

SPDX-License-Identifier: AGPL-3.0-or-later
