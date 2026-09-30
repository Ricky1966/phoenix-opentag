#include "nfc.h"
#include <SPI.h>
#include <PN5180.h>
#include <PN5180ISO15693.h>
#include "config.h"
#include "transport.h"

static PN5180ISO15693 nfc(PN5180_NSS, PN5180_BUSY, PN5180_RST);
SpoolData spool;

// ───────────────────────── lettura tag ─────────────────────────
// Un tag NFC-V di OpenPrintTag (ICODE SLIX2) ha 320 byte: si legge tutta la memoria
// una volta sola, quando compare un tag nuovo, e la si inoltra cosi' com'e'.
static constexpr int RAW_MAX = 512;
static uint8_t rawBuf[RAW_MAX];
static int rawLen = 0;
static bool rawComplete = false;

const uint8_t *nfcRaw(int *len) { *len = rawLen; return rawBuf; }

static bool readTag(SpoolData *s) {
  nfc.reset();
  bool rfOk = nfc.setupRF();
  uint8_t uid[8];
  ISO15693ErrorCode rc = nfc.getInventory(uid);
  if (rc != ISO15693_EC_OK) {
    // nfc.strerror() e' inutilizzabile su questo toolchain: il parametro si
    // chiama "errno", macro di sistema che corrompe la firma della funzione
    // in fase di preprocessore. Ci limitiamo al codice numerico (vedi
    // ISO15693ErrorCode in PN5180ISO15693.h per il significato).
    Serial.print("getInventory: rfReady="); Serial.print(rfOk ? "si" : "no");
    Serial.print(" codice="); Serial.println((int)rc);
    return false;
  }

  char *p = s->uid;
  for (int k = 7; k >= 0; k--) { p += sprintf(p, "%02X", uid[k]); if (k) *p++ = ':'; }
  *p = 0;
  s->present = true;

  // Stesso tag di prima e memoria gia' letta per intero: niente riletture.
  if (spool.present && rawComplete && strcmp(s->uid, spool.uid) == 0) { *s = spool; return true; }

  rawLen = 0;
  rawComplete = false;
  uint8_t blockSize = 0, numBlocks = 0;
  if (nfc.getSystemInfo(uid, &blockSize, &numBlocks) != ISO15693_EC_OK) return true;  // solo UID
  if (blockSize == 0 || blockSize > 16) return true;

  int blocks = numBlocks;
  if (blocks > RAW_MAX / blockSize) blocks = RAW_MAX / blockSize;
  bool all = true;
  for (int b = 0; b < blocks; b++) {
    uint8_t data[16];
    if (nfc.readSingleBlock(uid, b, data, blockSize) != ISO15693_EC_OK) { all = false; break; }
    memcpy(rawBuf + rawLen, data, blockSize);
    rawLen += blockSize;
  }
  rawComplete = all && blocks == numBlocks;
  optParse(rawBuf, rawLen, s);
  return true;
}

static void printSpoolData(const SpoolData &s) {
  Serial.println("--- OpenPrintTag ---");
  Serial.print("UID: ");      Serial.println(s.uid);
  if (s.brand[0])    { Serial.print("Brand: ");    Serial.println(s.brand); }
  if (s.material[0]) { Serial.print("Materiale: ");Serial.println(s.material); }
  if (s.hasColor) {
    char hex[8]; sprintf(hex, "#%02X%02X%02X", s.color[0], s.color[1], s.color[2]);
    Serial.print("Colore: "); Serial.println(hex);
  }
  if (!isnan(s.diameter)) { Serial.print("Diametro: "); Serial.print(s.diameter, 2); Serial.println(" mm"); }
  if (!isnan(s.density))  { Serial.print("Densita: ");  Serial.print(s.density, 3);  Serial.println(" g/cm3"); }
  if (s.nozzleMax) { Serial.print("Temp ugello: "); Serial.print(s.nozzleMin); Serial.print("-"); Serial.print(s.nozzleMax); Serial.println(" C"); }
  if (s.bedMax)    { Serial.print("Temp piatto: "); Serial.print(s.bedMin);    Serial.print("-"); Serial.print(s.bedMax);    Serial.println(" C"); }
  if (s.weight)    { Serial.print("Peso: ");        Serial.print(s.weight);    Serial.println(" g"); }
  if (s.country[0])  { Serial.print("Paese: ");     Serial.println(s.country); }
  if (rawLen > 0) {
    Serial.print("Memoria: "); Serial.print(rawLen); Serial.print(" byte");
    Serial.println(s.optTag ? " (OpenPrintTag)" : " (NDEF OpenPrintTag non riconosciuto)");
  }
  Serial.println("--------------------");
}

void nfcSetup() {
  nfc.begin();
  nfc.reset();
  uint8_t pv[2];
  nfc.readEEprom(PRODUCT_VERSION, pv, sizeof(pv));
  Serial.printf("PN5180 product version = %d.%d %s\n", pv[1], pv[0],
                pv[1] == 0xFF ? "(INIT FALLITA!)" : "(OK)");
}

void nfcLoop() {
  static unsigned long last = 0;
  static uint8_t misses = 0;
  if (millis() - last < NFC_POLL_MS) return;
  last = millis();

  SpoolData tmp;                 // azzera i campi ad ogni lettura
  bool wasPresent = spool.present;
  if (readTag(&tmp)) {
    misses = 0;
    tmp.lastReadMs = millis();
    bool changed = !wasPresent || strcmp(tmp.uid, spool.uid) != 0;
    spool = tmp;
    if (changed) {
      printSpoolData(spool);
      transportSendTag(spool);
    }
  } else if (wasPresent && ++misses >= 2) {
    // Un solo giro a vuoto capita con il tag fermo: la rimozione si dichiara al secondo.
    Serial.println("NFC: bobina rimossa");
    spool.present = false;
    rawLen = 0;
    rawComplete = false;
    misses = 0;
  }
}
