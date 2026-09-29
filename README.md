# Phoenix OpenTag

Lettore NFC minimale per tag **OpenPrintTag** (standard Prusa Research), basato su **ESP32 + PN5180**, pensato per essere collegato alla Sovol SV08 "Phoenix".

Nato come versione ridotta di [Odin-ESP32](https://github.com/Ricky1966/Odin-ESP32) (stazione di essiccazione filamenti): stesso driver PN5180/ISO15693 e stesso parser CBOR OpenPrintTag del branch `develop` di Odin (l'unico dei due branch effettivamente validato su questo hardware), ma senza dashboard web, riscaldatore, sensori ambiente o gestione multi-bobina — solo lettura del tag e stampa su seriale.

---

## Cosa fa

- Si connette al WiFi di casa (stazione, nessun Access Point) quando la modalità di invio lo richiede.
- Legge in loop i tag ISO15693/NFC-V vicini al PN5180 (il protocollo usato da OpenPrintTag).
- Quando riconosce un OpenPrintTag, stampa su seriale: brand, materiale, colore, diametro, densità, range temperature ugello/piatto, peso, paese.
- Stampa solo quando il tag cambia (niente spam ripetuto sullo stesso tag).
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

I valori iniziali sono in `src/config.h` (`TRANSPORT_DEFAULT_*`). JSON inviato (i campi assenti dal tag vengono omessi):

```json
{"uid":"E0:04:01:08:12:34:56:78","reader_id":"phoenix-opentag","fw":"0.3.0","brand":"Prusament",
 "material":"PLA Galaxy Black","color":"3D3E3D","diameter":1.75,"density":1.240,
 "nozzle_min":205,"nozzle_max":225,"bed_min":50,"bed_max":60,"weight":1000,"country":"CZ"}
```

## Demone su Phoenix

`host/phoenix_opentag_daemon.py` gira sulla CB1 con il `python3` di sistema (nessuna dipendenza). Per ogni lettura:

1. chiama `POST /api/v1/tag/scan` su Spoolman (≥ 0.27.0), che risolve l'UID nella bobina associata;
2. se l'UID è sconosciuto e il tag riporta almeno `material` e `density`, crea la bobina, riusando fornitore (`brand`) e filamento (nome, fornitore, colore, diametro) se esistono già, e le associa il tag;
3. imposta la bobina attiva con `POST /server/spoolman/spool_id` su Moonraker, **ma non se la stampante è in `printing` o `paused`**.

Letture ripetute dello stesso tag entro 5 s vengono ignorate. Opzioni utili: `--no-create`, `--no-activate`, `-v`.

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

Con `--serial` usa sempre il percorso `/dev/serial/by-id/...` dell'ESP32. Il demone rifiuta di aprire una porta che compare come `serial:` nei `.cfg` di Klipper (`--klipper-config`, predefinito `~/printer_data/config`).

Il demone e Spoolman non hanno autenticazione: vanno esposti solo sulla rete locale.

## Struttura

```
src/
  config.h        Pin PN5180, timing, include di secrets.h
  secrets.h       Credenziali WiFi (non versionato, vedi secrets.h.example)
  nfc.h/.cpp      Driver PN5180 (ISO15693) + parser NDEF/CBOR OpenPrintTag (solo lettura)
  transport.h/.cpp  Invio letture a Phoenix (USB/WiFi), comandi seriali, impostazioni in flash
  main.cpp        setup() + loop()
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
