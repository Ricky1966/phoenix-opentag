# Contratto lettore → demone Phoenix (0.5)

POST `http://<Phoenix>:7913/tag`, oppure riga USB `PHOENIX-TAG {json}`.
Solo osservazioni NFC presenti e validate. JSON `schema_version: 1`; `event_id`
identifica la singola osservazione (sessione casuale + contatore). UID NFC-V
esadecimale maiuscolo, 16 cifre, ordine MSB. Il demone accetta anche UID con due
punti e campi legacy 0.4.

Campi: `reader_id`, `firmware`, `instance_uuid`, `material_name`, `brand_name`,
`country_of_origin`, `material_class`, `material_type`, `filament_diameter`,
`density`, `nominal_netto_full_weight`, `actual_netto_full_weight`,
`empty_container_weight`, `min_print_temperature`, `max_print_temperature`,
`min_bed_temperature`, `max_bed_temperature`, `color_rgba` (#RRGGBBAA).
Campi mancanti omessi. Il demone importa nome, marca, RGB, diametro, densità,
peso nominale e temperature; non interpreta ancora enum materiale, UUID o
pesi effettivo/contenitore. L'associazione avviene per UID: tag riscritti per
un'altra bobina richiedono correzione manuale dell'associazione in Spoolman.

HTTP 2xx conferma gestione definitiva; errori API restituiscono 502.
USB conferma con `ack <event_id>\n`; nessun ACK sugli errori.
Solo eventi consecutivi identici sono deduplicati per cinque secondi.
Il firmware conserva solo l'ultima osservazione, ritenta con backoff fino
alla rimozione o sostituzione, senza coda persistente. Al reboot ritrasmette:
il demone risolve prima l'UID in Spoolman e non resetta il peso residuo.

Stampante printing/paused: nessun cambio bobina e nessuna attivazione differita.
Il controllo stato e l'attivazione sono due chiamate separate, non atomiche:
evitare scansioni in concomitanza con l'avvio di una stampa.

Limiti: rete locale fidata, HTTP senza autenticazione/TLS; POST sincrono;
USB 0.4 non ha ACK/retry; creazione bobina e associazione tag sono chiamate
separate (un errore fra le due può lasciare una bobina senza tag, da riconciliare).
Orca Slicer e la calibrazione della stampante non vengono modificati.
