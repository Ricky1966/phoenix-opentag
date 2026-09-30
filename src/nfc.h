#pragma once
#include <Arduino.h>
#include "opt_parse.h"

extern SpoolData spool;

// Memoria grezza (Capability Container, TLV, NDEF) dell'ultimo tag letto, per
// inoltrarla a Spoolman come payload_b64. Vuota se nessun tag e' presente.
const uint8_t *nfcRaw(int *len);


void nfcSetup();
void nfcLoop();  // poll del lettore ogni NFC_POLL_MS, stampa su seriale quando il tag cambia
