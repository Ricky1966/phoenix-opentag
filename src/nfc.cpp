#include "nfc.h"
#include <SPI.h>
#include <PN5180.h>
#include <PN5180ISO15693.h>
#include "config.h"

static PN5180ISO15693 nfc(PN5180_NSS, PN5180_BUSY, PN5180_RST);
SpoolData spool;

// ───────────────────────── CBOR minimale (solo lettura) ─────────────────────────
struct Cur { const uint8_t *b; int n; int i; };

static uint64_t cborHead(Cur *c, int *mt, int *ai) {
  uint8_t ib = c->b[c->i++];
  *mt = ib >> 5; *ai = ib & 0x1f;
  if (*ai < 24) return *ai;
  int len = (*ai == 24) ? 1 : (*ai == 25) ? 2 : (*ai == 26) ? 4 : 8;
  uint64_t v = 0;
  for (int k = 0; k < len && c->i < c->n; k++) v = (v << 8) | c->b[c->i++];
  return v;
}

static void cborSkip(Cur *c);
static void cborSkipContent(Cur *c, int mt, uint64_t val) {
  if (mt == 2 || mt == 3) c->i += (int)val;
  else if (mt == 4) for (uint64_t k = 0; k < val; k++) cborSkip(c);
  else if (mt == 5) for (uint64_t k = 0; k < val; k++) { cborSkip(c); cborSkip(c); }
}
static void cborSkip(Cur *c) {
  int mt, ai; uint64_t v = cborHead(c, &mt, &ai); cborSkipContent(c, mt, v);
}

static float cborFloat(int ai, uint64_t bits) {
  if (ai == 26) { uint32_t b = (uint32_t)bits; float f; memcpy(&f, &b, 4); return f; }
  if (ai == 27) { uint64_t b = bits; double d; memcpy(&d, &b, 8); return (float)d; }
  if (ai == 25) {  // float16
    uint16_t h = (uint16_t)bits; int s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    float f = (e == 0) ? (m / 1024.0f) * 0.00006103515625f
                       : (1 + m / 1024.0f) * powf(2, e - 15);
    return s ? -f : f;
  }
  return NAN;
}

static void copyText(char *dst, int cap, Cur *c, uint64_t len) {
  int n = (int)len < cap - 1 ? (int)len : cap - 1;
  memcpy(dst, c->b + c->i, n); dst[n] = 0;
  c->i += (int)len;
}

static void parseCbor(const uint8_t *p, int len, SpoolData *s) {
  Cur c = {p, len, 0};
  while (c.i < c.n) {
    int mt, ai; uint64_t v = cborHead(&c, &mt, &ai);
    if (mt == 5 && v > 0) {                    // mappa dati
      for (uint64_t e = 0; e < v && c.i < c.n; e++) {
        int kmt, kai; uint64_t key = cborHead(&c, &kmt, &kai);
        int vmt, vai; uint64_t val = cborHead(&c, &vmt, &vai);
        if (kmt != 0) { cborSkipContent(&c, vmt, val); continue; }
        switch ((int)key) {
          case 10: copyText(s->material, sizeof(s->material), &c, val); break;
          case 11: copyText(s->brand,    sizeof(s->brand),    &c, val); break;
          case 55: copyText(s->country,  sizeof(s->country),  &c, val); break;
          case 19:  // colore RGB (3 byte)
            if (vmt == 2 && val >= 3) { s->color[0]=c.b[c.i]; s->color[1]=c.b[c.i+1]; s->color[2]=c.b[c.i+2]; s->hasColor=true; }
            cborSkipContent(&c, vmt, val); break;
          case 30: s->diameter = cborFloat(vai, val); break;
          case 29: s->density  = cborFloat(vai, val); break;
          case 34: s->nozzleMin = (int)val; break;
          case 35: s->nozzleMax = (int)val; break;
          case 37: s->bedMin = (int)val; break;
          case 38: s->bedMax = (int)val; break;
          case 16: s->weight = (int)val; break;
          default: cborSkipContent(&c, vmt, val); break;
        }
      }
      return;
    }
    cborSkipContent(&c, mt, v);               // salta mappa vuota / altri item
  }
}

// ───────────────────────── NDEF Type5 → payload ─────────────────────────
static const uint8_t *ndefPayload(const uint8_t *d, int dlen, int *outLen) {
  int i = (dlen > 0 && d[0] == 0xE1) ? 4 : 0;     // salta Capability Container
  if (i >= dlen || d[i] != 0x03) return nullptr;  // NDEF Message TLV
  i++;
  if (i >= dlen) return nullptr;
  int ln = d[i++];
  if (ln == 0xFF) { ln = (d[i] << 8) | d[i + 1]; i += 2; }
  if (i + 3 > dlen) return nullptr;
  uint8_t hdr = d[i++];
  int tlen = d[i++];
  int plen = d[i++];                              // short record
  int idlen = (hdr & 0x08) ? d[i++] : 0;
  i += tlen + idlen;
  if (i + plen > dlen) plen = dlen - i;
  if (plen <= 0) return nullptr;
  *outLen = plen;
  return d + i;
}

// ───────────────────────── lettura tag ─────────────────────────
static bool readTag(SpoolData *s) {
  nfc.reset();
  bool rfOk = nfc.setupRF();
  uint8_t uid[8];
  ISO15693ErrorCode rc = nfc.getInventory(uid);
  if (rc != ISO15693_EC_OK) {
    Serial.print("getInventory: rfReady="); Serial.print(rfOk ? "si" : "no");
    Serial.print(" codice="); Serial.print((int)rc);
    Serial.print(" ("); Serial.print(nfc.strerror(rc)); Serial.println(")");
    return false;
  }

  char *p = s->uid;
  for (int k = 7; k >= 0; k--) { p += sprintf(p, "%02X", uid[k]); if (k) *p++ = ':'; }
  *p = 0;
  s->present = true;

  uint8_t blockSize = 0, numBlocks = 0;
  if (nfc.getSystemInfo(uid, &blockSize, &numBlocks) != ISO15693_EC_OK) return true;  // solo UID

  static uint8_t buf[200];
  int blen = 0, maxB = numBlocks < 40 ? numBlocks : 40;   // ~160 byte coprono l'NDEF
  for (int b = 0; b < maxB && blen + blockSize <= (int)sizeof(buf); b++) {
    uint8_t data[8];
    if (nfc.readSingleBlock(uid, b, data, blockSize) != ISO15693_EC_OK) break;
    memcpy(buf + blen, data, blockSize); blen += blockSize;
  }
  int plen = 0;
  const uint8_t *pay = ndefPayload(buf, blen, &plen);
  if (pay) parseCbor(pay, plen, s);
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
  if (millis() - last < NFC_POLL_MS) return;
  last = millis();

  SpoolData tmp;                 // azzera i campi ad ogni lettura
  bool wasPresent = spool.present;
  if (readTag(&tmp)) {
    tmp.lastReadMs = millis();
    bool changed = !wasPresent || strcmp(tmp.uid, spool.uid) != 0;
    spool = tmp;
    if (changed) printSpoolData(spool);
  } else {
    if (wasPresent) Serial.println("NFC: bobina rimossa");
    spool.present = false;
  }
}
