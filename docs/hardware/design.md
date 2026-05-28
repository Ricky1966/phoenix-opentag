# Architettura elettronica del sistema Essiccatore

## Panoramica

Il sistema è composto da una **scheda di controllo custom** (questa PCB) e da una serie di componenti di potenza **deliberatamente tenuti fuori dalla scheda**. La separazione è stata scelta per motivi di sicurezza, gestione termica e semplicità di assemblaggio.

```
                    ┌─────────────────────────────┐
  24V / 15A ──────► │  BUCK 24V→5V  (esterno)     │──► 5V J15
  Alimentatore      └─────────────────────────────┘
       │
       │  24V
       ▼
  ┌──────────┐    gate signal (3.3V)    ┌──────────────────────┐
  │ MOSFET   │◄────────────────────────│                      │
  │ N-ch ×6  │                         │   PCB di controllo   │
  │(esterni) │──► carico 24V           │   (questa scheda)    │
  └──────────┘                         │                      │
                                       └──────────────────────┘
```

---

## Perché il 24V è esterno alla scheda

I carichi a 24V (riscaldatori, ventola, solenoide) richiedono correnti che dipendono dal tipo di riscaldatore scelto. La corrente complessiva può essere significativa con quattro riscaldatori attivi contemporaneamente. La ventola e il solenoide aggiungono ulteriore assorbimento.

Far scorrere queste correnti attraverso la PCB di controllo comporterebbe:
- Piste larghissime, incompatibili con un layout compatto
- Calore eccessivo vicino all'ESP32 e ai sensori analogici (ADC NTC)
- Rischio di danni all'ESP32 in caso di corto sul lato potenza
- Complessità aggiuntiva per la certificazione/sicurezza

La PCB di controllo lavora esclusivamente a **3.3V / 5V, correnti di segnale** (< 100 mA totali).

---

## Buck converter 24V → 5V (esterno)

Il sistema ha un'unica sorgente di alimentazione a 24V. Per alimentare l'ESP32 e la logica a bassa tensione serve un convertitore buck.

- **Ingresso**: 24V dall'alimentatore principale
- **Uscita**: 5V, minimo 1A (ESP32 + sensori + PCF8574 + periferiche)
- **Connessione a PCB**: morsetto **J15** (MKDS 5.08mm 2-pin, +5V e GND)
- **Jumper JP1**: seleziona la sorgente di alimentazione tra USB (sviluppo/debug) e Buck (produzione); impedisce di connettere USB e Buck contemporaneamente

Il 3.3V usato da sensori e logica digitale viene ricavato dal regolatore interno all'ESP32 AZ-Delivery (già presente sul modulo).

---

## MOSFET di potenza (esterni)

Ogni carico a 24V è controllato da un **MOSFET N-channel** esterno, pilotato direttamente dai GPIO dell'ESP32 (3.3V logic-level).

| Canale | GPIO | Connettore PCB | Carico |
|--------|------|----------------|--------|
| Riscaldatore 1 | GPIO25 | J4 HEATER1_CTRL | Piatto riscaldante zona 1 |
| Riscaldatore 2 | GPIO26 | J5 HEATER2_CTRL | Piatto riscaldante zona 2 |
| Riscaldatore 3 | GPIO33 | J6 HEATER3_CTRL | Piatto riscaldante zona 3 |
| Riscaldatore 4 | GPIO04 | J9 HEATER4_CTRL | Piatto riscaldante zona 4 |
| Ventola | GPIO27 | J10 FAN_CTRL | Ventola 24V (PWM) |
| Solenoide | GPIO32 | J11 SOLENOID_CTRL | Valvola/chiusura meccanica |

**Requisiti MOSFET**:
- V_DS ≥ 30V (margine sul 24V)
- I_D adeguato alla corrente del riscaldatore scelto
- V_GS(th) ≤ 2V — **logic-level**, deve accendersi completamente con 3.3V sul gate
- Esempi compatibili: IRLZ44N, STP36NF06L, AOD4184A

Ogni connettore CTRL porta solo il **segnale gate** (1 pin segnale + 1 GND). Il circuito di potenza (drain→carico→24V, source→GND) è completamente esterno alla PCB.

---

## Componenti sulla PCB

### ESP32 AZ-Delivery 38-pin (J1 + J2)
Microcontrollore principale. Montato su due socket femmina 1×19 a 2.54mm. Gestisce:
- Controllo PID dei riscaldatori
- Lettura sensori NTC via ADC
- Comunicazione I2C (BME280, PCF8574, OPT4048)
- Comunicazione SPI (PN5180 NFC)
- Web dashboard via Wi-Fi

### PCF8574AP — I2C I/O Expander (U1)
Expander I2C a 8 bit in package DIP-16, indirizzo 0x20 (A2=A1=A0=GND).
Estende le uscite digitali dell'ESP32 per controllare LED di stato, indicatori o canali aggiuntivi senza consumare GPIO.

### BME280 — Sensore T°/Umidità (J7)
Modulo esterno collegato via JST-XH 4-pin: 3V3, GND, SDA (GPIO21), SCL (GPIO22).
Misura temperatura e umidità relativa all'interno della camera di essiccazione.

### PN5180 — Lettore NFC (J3, FPC 10-pin)
Modulo NFC a 13.56 MHz per la lettura dei tag OpenPrintTag sulle bobine.
Collegato via VSPI (GPIO18/19/23) + CS GPIO5 + BUSY GPIO16 + RST GPIO17.
Alimentato a 3.3V (logica) e 5V (RF), entrambi forniti dalla PCB.

