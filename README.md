# Phoenix OpenTag

Lettore NFC minimale per tag **OpenPrintTag** (standard Prusa Research), basato su **ESP32 + PN5180**, pensato per essere collegato alla Sovol SV08 "Phoenix".

Nato come versione ridotta di [Odin-ESP32](https://github.com/Ricky1966/Odin-ESP32) (stazione di essiccazione filamenti): stesso driver PN5180/ISO15693 e stesso parser CBOR OpenPrintTag del branch `develop` di Odin (l'unico dei due branch effettivamente validato su questo hardware), ma senza dashboard web, riscaldatore, sensori ambiente o gestione multi-bobina — lettura del tag e stampa su seriale, con trasporto HTTP opzionale.

---

## Cosa fa

- Si connette al WiFi di casa (stazione, nessun Access Point) e stampa l'IP assegnato — la lettura tag funziona anche senza rete; il trasporto opzionale è descritto sotto.
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
  nfc.h/.cpp      Polling PN5180 e stato della lettura
  tag_parser.*    Parser NDEF/CBOR OpenPrintTag testabile su host
  delivery.*      JSON e stato ritentativi
  transport.*     Client HTTP opzionale
  main.cpp        setup() + loop()
lib/PN5180-Library/  Libreria PN5180 vendorizzata (da Odin-ESP32)
```

## Riferimenti

- [OpenPrintTag](https://openprinttag.org/) — standard NFC open-source di Prusa Research
- [Odin-ESP32](https://github.com/Ricky1966/Odin-ESP32) — progetto sorgente (essiccatore filamenti)

## Licenza

Distribuito con licenza GNU Affero General Public License v3.0.

SPDX-License-Identifier: AGPL-3.0-or-later

## Consolidamento 0.3.0-dev

Questa revisione conserva una sola istanza PN5180, gli stessi GPIO, clock SPI e
sequenza di inizializzazione. Il parser ora legge il MIME OpenPrintTag e separa
meta/main region, supporta NDEF corto e lungo, altri TLV/record prima del materiale,
mappe indefinite e campi sconosciuti con profondità limitata. I dati vengono
pubblicati solo dopo una lettura completa e valida dei campi utilizzati.

Limiti espliciti: massimo 2048 byte letti dal tag, indirizzamento ISO15693 standard
fino a 256 blocchi da 1–32 byte, regioni da massimo 512 byte. Record NDEF spezzati,
stringhe note in formato CBOR indefinito, strutture annidate oltre 8 livelli,
classi diverse da FFF e dati non validi vengono rifiutati. La regione ausiliaria
non viene importata né riscritta. Il parser non è un validatore completo di ogni
campo dello standard: controlla i campi consumati e salta quelli sconosciuti.

`spool.present` indica la presenza, `spool.valid` una lettura utilizzabile.
Tre assenze consecutive confermano la rimozione; un errore temporaneo sospende
la pubblicazione senza cancellare i metadati dell'ultima lettura buona.

Il trasporto HTTP opzionale è disattivato di default. Configurazione, schema JSON,
ritentativi e responsabilità del futuro ricevitore sono in
[docs/http-contract.md](docs/http-contract.md). Nessun servizio viene installato
su Phoenix e nessuna bobina viene attivata in Moonraker da questa sola revisione.

### Verifiche riproducibili

```bash
bash tests/run_host.sh
cp src/secrets.h.example src/secrets.h  # solo per una compilazione di prova
pio run -e esp32dev
```

Il primo comando compila parser e stato di consegna su Linux con AddressSanitizer
e UndefinedBehaviorSanitizer; verifica anche un tag di esempio ufficiale Prusa,
troncamenti, MIME errato, metadati con offset, NDEF lungo, numeri CBOR, UTF-8,
ritentativi e 40.000 ingressi alterati/casuali. Verifica anche il protocollo
ISO15693 con risposte simulate (blocchi da 32 byte, 256 blocchi, risposte corte
ed errori I/O). La piattaforma ESP32 è fissata a `espressif32@7.1.3`. Non sovrascrivere `secrets.h` se
contiene già le tue credenziali: la copia è solo per un checkout di prova.

Prima dell'uso reale serve un collaudo ESP32: stesso tag già letto, rimozione e
reinserimento, avvio senza Wi-Fi e riconnessione; successivamente prova con bridge
che restituisca successo/errore. Il caricamento non è parte dei test automatici.
