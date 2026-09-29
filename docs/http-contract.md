# Contratto del bridge Phoenix, schema 1

Questa versione aggiunge il **client** HTTP sul lettore. Il ricevitore CB1 e
l'integrazione Spoolman/Moonraker non sono inclusi e non vengono installati.
L'endpoint non coincide con `/api/v1/tag/scan` di Spoolman: il nostro payload
contiene i dati OpenPrintTag già decodificati, oltre all'UID.

In `src/secrets.h`, solo quando il bridge sarà disponibile:

```cpp
#define PHOENIX_TAG_ENDPOINT "http://<CB1-IP>:<PORT>/tag"
#define PHOENIX_READER_ID "phoenix-opentag"
```

Un endpoint vuoto (default) disattiva il trasporto. Questa prima versione supporta
HTTP sulla rete locale; URL HTTPS vengono rifiutati, senza downgrade. Non pubblicare
il bridge su Internet. Non sono inclusi token o credenziali di servizi.

Esempio di POST `Content-Type: application/json`:

```json
{
  "schema_version": 1,
  "reader_id": "phoenix-opentag",
  "firmware": "0.3.0-dev",
  "uid": "E004010203040506",
  "material_class": 0,
  "material_type": 0,
  "material_name": "PLA Galaxy Black",
  "brand_name": "Prusament",
  "filament_diameter": 1.75,
  "nominal_netto_full_weight": 1000,
  "actual_netto_full_weight": 1012
}
```

- UID: 8 byte NFC-V, MSB prima, esadecimale maiuscolo senza separatori.
- `instance_uuid`, quando presente sul tag, distingue riutilizzi dello stesso tag.
  Il ricevitore deve gestire esplicitamente il cambio di UUID sul medesimo UID;
  non deve sovrascrivere silenziosamente una vecchia bobina.
- I campi opzionali assenti vengono omessi, mai serializzati come NaN.
- `material_name` è il nome commerciale. `material_type` è il codice enum
  OpenPrintTag, non un ID Spoolman. Nessuna temperatura viene applicata alla stampante.
- `color_rgba`: `#RRGGBBAA`, alpha 255 quando il tag contiene solo RGB.
- Pesi in grammi, diametro in mm, densità in g/cm³, temperature in °C.
- I pesi full/nominal/actual **non sono il residuo**: ripassare il tag non deve
  ripristinare il peso residuo già contabilizzato da Spoolman.

## Conferma e ritentativi

Solo risposte HTTP 2xx confermano la consegna. Errori di rete, 3xx, 4xx e 5xx
producono ritentativi dopo 5, 10, 20, 40 e poi 60 secondi. Il ricevitore deve essere
idempotente: la risposta può perdersi dopo che la richiesta è stata applicata.
L'UID identifica il tag fisico; usare anche `instance_uuid` quando disponibile.

Lo stesso contenuto confermato non viene reinviato a ogni polling. Una rimozione
confermata dopo tre polling senza tag permette una nuova scansione della stessa
bobina. Errori temporanei sospendono gli invii finché non torna una lettura valida.
Una nuova lettura valida sostituisce l'eventuale invio ancora da consegnare: non
viene riprodotta una coda di vecchie bobine alla riconnessione. Lo stato di consegna
è in RAM: un riavvio può ripetere il POST. Un bridge riavviato dopo una consegna
confermata richiede una nuova scansione se non conserva la propria associazione.

Il client usa timeout di connessione e risposta di 1500 ms e non segue redirect.
Il POST è sincrono: durante un tentativo HTTP il polling NFC può essere ritardato.
Usare l'IP locale evita ritardi DNS. La conferma riguarda l'ultima lettura valida,
non una garanzia che la bobina sia rimasta sul lettore durante la richiesta.

## Responsabilità del futuro ricevitore

Validare nuovamente lo schema; associare/creare la bobina in modo idempotente;
non azzerare il consumo; chiedere intervento per tag riutilizzati/identità ambigue.
L'attivazione in Moonraker deve rispettare lo stato della stampante: una scansione
accidentale durante una stampa non deve cambiare la bobina tracciata. Confermare 2xx
solo quando il bridge ha realmente accettato e gestito l'evento.
Non cambiare automaticamente parametri Orca o Klipper da un tag.
