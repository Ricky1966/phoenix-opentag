# BOM Elettronica — Componenti esterni alla PCB

> I componenti SMT/THT della PCB sono in `hardware/gerber/BOM.csv`.
> Questa lista copre tutto ciò che si collega alla PCB ma non è su di essa.

| Componente | Qt | Note | Stato |
|---|:---:|---|:---:|
| Alimentatore S-360-24 24V 15A | 1 | Montato sopra il box | ✅ In casa |
| Buck converter 24V→5V ≥1A | 1 | Output fisso 5V, entra su J15 PCB | 🛒 Da comprare |
| MOSFET N-ch logic-level (IRLZ44N / STP36NF06L / AOD4184A) | 6 | 4× riscaldatori + 1× fan + 1× solenoide | 🛒 Da comprare |
| Diodo flyback 1N4007 | 2 | Protezione MOSFET sui solenoidi — obbligatorio | 🛒 Da comprare |
| ESP32 AZ-Delivery 38-pin | 1 | Montato su J1/J2 PCB | ✅ In casa |
| PCB ESP32_AZDelivery_Baseboard | 1 | In quotazione PCBA presso PCBWay (Felix) | 🔄 In attesa |
| Riscaldatore kapton 90×40mm 24V ~15W | 4 | Design finale, uno per vano | 🛒 Da comprare |
| Piatto Geeetech A10M 235×235mm 24V | 1 | Solo per test temporaneo | ✅ In casa |
| NTC 100kΩ β=3950 con cavo | 4 | Tipo NTC3950, uno per vano | 🛒 Da comprare |
| Ventola YELUFT 4010 40×40×10mm 24V 1.92W 2-pin | 4 | Una per slot, 6600 RPM, 5.75 CFM | 📦 Ordinata Amazon |
| Ventola Sunon EF92252S1 92mm 24V 1.66W | 1 | Spare (vecchio design) | ✅ In casa |
| Valvola solenoide fspump.cn 24V NC | 2 | ⚠️ Verificare direct-acting a 0 bar prima dell'uso | 📦 Ordinata AliExpress |
| Modulo BME280 | 1 | I2C su GPIO21/22, JST-XH 4-pin → J7 | ✅ In casa |
| Modulo PN5180 clone | 1 | SPI su GPIO18/19/23, FPC 10-pin 0.5mm → J3 | ✅ In casa |
| Modulo OPT4048 + 2× LED bianchi + runout filamento | 1 | I2C + interrupt GPIO15, FPC 8-pin 0.5mm → J16 | 🛒 Da comprare |
| Driver stepper A4988 / DRV8825 / TMC2209 | 1 | Breakout board, riceve STEP/DIR/EN da J17 PCB | 🛒 Da comprare |
| Motore NEMA17 | 1 | Bobine alimentate a 24V tramite driver | 🛒 Da comprare |
| Connettori JST-XH 2-pin + crimper | — | Per riscaldatori, NTC, fan, solenoide | 🛒 Da comprare |
| Cavo silicone multifilo 24V (rosso/nero) | — | Cablaggio potenza verso MOSFET e carichi | 🛒 Da comprare |
