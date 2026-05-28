# Essiccatore

Sistema per la gestione e l'essiccazione dei filamenti, basato su ESP32 e sullo standard NFC OpenPrintTag di Prusa per il riconoscimento delle bobine.

Il sistema usera un lettore PN5180 per leggere i tag NFC/RFID delle bobine, un sensore BME280 per misurare temperatura e umidita della camera e un controllo PID per gestire da 1 a 4 riscaldatori a 24 V, infine un sistema OPT4048 per leggere il colore del filamento.

L'idea iniziale è di realizzare uno spazio comune per 4 bobine, in base al contenitore disponibile, con un motore stepper per far ruotare le bobine durante la lettura o la gestione del filamento.

## Obiettivi

- Gestire un vano comune per 4 bobine di filamento.
- Riconoscere i filamenti tramite tag NFC/RFID compatibili OpenPrintTag.
- Leggere i tag tramite modulo PN5180 collegato a ESP32.
- Leggerne il colore attraverso un sensore OPT4048.
- Monitorare temperatura e umidità interna con BME280.
- Controllare il riscaldamento con regolazione PID su quattro zone indipendenti.
- Utilizzare elementi riscaldanti a 24V adatti alla geometria del contenitore scelto.
- Far ruotare le bobine con uno stepper, per facilitare lettura e gestione.

## Hardware previsto

- ESP32.
- Lettore NFC PN5180.
- Sensore temperatura/umidita BME280.
- Quattro elementi riscaldanti a 24V.
- Alimentatore 24 V 15 A.
- Convertitore buck 24 V / 5 V per alimentare ESP32 ed elettronica a bassa tensione.
- Motore stepper per rotazione bobine.
- Sensore OPT4048 e 2 Led bianchi.
- Alloggiamento per 4 bobine di filamento.

## Riferimenti

- OpenPrintTag: standard NFC open-source di Prusa Research per bobine smart.
- Specifiche OpenPrintTag: https://openprinttag.org/
- Note hardware ESP32: [docs/hardware/esp32.md](docs/hardware/esp32.md)
- TODO di progetto: [docs/TODO.md](docs/TODO.md)

## Stato del progetto

Repository inizializzato. La documentazione raccoglie le prime specifiche funzionali e hardware; il codice verrà aggiunto durante lo sviluppo.

## Licenza

Questo progetto e distribuito con licenza GNU Affero General Public License v3.0.

SPDX-License-Identifier: AGPL-3.0-or-later
