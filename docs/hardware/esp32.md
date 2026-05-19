# ESP32 usato nel progetto

Scheda ESP32 DevKit con interfaccia USB-seriale CP2102, rilevata su `/dev/ttyUSB0`.

Chip rilevato durante l'upload:

- ESP32-D0WD-V3, revision v3.0.
- MAC: `40:22:d8:e8:7b:58`.
- Upload verificato con PlatformIO su porta `/dev/ttyUSB0`.

## Pin serigrafati sulla scheda

Vista come nella foto, con connettore USB a sinistra e modulo ESP32 a destra.

### Lato superiore

Da sinistra verso destra:

| Serigrafia | Nota |
| --- | --- |
| 5V | Alimentazione 5 V da USB/VIN scheda |
| CMD | Linea flash, evitare per I/O generico |
| SD3 | Linea flash, evitare per I/O generico |
| SD2 | Linea flash, evitare per I/O generico |
| G13 | GPIO 13 |
| GND | Massa |
| G12 | GPIO 12, pin di bootstrap: usare con cautela |
| G14 | GPIO 14 |
| G27 | GPIO 27 |
| G26 | GPIO 26 |
| G25 | GPIO 25 |
| G33 | GPIO 33 |
| G32 | GPIO 32 |
| G35 | GPIO 35, solo input |
| G34 | GPIO 34, solo input |
| SN | Sensor VN / GPIO 39, solo input |
| SP | Sensor VP / GPIO 36, solo input |
| EN | Enable/reset chip |
| 3V3 | Alimentazione 3,3 V |

### Lato inferiore

Da sinistra verso destra:

| Serigrafia | Nota |
| --- | --- |
| CLK | Linea flash, evitare per I/O generico |
| SD0 | Linea flash, evitare per I/O generico |
| SD1 | Linea flash, evitare per I/O generico |
| G15 | GPIO 15, pin di bootstrap: usare con cautela |
| G2 | GPIO 2, pin di bootstrap: usare con cautela |
| G0 | GPIO 0, pin di bootstrap/programmazione |
| G4 | GPIO 4 |
| G16 | GPIO 16 |
| G17 | GPIO 17 |
| G5 | GPIO 5, pin di bootstrap: usare con cautela |
| G18 | GPIO 18 |
| G19 | GPIO 19 |
| GND | Massa |
| G21 | GPIO 21 |
| RXD | UART0 RX / GPIO 3 |
| TXD | UART0 TX / GPIO 1 |
| G22 | GPIO 22 |
| G23 | GPIO 23 |
| GND | Massa |

## Pin consigliati per il progetto

- I2C BME280: GPIO 21 SDA, GPIO 22 SCL.
- SPI PN5180: GPIO 18 SCK, GPIO 19 MISO, GPIO 23 MOSI, GPIO 5 CS/SS.
- Controllo PN5180: GPIO 16 BUSY, GPIO 17 RST.
- Stepper driver: GPIO 25 STEP, GPIO 26 DIR, GPIO 27 ENABLE.
- Controllo riscaldatore/PID: GPIO 32 o GPIO 33 verso driver/MOSFET/SSR, non direttamente al piatto.

## Pin da evitare o usare con cautela

- Evitare `CMD`, `CLK`, `SD0`, `SD1`, `SD2`, `SD3`: sono legati alla flash.
- Evitare carichi o pull errati su GPIO 0, 2, 5, 12, 15: influenzano il boot.
- GPIO 34, 35, 36/SP e 39/SN sono solo input.
- RXD/TXD sono usati dalla seriale USB; meglio lasciarli liberi durante sviluppo e debug.
