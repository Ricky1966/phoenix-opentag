# Hardware — ESP32 AZ-Delivery Baseboard

> Per l'architettura generale del sistema (alimentazione 24V, MOSFET esterni, buck converter, componenti fuori PCB) vedi [design.md](design.md).

Scheda ESP32 DevKit 38-pin con interfaccia USB-seriale CP2102, rilevata su `/dev/ttyUSB0`.

Chip rilevato durante l'upload:

- ESP32-D0WD-V3, revision v3.0
- MAC: `40:22:d8:e8:7b:58`
- Upload verificato con PlatformIO su porta `/dev/ttyUSB0`

---

## Pin assegnati al progetto

### Tabella completa

| GPIO | Serigrafia | Funzione firmware   | Periferica          | Connettore PCB        | Note                    |
|------|-----------|----------------------|---------------------|-----------------------|-------------------------|
| 5    | G5        | `PN5180_NSS`         | PN5180 SPI CS       | J4 PN5180_SPI (pin 4) | Bootstrap, att. al boot |
| 16   | G16       | `PN5180_BUSY`        | PN5180 CTRL         | J5 PN5180_CTRL (pin 2)|                         |
| 17   | G17       | `PN5180_RST`         | PN5180 CTRL         | J5 PN5180_CTRL (pin 1)|                         |
| 18   | G18       | SPI SCK              | PN5180 SPI CLK      | J4 PN5180_SPI (pin 1) | VSPI default            |
| 19   | G19       | SPI MISO             | PN5180 SPI MISO     | J4 PN5180_SPI (pin 2) | VSPI default            |
| 21   | G21       | I2C SDA              | BME280              | J7 BME280 (pin 3)     |                         |
| 22   | G22       | I2C SCL              | BME280              | J7 BME280 (pin 4)     |                         |
| 23   | G23       | SPI MOSI             | PN5180 SPI MOSI     | J4 PN5180_SPI (pin 3) | VSPI default            |
| 25   | G25       | `HEATER_PIN`         | MOSFET riscaldatore | **da aggiungere**     | Active-low              |
| 34   | G34       | `BED_THERMISTOR_PIN` | NTC piatto          | J8 NTC_SENSOR (pin 1) | Solo input, ADC         |
| 27   | G27       | *(riservato)*        | Ventola 1 PWM       | **da aggiungere**     | Futuro                  |
| 32   | G32       | *(riservato)*        | Ventola 2 PWM       | **da aggiungere**     | Futuro                  |

### Pin liberi (breakout disponibile, non usati dal firmware)

GPIO 0, 2, 4, 12, 13, 14, 15, 26, 33, 35, 36/SP, 39/SN.

---

## Connettori presenti nel progetto KiCad

| Riferimento | Valore | Footprint | Segnali |
|-------------|--------|-----------|---------|
| J1 | ESP32_LEFT_1x19 | PinSocket 1×19 2.54mm | Lato sinistro ESP32 |
| J2 | ESP32_RIGHT_1x19 | PinSocket 1×19 2.54mm | Lato destro ESP32 |
| J3 | PN5180_1x13 | PinSocket 1×13 2.54mm | Pin-header femmina 13-pin per modulo PN5180 |
| J7 | BME280 | JST XH 4-pin | 3V3 · GND · SDA · SCL |
| J8 | NTC_SENSOR | Terminal block Phoenix 5.08mm 2-pin | NTC_ADC · GND |

### Pinout J3 — PN5180 1×13 pin header femmina

Connettore unico che sostituisce i quattro JST separati (J3/J4/J5/J6) presenti in schema nelle versioni precedenti.
Corrisponde al pinout standard del modulo PN5180 NXP/AZ-Delivery.

| Pin | Segnale | Note |
|-----|---------|------|
| 1 | 5V | Alimentazione RF |
| 2 | 3V3 | Alimentazione logica |
| 3 | PN5180_RST | Reset (GPIO 17) |
| 4 | PN5180_NSS | SPI Chip Select (GPIO 5, bootstrap) |
| 5 | PN5180_MOSI | SPI MOSI (GPIO 23) |
| 6 | PN5180_MISO | SPI MISO (GPIO 19) |
| 7 | PN5180_CLK | SPI Clock (GPIO 18) |
| 8 | PN5180_BUSY | Busy flag (GPIO 16) |
| 9 | GND | Massa |
| 10 | PN5180_IRQ | Interrupt (non connesso al firmware) |
| 11 | PN5180_GPIO | GPIO ausiliario (non connesso al firmware) |
| 12 | PN5180_REQ | REQ ausiliario (non connesso al firmware) |
| 13 | PN5180_AUX | AUX ausiliario (non connesso al firmware) |

