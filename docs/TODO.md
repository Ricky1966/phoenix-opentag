# TODO

Lista di lavoro provvisoria, aggiornata mentre il progetto prende forma.

## App mobile (odin_mobile)

### Completato (2026-05-31)
- ✅ Banner Tailscale in home: avviso permanente quando l'URL è un IP Tailscale (100.x.x.x).
- ✅ Rilevamento intelligente errore connessione: distingue tra "Tailscale non attivo" e "Raspberry spento" tramite check TCP su 100.100.100.100:53.
- ✅ Ping proattivo al Raspberry (TCP su 100.97.249.32:8000 ogni 15 s): banner arancio "Raspberry non raggiungibile — accendilo!" appare in 2 s senza aspettare timeout HTTP.
- ✅ Settings screen: URL base configurabile con preset Tailscale/LAN, pulsante Test connessione, persistenza con shared_preferences.
- ✅ Notifica push al termine del ciclo di essiccazione (flutter_local_notifications), con rilevamento transizione cycleRunning true→false.
- ✅ Icona app aggiornata con odin_mobile_fav.png.
- ✅ Splash screen Flutter con odin.jpeg grande (BoxFit.contain, sfondo #102A43, fade-out dopo 2.2 s) — mostrata solo al primo avvio.
- ✅ Splash nativa Android 12+ con PNG trasparente: cerchio icona invisibile, transizione seamless verso la splash Flutter.
- ✅ Schermata Tag NFC: lettura OpenPrintTag via nfc_manager, parsing CBOR, visualizzazione dati bobina, pulsante Applica profilo.
- ✅ NFC intercept: intent filter NDEF + MethodChannel + StreamController — avvicina il tag da qualsiasi schermata e l'app naviga automaticamente alla tab NFC.
- ✅ Grafici storici: 4 grafici (temp/umidità/piatto/riscaldatore) con selezione range 6h/24h/2gg/7gg, dati dal log CSV su Pi5 con downsampling adattivo.
- ✅ Localizzazione IT/EN completa: flutter_localizations + ARB files, lingua segue il dispositivo.

### Completato (2026-05-31 cont.)
- ✅ Profili materiale su Pi5: fonte di verità condivisa tra app e web UI. CRUD dall'app Flutter e dalla web UI ESP32 (proxy GET verso Pi5). Fallback ai profili firmware se Pi5 non raggiungibile.

### Da fare
- (nessun task aperto per l'app mobile)

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
