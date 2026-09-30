#pragma once
// Parser OpenPrintTag senza dipendenze da Arduino, cosi' si puo' provare sul PC
// contro i tag di esempio ufficiali (vedi tools/host-test).
#include <math.h>
#include <stdint.h>

// Dati bobina letti da un tag OpenPrintTag (NDEF Type5 + CBOR).
struct SpoolData {
  bool  present = false;
  bool  optTag = false;   // trovato il record MIME application/vnd.openprinttag
  char  uid[25] = "";
  char  material[64] = "";  // material_name, max 63 caratteri da specifica
  char  brand[32] = "";     // brand_name, max 31
  char  country[8] = "";
  float diameter = NAN;   // mm
  float density  = NAN;   // g/cm3
  int   nozzleMin = 0, nozzleMax = 0;  // °C
  int   bedMin = 0, bedMax = 0;        // °C
  int   weight = 0;                    // g (nominal_netto_full_weight)
  uint8_t color[3] = {0, 0, 0};
  bool  hasColor = false;
  unsigned long lastReadMs = 0;
};

// Estrae i campi dal dump della memoria di un tag NFC-V (Capability Container,
// TLV, NDEF). Legge la sezione meta per trovare la regione principale e accetta
// mappe CBOR a lunghezza definita e indefinita, record NDEF corti e lunghi.
// Per i tag scritti senza sezione meta (formato usato da Odin) prova comunque
// la mappa iniziale come sezione principale.
// Ritorna true se ha trovato e letto un payload; i campi non presenti restano
// ai valori iniziali. Non legge mai fuori da [mem, mem+len).
bool optParse(const uint8_t *mem, int len, SpoolData *out);
