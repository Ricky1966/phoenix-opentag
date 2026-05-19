# TODO

Lista di lavoro provvisoria, aggiornata mentre il progetto prende forma.

## Firmware e portale

- Completare la traduzione dei messaggi JavaScript rimasti hardcoded nelle pagine Home, SetPoint, Settings e Lettura TAG.
- Rendere piu robusto il cambio lingua, evitando testi misti dopo azioni asincrone.
- Rifinire la gestione WiFi:
  - verificare salvataggio credenziali da rete router e da AP;
  - mostrare sempre l'indirizzo corretto raggiungibile;
  - valutare un riavvio guidato dopo il cambio modalita rete;
  - preparare la base per OTA.
- Migliorare la pagina Lettura TAG:
  - parsare tutti i campi OpenPrintTag letti dal tag, non solo il colore;
  - ripopolare il form con brand, materiale, temperature, pesi, lunghezze e dimensioni bobina;
  - distinguere meglio dati tag, stato lettore e debug PN5180.
- Migliorare la pagina SetPoint:
  - completare i messaggi localizzati;
  - chiarire la differenza tra profilo consigliato, ciclo applicato e modifica manuale;
  - verificare profili personalizzati dopo riavvio.

## Controllo termico

- Continuare il tuning PI nel contenitore reale, non solo con il piatto sulla scrivania.
- Valutare se introdurre un termine D solo dopo prove con volume chiuso e ventilazione.
- Aggiungere controlli di sicurezza:
  - timeout di riscaldamento;
  - errore NTC scollegata o valori incoerenti;
  - limite massimo configurabile ma protetto.

## Hardware futuro

- Integrare due ventole 24 V su GPIO 27 e GPIO 32, con controllo PWM tramite MOSFET.
- Valutare servo per sportellino di sfiato aria calda/umida, alimentato a 5 V dal buck.
- Valutare stepper per rotazione bobine solo dopo aver definito meccanica e reale utilita.
- Allineare la documentazione pinout al cablaggio definitivo e alla futura PCB.

## Documentazione

- Creare e mantenere la documentazione in `docs/`:
  - pinout definitivo;
  - collegamenti BME280, PN5180, NTC, MOSFET, ventole;
  - uso del portale web;
  - setup WiFi;
  - scrittura e lettura OpenPrintTag;
  - note di sicurezza elettrica e termica.
- Aggiungere in seguito un pulsante HELP nel portale.
- Valutare piu avanti se trasformare `docs/` in GitHub Wiki o usarla come sorgente per la Wiki.
