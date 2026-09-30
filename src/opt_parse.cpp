#include "opt_parse.h"
#include <string.h>

// Formato: https://specs.openprinttag.org (repository prusa3d/OpenPrintTag,
// data/main_fields.yaml e data/meta_fields.yaml per i numeri delle chiavi).

namespace {

const char MIME[] = "application/vnd.openprinttag";
const int MIME_LEN = sizeof(MIME) - 1;

// ───────────────────────── CBOR ─────────────────────────
struct Cur {
  const uint8_t *b;
  int n;
  int i;
  bool err;
};

bool need(Cur *c, int k) {
  if (c->err) return false;
  if (k < 0 || c->i + k > c->n) { c->err = true; return false; }
  return true;
}

// Legge l'intestazione di un item. `indef` e' vero per lunghezza indefinita (e per
// il break code 0xFF, che ha mt 7).
uint64_t head(Cur *c, int *mt, int *ai, bool *indef) {
  *mt = 0; *ai = 0; *indef = false;
  if (!need(c, 1)) return 0;
  uint8_t ib = c->b[c->i++];
  *mt = ib >> 5;
  *ai = ib & 0x1f;
  if (*ai < 24) return (uint64_t)*ai;
  if (*ai == 31) { *indef = true; return 0; }
  int len = (*ai == 24) ? 1 : (*ai == 25) ? 2 : (*ai == 26) ? 4 : (*ai == 27) ? 8 : -1;
  if (len < 0) { c->err = true; return 0; }
  if (!need(c, len)) return 0;
  uint64_t v = 0;
  for (int k = 0; k < len; k++) v = (v << 8) | c->b[c->i++];
  return v;
}

bool atBreak(Cur *c) { return c->i < c->n && c->b[c->i] == 0xFF; }

void skipItem(Cur *c, int depth);

// Salta il corpo di un item la cui intestazione e' gia' stata letta.
void skipBody(Cur *c, int mt, uint64_t v, bool indef, int depth) {
  if (c->err) return;
  if (depth > 8) { c->err = true; return; }
  switch (mt) {
    case 2:
    case 3:
      if (indef) {
        while (!c->err) {
          if (!need(c, 1)) return;
          if (atBreak(c)) { c->i++; return; }
          skipItem(c, depth + 1);
        }
      } else if (v > (uint64_t)(c->n - c->i)) {
        c->err = true;
      } else {
        c->i += (int)v;
      }
      break;
    case 4:
    case 5: {
      int per = (mt == 5) ? 2 : 1;
      if (indef) {
        while (!c->err) {
          if (!need(c, 1)) return;
          if (atBreak(c)) { c->i++; return; }
          for (int k = 0; k < per; k++) skipItem(c, depth + 1);
        }
      } else {
        for (uint64_t k = 0; k < v && !c->err; k++)
          for (int j = 0; j < per; j++) skipItem(c, depth + 1);
      }
      break;
    }
    case 6:
      skipItem(c, depth + 1);
      break;
    default:
      if (mt == 7 && indef) c->err = true;  // break code fuori posto
      break;
  }
}

void skipItem(Cur *c, int depth) {
  int mt, ai; bool indef;
  uint64_t v = head(c, &mt, &ai, &indef);
  skipBody(c, mt, v, indef, depth);
}

bool numberValue(int mt, int ai, uint64_t v, double *out) {
  if (mt == 0) { *out = (double)v; return true; }
  if (mt == 1) { *out = -1.0 - (double)v; return true; }
  if (mt != 7) return false;
  if (ai == 26) { uint32_t b = (uint32_t)v; float f; memcpy(&f, &b, 4); *out = f; return true; }
  if (ai == 27) { uint64_t b = v; double d; memcpy(&d, &b, 8); *out = d; return true; }
  if (ai == 25) {  // half float
    uint16_t h = (uint16_t)v;
    int s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    double f;
    if (e == 0) f = (m / 1024.0) * 0.00006103515625;
    else if (e == 31) f = (m == 0) ? INFINITY : NAN;
    else f = (1 + m / 1024.0) * pow(2.0, e - 15);
    *out = s ? -f : f;
    return true;
  }
  return false;
}

// Copia una stringa di testo CBOR (lunghezza definita) in dst, troncandola.
bool copyText(char *dst, int cap, Cur *c, uint64_t len) {
  if (len > (uint64_t)(c->n - c->i)) { c->err = true; return false; }
  int n = (int)len < cap - 1 ? (int)len : cap - 1;
  memcpy(dst, c->b + c->i, n);
  dst[n] = 0;
  c->i += (int)len;
  return true;
}

typedef void (*EntryFn)(Cur *c, void *ctx, uint64_t key, int vmt, int vai, uint64_t v, bool vindef);

// Legge una mappa con chiavi intere. La callback deve consumare il corpo del
// valore, o chiamare skipBody. Ritorna false se il primo item non e' una mappa o
// se i dati sono malformati.
bool readMap(Cur *c, EntryFn fn, void *ctx) {
  int mt, ai; bool indef;
  uint64_t n = head(c, &mt, &ai, &indef);
  if (c->err || mt != 5) return false;
  for (uint64_t k = 0;; k++) {
    if (indef) {
      if (!need(c, 1)) break;
      if (atBreak(c)) { c->i++; break; }
    } else if (k >= n) {
      break;
    }
    int kmt, kai; bool kind;
    uint64_t key = head(c, &kmt, &kai, &kind);
    if (c->err) break;
    if (kmt != 0) {           // chiave non intera: salta chiave e valore
      skipBody(c, kmt, key, kind, 1);
      skipItem(c, 1);
      if (c->err) break;
      continue;
    }
    int vmt, vai; bool vind;
    uint64_t v = head(c, &vmt, &vai, &vind);
    if (c->err) break;
    fn(c, ctx, key, vmt, vai, v, vind);
    if (c->err) break;
  }
  return !c->err;
}

// ───────────────────────── sezione meta ─────────────────────────
struct Meta {
  int64_t mainOffset = -1, mainSize = -1, auxOffset = -1;
  bool notMeta = false;   // chiavi o valori che una sezione meta non puo' avere
};

void metaEntry(Cur *c, void *ctx, uint64_t key, int vmt, int vai, uint64_t v, bool vind) {
  Meta *m = (Meta *)ctx;
  (void)vai;
  if (key > 3 || vmt != 0 || v > 0x7fffffff) {
    m->notMeta = true;
    skipBody(c, vmt, v, vind, 1);
    return;
  }
  if (key == 0) m->mainOffset = (int64_t)v;
  else if (key == 1) m->mainSize = (int64_t)v;
  else if (key == 2) m->auxOffset = (int64_t)v;
}

// ───────────────────────── sezione principale ─────────────────────────
void mainEntry(Cur *c, void *ctx, uint64_t key, int vmt, int vai, uint64_t v, bool vind) {
  SpoolData *s = (SpoolData *)ctx;
  double d;
  int k = key < 1000 ? (int)key : -1;

  if (vmt == 3 && !vind) {
    if (k == 10) { copyText(s->material, sizeof(s->material), c, v); return; }  // material_name
    if (k == 11) { copyText(s->brand, sizeof(s->brand), c, v); return; }        // brand_name
    if (k == 55) { copyText(s->country, sizeof(s->country), c, v); return; }    // country_of_origin
  }
  if (k == 19 && vmt == 2 && !vind && v >= 3) {                                 // primary_color
    if (need(c, 3)) {
      s->color[0] = c->b[c->i]; s->color[1] = c->b[c->i + 1]; s->color[2] = c->b[c->i + 2];
      s->hasColor = true;
    }
  } else if (numberValue(vmt, vai, v, &d)) {
    switch (k) {
      case 29: s->density = (float)d; break;        // density
      case 30: s->diameter = (float)d; break;       // filament_diameter
      case 34: s->nozzleMin = (int)d; break;        // min_print_temperature
      case 35: s->nozzleMax = (int)d; break;        // max_print_temperature
      case 37: s->bedMin = (int)d; break;           // min_bed_temperature
      case 38: s->bedMax = (int)d; break;           // max_bed_temperature
      case 16: s->weight = (int)d; break;           // nominal_netto_full_weight
      default: break;
    }
    return;
  }
  skipBody(c, vmt, v, vind, 1);
}

// ───────────────────────── NDEF / TLV ─────────────────────────

// Cerca nel messaggio NDEF il record MIME OpenPrintTag. Se non c'e' usa il payload
// del primo record (comportamento delle versioni precedenti).
bool scanNdef(const uint8_t *m, int n, const uint8_t **pay, int *plen, bool *strict) {
  const uint8_t *firstPay = nullptr;
  int firstLen = 0;
  int i = 0;
  while (i < n) {
    uint8_t h = m[i++];
    bool sr = h & 0x10, il = h & 0x08, me = h & 0x40;
    int tnf = h & 0x07;
    if (i >= n) break;
    int tlen = m[i++];
    uint32_t pl;
    if (sr) {
      if (i >= n) break;
      pl = m[i++];
    } else {
      if (i + 4 > n) break;
      pl = ((uint32_t)m[i] << 24) | ((uint32_t)m[i + 1] << 16) | ((uint32_t)m[i + 2] << 8) | m[i + 3];
      i += 4;
    }
    int idlen = 0;
    if (il) {
      if (i >= n) break;
      idlen = m[i++];
    }
    if (i + tlen + idlen > n) break;
    const uint8_t *type = m + i;
    i += tlen + idlen;
    int avail = n - i;
    int pn = pl > (uint32_t)avail ? avail : (int)pl;
    if (!firstPay) { firstPay = m + i; firstLen = pn; }
    if (tnf == 2 && tlen == MIME_LEN && memcmp(type, MIME, MIME_LEN) == 0) {
      *pay = m + i; *plen = pn; *strict = true;
      return true;
    }
    i += pn;
    if (me) break;
  }
  if (firstPay && firstLen > 0) { *pay = firstPay; *plen = firstLen; *strict = false; return true; }
  return false;
}

bool findPayload(const uint8_t *d, int n, const uint8_t **pay, int *plen, bool *strict) {
  int i = 0;
  if (n > 0 && d[0] == 0xE1) i = 4;        // Capability Container a 4 byte
  else if (n > 0 && d[0] == 0xE2) i = 8;   // Capability Container a 8 byte
  while (i < n) {
    uint8_t t = d[i++];
    if (t == 0x00) continue;               // padding
    if (t == 0xFE) break;                  // terminatore
    if (i >= n) break;
    int len = d[i++];
    if (len == 0xFF) {
      if (i + 2 > n) break;
      len = (d[i] << 8) | d[i + 1];
      i += 2;
    }
    int avail = n - i;
    int tl = len > avail ? avail : len;
    if (t == 0x03 && scanNdef(d + i, tl, pay, plen, strict)) return true;
    i += len;
  }
  return false;
}

}  // namespace

