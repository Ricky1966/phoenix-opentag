#pragma once
#include <Arduino.h>

// Dati bobina letti da un tag OpenPrintTag (NDEF Type5 + CBOR).
struct SpoolData {
  bool  present = false;
  char  uid[25] = "";
  char  material[24] = "";
  char  brand[24] = "";
  char  country[8] = "";
  float diameter = NAN;   // mm
  float density  = NAN;   // g/cm3
  int   nozzleMin = 0, nozzleMax = 0;  // °C
  int   bedMin = 0, bedMax = 0;        // °C
  int   weight = 0;                    // g
  uint8_t color[3] = {0, 0, 0};
  bool  hasColor = false;
  unsigned long lastReadMs = 0;
};

extern SpoolData spool;

void nfcSetup();
void nfcLoop();  // poll del lettore ogni NFC_POLL_MS, stampa su seriale quando il tag cambia