### OPT4048 + LED bianchi + Runout filamento (J16, FPC 8-pin)
Il connettore J16 porta un modulo composto da tre elementi che lavorano insieme:

- **OPT4048** — sensore di colore tristimolo ad alta precisione, collegato via I2C (indirizzo fisso, bus condiviso con BME280 e PCF8574), interrupt su GPIO15
- **2× LED bianchi** — illuminano il filamento o la bobina per garantire una lettura colore stabile e indipendente dalla luce ambiente
- **Runout filament sensor** — rileva l'assenza del filamento (esaurimento bobina) analizzando la variazione di riflettanza/colore quando il filamento non è presente davanti al sensore

Il sistema permette di identificare il colore del filamento in uso e di rilevare automaticamente quando la bobina è esaurita, integrando le informazioni con i tag NFC letti dal PN5180.

### Circuito NTC ×4 (R1–R4, C1–C5)
Quattro divisori resistivi identici per lettura termistore NTC:

```
3V3 ──── R_pullup (100kΩ) ──┬── GPIO_ADC
                             C (100nF)
                             │
                  NTC ───────┴──── GND
```

| NTC | GPIO | Connettore |
|-----|------|------------|
| NTC1 | GPIO34 | J8 |
| NTC2 | GPIO35 | J12 |
| NTC3 | GPIO36 | J13 |
| NTC4 | GPIO39 | J14 |

GPIO34–39 sono **solo input** e non hanno pull-up interni: il pull-up esterno (R1–R4, 100kΩ) è indispensabile. I condensatori C1–C5 (100nF) filtrano il rumore sul segnale ADC.

### Motore stepper NEMA17 (J17, JST-XH 4-pin)
La PCB espone i segnali di controllo per un driver stepper esterno tramite il connettore J17:

| Pin J17 | Segnale | Sorgente | Funzione |
|---------|---------|----------|----------|
| 1 | GND | — | Massa riferimento |
| 2 | EN | PCF8574 P0 (I2C) | Enable driver (LOW = attivo) |
| 3 | STEP | GPIO14 | Impulso passo |
| 4 | DIR | GPIO13 | Direzione rotazione |

Il segnale EN è gestito dal PCF8574 (I2C expander, P0) invece che da un GPIO diretto, risparmiando un pin ESP32.

**Componenti esterni necessari:**
- **Driver stepper** (breakout board): A4988, DRV8825 o TMC2208/TMC2209. Riceve STEP/DIR/EN dalla PCB e converte in corrente per le bobine del motore.
- **Alimentazione bobine**: 24V dall'alimentatore principale (stessa sorgente dei riscaldatori). Il driver regola la corrente alle bobine internamente.
- **Motore**: NEMA17, collegato al driver tramite cavo a 4 fili (2 bobine).

```
PCB J17 ──STEP/DIR/EN──► Driver (A4988/DRV8825/TMC22xx)
                              │
                    24V ──────┤  VMOT
                    5V  ──────┤  VDD (logica driver)
                              │
                           NEMA17 (bobine A/B)
```

### Pull-up I2C (R7, R8)
Due resistori da 4.7kΩ collegano SDA e SCL a 3.3V. Necessari perché il bus I2C ha tre dispositivi (BME280 + PCF8574 + OPT4048) e il cavo FPC introduce capacità parassita.

### LED di stato (D1 + R5)
LED verde 0805 con resistore di limitazione 330Ω su F.Cu. Indicatore visivo di alimentazione/stato.

### R6 (120Ω)
Resistore di terminazione o limitazione corrente per segnale digitale (PN5180 o linea FPC).

---

## Schema di alimentazione

```
Rete 230V AC
     │
     ▼
Alimentatore 24V / 15A
     │
     ├──► Buck 24V→5V ──► J15 (PCB) ──► ESP32 5V ──► 3.3V (interno ESP32)
     │                                               └──► Sensori, PCF8574
     │
     └──► MOSFET gate (da PCB GPIO) ──► Riscaldatori / Ventola / Solenoide
```

---

## Componenti fuori dalla PCB — lista di acquisto

| Componente                                 |Qt | Note                                                    |
|--------------------------------------------|---|---------------------------------------------------------|
| MOSFET N-ch logic-level (es. IRLZ44N)      | 6 | 4× riscaldatori + ventola + solenoide                   |
| Buck converter 24V→5V ≥ 1A                 | 1 | Con output regolabile o fisso 5V                        |
| Riscaldatore 24V                           | 4 | Tipo da definire in base alla geometria del contenitore |
| NTC 100kΩ β=3950 con cavo                  | 4 | Tipo NTC3950                                            |
| Ventola 24V                                | 1 | Con segnale PWM o ON/OFF                                |
| Solenoide 24V                              | 1 | Per chiusura meccanica camera                           |
| Modulo BME280                              | 1 | Con connettore JST-XH 4-pin o adattatore                |
| Driver stepper (A4988 / DRV8825 / TMC2209) | 1 | Breakout board esterna, riceve STEP/DIR/EN dalla PCB    |
| Motore NEMA17                              | 1 | Bobine alimentate a 24V tramite driver                  |
| Modulo PN5180                              | 1 | Con cavo FPC 10-pin 0.5mm                               |
| Modulo OPT4048 + 2× LED bianchi + runout   | 1 | Con cavo FPC 8-pin 0.5mm                                |