bool optParse(const uint8_t *mem, int len, SpoolData *s) {
  if (!mem || len <= 0 || !s) return false;
  const uint8_t *pay = nullptr;
  int plen = 0;
  bool strict = false;
  if (!findPayload(mem, len, &pay, &plen, &strict)) return false;
  s->optTag = strict;

  // Sezione meta: prima mappa del payload.
  Cur mc = {pay, plen, 0, false};
  Meta meta;
  bool metaOk = readMap(&mc, metaEntry, &meta) && !meta.notMeta;

  int mainOff = 0;
  int mainEnd = plen;
  if (metaOk) {
    int64_t off = meta.mainOffset >= 0 ? meta.mainOffset : mc.i;
    if (off >= plen) return false;
    mainOff = (int)off;
    int64_t end = plen;
    if (meta.mainSize >= 0) end = off + meta.mainSize;
    else if (meta.auxOffset > off) end = meta.auxOffset;
    mainEnd = end > plen ? plen : (int)end;
  }                                         // altrimenti: tag senza meta, la mappa iniziale e' la principale
  if (mainOff < 0 || mainOff >= plen) return false;
  if (mainEnd > plen || mainEnd < mainOff) mainEnd = plen;

  Cur c = {pay + mainOff, mainEnd - mainOff, 0, false};
  readMap(&c, mainEntry, s);
  return true;
}
