# ODIN — Open Dryer with Intelligent NFC

Stazione intelligente per l'essiccazione e la gestione dei filamenti per stampa 3D, basata su **ESP32** con PCB custom, **Raspberry Pi 5** come backend cloud locale e **app mobile Flutter** cross-platform.

ODIN integra lettura/scrittura tag NFC OpenPrintTag, rilevamento colore OPT4048, controllo PI multi-zona della temperatura, gestione umidità con valvola solenoide e dashboard web embedded.

---

## Repository correlati

| Repository | Descrizione |
|---|---|
| **[Ricky1966/Odin-ESP32](https://github.com/Ricky1966/Odin-ESP32)** ← questo | Firmware ESP32, PCB KiCad, documentazione |
| **[Ricky1966/Odin-App](https://github.com/Ricky1966/Odin-App)** | App mobile Flutter (Android/iOS) |
| **[Ricky1966/Odin-Cloud](https://github.com/Ricky1966/Odin-Cloud)** | Backend FastAPI su Raspberry Pi 5 |
| **[Ricky1966/Odin-Releases](https://github.com/Ricky1966/Odin-Releases)** | Release artifacts (firmware.bin, app.apk) |

---

## Funzionalità

- Essiccazione filamenti con controllo PI su 4 zone indipendenti a 24V
- Lettura e scrittura tag NFC/RFID OpenPrintTag (standard Prusa Research) tramite PN5180
- Rilevamento colore filamento con sensore OPT4048 + 2 LED bianchi
- Monitoraggio temperatura e umidità della camera con BME280
- Valvola solenoide 24V per espulsione aria umida (ciclo heat → vent → repeat)
- Fan 24V per circolazione aria
- Motore stepper NEMA17 per rotazione bobine durante la lettura
- Dashboard web embedded su ESP32 (accesso via WiFi AP o rete locale)
- Aggiornamento firmware OTA via browser (`/update`)
- Backend FastAPI su Raspberry Pi 5 con accesso remoto via Tailscale
- App mobile Flutter con controllo completo del ciclo di essiccazione

---

## Hardware

- ESP32 AZ-Delivery 38-pin su PCB custom (KiCad)
- Lettore NFC PN5180 (FPC 10-pin)
- Sensore colore OPT4048 + runout filamento (FPC 8-pin)
- Sensore T°/umidità BME280
- 4× riscaldatori kapton 24V + 4× NTC 100kΩ β=3950
- Alimentatore 24V 15A + buck converter 24V→5V
- Motore stepper NEMA17 con driver esterno (A4988/DRV8825/TMC2209)
- Ventola 24V + valvola solenoide NC 24V
- Raspberry Pi 5 come backend cloud locale
- Box plexiglass 440×340×330mm (~49L)

---

## Struttura repository

```
src/                  Firmware ESP32 (PlatformIO / Arduino)
  config.h            Costanti, pin, struct
  sensor.h/.cpp       BME280 + NTC ADC
  heater.h/.cpp       Controllo PI riscaldatore
  nfc.h/.cpp          Driver PN5180 + OpenPrintTag CBOR
  web_ui.h/.cpp       Dashboard web + API HTTP
  i18n.h/.cpp         Sistema multilingua (IT/EN)
  materials.h/.cpp    Profili materiali
  utils.h/.cpp        Utility generali
  main.cpp            setup() + loop()

hardware/             PCB KiCad + Gerber per PCBWay
  gerber/Archivio.zip Gerber + drill + BOM + CPL pronti per produzione

backend/              (in sviluppo) FastAPI su Raspberry Pi 5
docs/hardware/        Documentazione elettronica e GPIO
```

---

## Accesso alla dashboard

- **WiFi AP**: SSID `ODIN`, password `odindryer` → `http://192.168.4.1`
- **Rete locale**: `http://<IP_ESP32>`
- **OTA**: `http://<IP_ESP32>/update`

---

## Riferimenti

- [OpenPrintTag](https://openprinttag.org/) — standard NFC open-source di Prusa Research
- [App mobile ODIN](https://github.com/Ricky1966/odin-mobile) — Flutter app (Android/iOS)
- [docs/hardware/design.md](docs/hardware/design.md) — architettura elettronica
- [docs/hardware/esp32.md](docs/hardware/esp32.md) — GPIO map e pinout

---

## Licenza

Distribuito con licenza GNU Affero General Public License v3.0.

SPDX-License-Identifier: AGPL-3.0-or-later
