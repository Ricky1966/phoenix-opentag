# Phoenix OpenTag

Lettore NFC minimale per tag **OpenPrintTag** (standard Prusa Research), basato su **ESP32 + PN5180**, pensato per essere collegato alla Sovol SV08 "Phoenix".

Nato come versione ridotta di [Odin-ESP32](https://github.com/Ricky1966/Odin-ESP32) (stazione di essiccazione filamenti): stesso driver PN5180/ISO15693 e stesso parser CBOR OpenPrintTag del branch `develop` di Odin (l'unico dei due branch effettivamente validato su questo hardware), ma senza dashboard web, riscaldatore, sensori ambiente o gestione multi-bobina — solo lettura del tag e stampa su seriale.

---

## Cosa fa

- Si connette al WiFi di casa (stazione, nessun Access Point) e stampa l'IP assegnato — non serve ancora per la lettura tag, è un punto di aggancio per sviluppi futuri (es. integrazione Moonraker/Spoolman).
- Legge in loop i tag ISO15693/NFC-V vicini al PN5180 (il protocollo usato da OpenPrintTag).
- Quando riconosce un OpenPrintTag, stampa su seriale: brand, materiale, colore, diametro, densità, range temperature ugello/piatto, peso, paese.
- Stampa solo quando il tag cambia (niente spam ripetuto sullo stesso tag).

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

## Struttura

```
src/
  config.h        Pin PN5180, timing, include di secrets.h
  secrets.h       Credenziali WiFi (non versionato, vedi secrets.h.example)
  nfc.h/.cpp      Driver PN5180 (ISO15693) + parser NDEF/CBOR OpenPrintTag (solo lettura)
  main.cpp        setup() + loop()
lib/PN5180-Library/  Libreria PN5180 vendorizzata (da Odin-ESP32)
```

## Riferimenti

- [OpenPrintTag](https://openprinttag.org/) — standard NFC open-source di Prusa Research
- [Odin-ESP32](https://github.com/Ricky1966/Odin-ESP32) — progetto sorgente (essiccatore filamenti)

## Licenza

Distribuito con licenza GNU Affero General Public License v3.0.

SPDX-License-Identifier: AGPL-3.0-or-later
