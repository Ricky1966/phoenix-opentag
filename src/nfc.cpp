#include "nfc.h"
#include <SPI.h>
#include <PN5180.h>
#include <PN5180ISO15693.h>
#include "config.h"
#include "transport.h"

static PN5180ISO15693 nfc(PN5180_NSS, PN5180_BUSY, PN5180_RST);
SpoolData spool;
#include "nfc_writer.inc"

enum class ReadResult { Missing, Error, Invalid, Valid };
static uint8_t lastRawUid[8];  // wire order, of the tag found by the latest readTag

static ReadResult readTag(SpoolData *s) {
  nfc.reset();
  if (!nfc.setupRF()) return ReadResult::Error;
  uint8_t uid[8];
  const ISO15693ErrorCode rc = nfc.getInventory(uid);
  if (rc == EC_NO_CARD) return ReadResult::Missing;
  if (rc != ISO15693_EC_OK) return ReadResult::Error;

  memcpy(lastRawUid, uid, 8);
  char *p = s->uid;
  for (int k=7; k>=0; --k) { p += sprintf(p, "%02X", uid[k]); if (k) *p++ = ':'; }
  *p = 0;
  s->present = true;
  uint8_t blockSize = 0;
  uint16_t numBlocks = 0;
  if (nfc.getSystemInfo(uid, &blockSize, &numBlocks) != ISO15693_EC_OK ||
      blockSize == 0 || blockSize > 32 || numBlocks == 0 || numBlocks > 256)
    return ReadResult::Error;

  static uint8_t buf[2048];
  size_t used = 0;
  for (uint16_t b=0; b<numBlocks && blockSize<=sizeof(buf)-used; ++b) {
    uint8_t data[32];
    if (nfc.readSingleBlock(uid, uint8_t(b), data, blockSize) != ISO15693_EC_OK)
      return ReadResult::Error;
    memcpy(buf+used, data, blockSize); used += blockSize;
    const TagParseResult result = parseType5Tag(buf, used, *s);
    if (result == TagParseResult::Valid) return ReadResult::Valid;
    if (result != TagParseResult::Incomplete) return ReadResult::Invalid;
  }
  return ReadResult::Invalid; // incomplete/oversized messages are never published
}

// ICODE SLIX2 (UID E0 04 01 ...) whose user memory, blocks 0-78, is all zero.
// Block 79 is the NXP counter and is ignored. A read error is not "blank".
enum class BlankResult { No, Yes, Error };
static BlankResult checkBlankSlix2() {
  if (lastRawUid[7] != 0xe0 || lastRawUid[6] != 0x04 || lastRawUid[5] != 0x01) return BlankResult::No;
  uint8_t uid[8], blockSize = 0;
  uint16_t blocks = 0;
  memcpy(uid, lastRawUid, 8);
  if (nfc.getSystemInfo(uid, &blockSize, &blocks) != ISO15693_EC_OK) return BlankResult::Error;
  if (blockSize != 4 || blocks != 80) return BlankResult::No;
  for (unsigned b = 0; b < TAG_WRITE_BLOCKS; ++b) {
    uint8_t data[4];
    if (nfc.readSingleBlock(uid, uint8_t(b), data, 4) != ISO15693_EC_OK) return BlankResult::Error;
    if (data[0] | data[1] | data[2] | data[3]) return BlankResult::No;
  }
  return BlankResult::Yes;
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
  if (s.nozzleMax >= 0) { Serial.print("Temp ugello: "); Serial.print(s.nozzleMin); Serial.print("-"); Serial.print(s.nozzleMax); Serial.println(" C"); }
  if (s.bedMax >= 0)    { Serial.print("Temp piatto: "); Serial.print(s.bedMin);    Serial.print("-"); Serial.print(s.bedMax);    Serial.println(" C"); }
  if (!isnan(s.weight))    { Serial.print("Peso: ");        Serial.print(s.weight);    Serial.println(" g"); }
  if (s.country[0])  { Serial.print("Paese: ");     Serial.println(s.country); }
  Serial.println("--------------------");
}

void nfcSetup() {
  nfc.begin();
  nfc.reset();
  uint8_t pv[2] = {0xff, 0xff};
  const bool ok = nfc.readEEprom(PRODUCT_VERSION, pv, sizeof(pv));
  Serial.printf("PN5180 product version = %d.%d %s\n", pv[1], pv[0],
                (!ok || pv[1] == 0xFF) ? "(INIT FALLITA!)" : "(OK)");
}

void nfcLoop() {
  static unsigned long last = 0;
  static unsigned misses = 0;
  static char checkedUid[25] = "";  // tag already classified during this presence
  if (millis() - last < NFC_POLL_MS) return;
  last = millis();
  if(writerPoll()) return;
  SpoolData tmp;
  const ReadResult result = readTag(&tmp);
  if (result == ReadResult::Valid) {
    const bool changed = !spool.valid || strcmp(tmp.uid, spool.uid) != 0;
    tmp.lastReadMs = millis();
    spool = tmp;
    misses = 0;
    checkedUid[0] = 0;
    if (changed) printSpoolData(spool);
  } else {
    // Keep the last good metadata for diagnostics; never send stale values.
    spool.valid = false;
    if (result == ReadResult::Missing) {
      if (++misses >= 3 && spool.present) {
        Serial.println("NFC: bobina rimossa (3 letture consecutive)");
        spool.present = false;
      }
      if (misses >= 3) checkedUid[0] = 0;
    } else {
      misses = 0;
      Serial.println(result == ReadResult::Invalid ?
        "NFC: tag non valido, incompleto o non supportato" : "NFC: errore di lettura, riprovo");
      if (result == ReadResult::Invalid && strcmp(tmp.uid, checkedUid) != 0) {
        const BlankResult blank = checkBlankSlix2();
        if (blank != BlankResult::Error) {
          strlcpy(checkedUid, tmp.uid, sizeof(checkedUid));
          if (blank == BlankResult::Yes) {
            Serial.println("NFC: tag SLIX2 vuoto");
            transportBlank(tmp.uid);
          }
        }
      }
    }
  }
}