---

## Circuito NTC piatto

```
3V3 ──── R_pullup (100 kΩ) ──┬── GPIO34 (ADC)
                              │
                             C1 (100 nF)
                              │
NTC (100 kΩ @ 25 °C) ────────┴──── GND
```

- Pullup: **100 kΩ** (costante firmware `BED_PULLUP_OHMS = 100000.0F`)
- NTC nominale: 100 kΩ a 25 °C, β = 3950
- Bypass cap: 100 nF (C1 in schema)
- GPIO 34 è solo-input: nessun problema di boot

> **Nota schematica**: R1 è valorizzato **100 kΩ** nello schema KiCad, in accordo con la costante firmware `BED_PULLUP_OHMS = 100000.0F`.

---

## Circuito MOSFET riscaldatore

```
GPIO25 ──── R_gate ──── GATE
                         │
                        MOSFET N-ch
                         │
                        DRAIN ──── Riscaldatore ──── VCC (24V)
                         │
                        SOURCE ──── GND
```

- GPIO 25, active-low (`HEATER_ACTIVE_LOW = true`): LOW = riscaldatore ON
- Il connettore MOSFET non è ancora nello schema KiCad
- Safety limit firmware: 90 °C (stop automatico)

---

## Alimentazione PN5180

Il modulo PN5180 richiede sia 3,3 V (logica) che 5 V (RF). Entrambi sono portati al connettore J3.

---

## Pin da evitare o usare con cautela

| GPIO | Motivo |
|------|--------|
| CMD, CLK, SD0–SD3 | Linee flash SPI interne, non usare |
| 0 | Bootstrap / programmazione |
| 2, 5, 12, 15 | Bootstrap: pull errati impediscono il boot |
| 1 (TXD), 3 (RXD) | UART0 usata da USB/Serial, non usare durante debug |
| 34, 35, 36, 39 | Solo input, niente pull interno disponibile |

---

## Pinout fisico scheda (38 pin)

### Lato superiore — da sinistra verso destra

| Serigrafia | GPIO | Uso nel progetto |
|-----------|------|-----------------|
| 5V | — | Alimentazione 5 V |
| CMD | — | Flash, non usare |
| SD3 | — | Flash, non usare |
| SD2 | — | Flash, non usare |
| G13 | 13 | Libero |
| GND | — | Massa |
| G12 | 12 | Libero (bootstrap) |
| G14 | 14 | Libero |
| G27 | 27 | **Riservato Ventola 1** |
| G26 | 26 | Libero |
| G25 | 25 | **HEATER MOSFET** |
| G33 | 33 | Libero |
| G32 | 32 | **Riservato Ventola 2** |
| G35 | 35 | Libero (solo input) |
| G34 | 34 | **NTC piatto (ADC)** |
| SN | 39 | Libero (solo input) |
| SP | 36 | Libero (solo input) |
| EN | — | Enable/reset chip |
| 3V3 | — | Alimentazione 3,3 V |

### Lato inferiore — da sinistra verso destra

| Serigrafia | GPIO | Uso nel progetto |
|-----------|------|-----------------|
| CLK | — | Flash, non usare |
| SD0 | — | Flash, non usare |
| SD1 | — | Flash, non usare |
| G15 | 15 | Libero (bootstrap) |
| G2 | 2 | Libero (bootstrap) |
| G0 | 0 | Libero (bootstrap/prog) |
| G4 | 4 | Libero |
| G16 | 16 | **PN5180 BUSY** |
| G17 | 17 | **PN5180 RST** |
| G5 | 5 | **PN5180 NSS/CS** (bootstrap) |
| G18 | 18 | **PN5180 CLK** |
| G19 | 19 | **PN5180 MISO** |
| GND | — | Massa |
| G21 | 21 | **BME280 SDA** |
| RXD | 3 | UART0 RX |
| TXD | 1 | UART0 TX |
| G22 | 22 | **BME280 SCL** |
| G23 | 23 | **PN5180 MOSI** |
| GND | — | Massa |
