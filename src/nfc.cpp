#include "nfc.h"
#include "utils.h"
#include <PN5180.h>
#include <PN5180ISO14443.h>
#include <PN5180ISO15693.h>

static PN5180ISO14443 nfc14443(PN5180_NSS, PN5180_BUSY, PN5180_RST);
static PN5180ISO15693 nfc15693(PN5180_NSS, PN5180_BUSY, PN5180_RST);
NfcData nfcData;

static constexpr uint8_t OPENPRINTTAG_MIME_TYPE[] = {
  'a','p','p','l','i','c','a','t','i','o','n','/',
  'v','n','d','.','o','p','e','n','p','r','i','n','t','t','a','g'
};

static void resetNfcRf()      { nfc14443.reset(); nfc14443.setupRF(); }
static void resetNfc15693Rf() { nfc15693.reset(); nfc15693.setupRF(); }

// ── Minimal CBOR parser for OpenPrintTag ─────────────────────────────────────

static uint32_t cborReadArg(const uint8_t *p, uint16_t &pos, uint16_t end) {
  if (pos >= end) return 0;
  uint8_t info = p[pos++] & 0x1F;
  if (info < 24) return info;
  if (info == 24 && pos < end) return p[pos++];
  if (info == 25 && pos + 1 < end) { uint32_t v = ((uint32_t)p[pos]<<8)|p[pos+1]; pos+=2; return v; }
  if (info == 26 && pos + 3 < end) { uint32_t v = ((uint32_t)p[pos]<<24)|((uint32_t)p[pos+1]<<16)|((uint32_t)p[pos+2]<<8)|p[pos+3]; pos+=4; return v; }
  return 0;
}

static String cborReadText(const uint8_t *p, uint16_t &pos, uint16_t end) {
  uint32_t len = cborReadArg(p, pos, end);
  if (pos + len > end) return "";
  String s = String((const char*)(p + pos), len);
  pos += len; return s;
}

static float cborReadFloat32(const uint8_t *p, uint16_t &pos, uint16_t end) {
  pos++; // skip 0xFA
  if (pos + 3 >= end) return 0.0f;
  union { uint32_t u; float f; } c;
  c.u = ((uint32_t)p[pos]<<24)|((uint32_t)p[pos+1]<<16)|((uint32_t)p[pos+2]<<8)|p[pos+3];
  pos += 4; return c.f;
}

static void cborSkipValue(const uint8_t *p, uint16_t &pos, uint16_t end) {
  if (pos >= end) return;
  uint8_t b = p[pos];
  uint8_t maj = (b >> 5) & 0x7;
  if (maj == 7 && (b & 0x1F) == 26) { pos += 5; return; } // float32
  uint32_t len = cborReadArg(p, pos, end);
  if (maj == 2 || maj == 3) pos += len; // bytes/text: skip payload
}

static bool parseOpenPrintTag(const uint8_t *memory, uint16_t memLen) {
  const uint16_t mimeLength = sizeof(OPENPRINTTAG_MIME_TYPE);
  if (memLen <= mimeLength + 4) return false;

  uint16_t payloadStart = 0; bool found = false;
  for (uint16_t i = 0; i + mimeLength < memLen; i++) {
    if (memcmp(&memory[i], OPENPRINTTAG_MIME_TYPE, mimeLength) == 0) {
      payloadStart = i + mimeLength; found = true; break;
    }
  }
  if (!found) return false;

  const uint8_t *p = memory + payloadStart;
  uint16_t end = memLen - payloadStart;
  uint16_t pos = 0;

  // Skip 0xA0 (empty map header), then expect 0xB8 <count>
  if (pos >= end || p[pos++] != 0xA0) return false;
  if (pos + 1 >= end || p[pos++] != 0xB8) return false;
  uint8_t count = p[pos++];

  nfcData.optParsed = false;
  for (uint8_t i = 0; i < count && pos < end; i++) {
    if ((p[pos] >> 5) != 0) break; // key must be uint
    uint16_t kp = pos;
    uint32_t key = cborReadArg(p, kp, end); pos = kp;
    if (pos >= end) break;
    uint8_t valMaj = (p[pos] >> 5) & 0x7;

    if (valMaj == 0) { // unsigned int
      uint32_t val = cborReadArg(p, pos, end);
      switch (key) {
        case 9:  nfcData.optMatType   = (uint8_t)val;  break;
        case 14: nfcData.optMdate     = (uint16_t)val; break;
        case 16: nfcData.optNomWeight = (uint16_t)val; break;
        case 17: nfcData.optActWeight = (uint16_t)val; break;
        case 18: nfcData.optEmptyWeight=(uint16_t)val; break;
        case 34: nfcData.optNozzleMin = (uint16_t)val; break;
        case 35: nfcData.optNozzleMax = (uint16_t)val; break;
        case 37: nfcData.optBedMin    = (uint16_t)val; break;
        case 38: nfcData.optBedMax    = (uint16_t)val; break;
        case 42: nfcData.optSpoolW    = (uint16_t)val; break;
        case 43: nfcData.optSpoolOd   = (uint16_t)val; break;
        case 44: nfcData.optSpoolId   = (uint16_t)val; break;
        case 45: nfcData.optSpoolHole = (uint16_t)val; break;
        case 53: nfcData.optNomLength = val;            break;
        case 54: nfcData.optActLength = val;            break;
        case 57: nfcData.optDryTemp   = (uint16_t)val; break;
        case 58: nfcData.optDryTime   = (uint16_t)val; break;
      }
    } else if (valMaj == 3) { // text
      String s = cborReadText(p, pos, end);
      switch (key) {
        case 10: nfcData.optMaterial = s; break;
        case 11: nfcData.optBrand    = s; break;
        case 55: nfcData.optCountry  = s; break;
      }
    } else if (valMaj == 2) { // bytes
      uint32_t len = cborReadArg(p, pos, end);
      if (key == 19 && len >= 3 && pos + (int)len <= end) {
        nfcData.openPrintTagColor = "#";
        for (uint8_t c = 0; c < 3; c++) {
          if (p[pos+c] < 0x10) nfcData.openPrintTagColor += "0";
          String h = String(p[pos+c], HEX); h.toUpperCase();
          nfcData.openPrintTagColor += h;
        }
      }
      pos += len;
    } else if (valMaj == 7 && p[pos] == 0xFA) { // float32
      float f = cborReadFloat32(p, pos, end);
      switch (key) {
        case 29: nfcData.optDensity  = f; break;
        case 30: nfcData.optDiameter = f; break;
      }
    } else {
      cborSkipValue(p, pos, end);
    }
  }
  nfcData.optParsed = true;
  return true;
}

static void setNfcTagData(const String &uidText, uint8_t uidLength) {
  nfcData.lastStatus  = "Tag ISO14443A letto";
  nfcData.lastUid     = uidText;
  nfcData.lastUidLength = uidLength;
  nfcData.memoryDump  = "Dump non disponibile per ISO14443A";
  nfcData.tagType = "ISO 14443A"; nfcData.technologies = "NfcA";
  nfcData.atqa = "--"; nfcData.sak = "--";
  nfcData.memoryInfo = "Da identificare"; nfcData.dataFormat = "Da identificare";
}

static bool readIso15693Tag() {
  resetNfc15693Rf();
  uint8_t uid[8] = {0};
  ISO15693ErrorCode rc = EC_NO_CARD;
  for (uint8_t attempt = 1; attempt <= NFC_READ_ATTEMPTS; attempt++) {
    rc = nfc15693.getInventory(uid);
    if (rc == ISO15693_EC_OK) break;
    resetNfc15693Rf(); delay(NFC_RETRY_DELAY_MS);
  }
  if (rc != ISO15693_EC_OK) return false;

  const String uidText = uidToStringReversed(uid, sizeof(uid));
  nfcData.lastStatus = "Tag ISO15693 letto"; nfcData.lastUid = uidText;
  nfcData.lastUidLength = sizeof(uid); nfcData.tagType = "ISO 15693 / NXP ICODE";
  nfcData.technologies = "NfcV, NdefFormatable"; nfcData.atqa = "DSFID 0x00";
  nfcData.sak = "--"; nfcData.memoryInfo = "Da identificare"; nfcData.dataFormat = "NFC Forum Type 5";

  uint8_t blockSize = 0, numBlocks = 0, systemUid[8] = {0};
  memcpy(systemUid, uid, sizeof(uid));
  rc = nfc15693.getSystemInfo(systemUid, &blockSize, &numBlocks);
  if (rc == ISO15693_EC_OK && blockSize > 0 && numBlocks > 0)
    nfcData.memoryInfo = String((uint16_t)blockSize * numBlocks) + " bytes, " + String(numBlocks) + " blocchi da " + String(blockSize) + " bytes";

  nfcData.memoryDump = "";
  if (blockSize > 0) {
    const uint8_t blocksToRead = min<uint8_t>(numBlocks, 32);
    uint8_t tagMemory[128] = {0}; uint16_t tagMemoryLength = 0;
    uint8_t blockData[32] = {0};
    for (uint8_t blockNo = 0; blockNo < blocksToRead; blockNo++) {
      memset(blockData, 0, sizeof(blockData));
      rc = nfc15693.readSingleBlock(uid, blockNo, blockData, blockSize);
      if (rc != ISO15693_EC_OK) continue;
      if (tagMemoryLength + blockSize <= sizeof(tagMemory)) {
        memcpy(&tagMemory[tagMemoryLength], blockData, blockSize); tagMemoryLength += blockSize;
      }
    }
    if (!parseOpenPrintTag(tagMemory, tagMemoryLength)) {
      nfcData.openPrintTagColor = "#ffffff";
    }
  } else {
    nfcData.memoryDump = "Memoria non identificata"; nfcData.openPrintTagColor = "#ffffff";
  }

  return true;
}

void setupNfcReader() {
  if (nfcData.initialized) return;
  Serial.println("Inizializzo PN5180...");
  nfc14443.begin(); nfc15693.begin(); nfc14443.reset();
  nfcData.initialized = true;

  uint8_t pv[2]={0xff,0xff}, fv[2]={0xff,0xff}, ev[2]={0xff,0xff};
  nfc14443.readEEprom(PRODUCT_VERSION,  pv, sizeof(pv));
  nfc14443.readEEprom(FIRMWARE_VERSION, fv, sizeof(fv));
  nfc14443.readEEprom(EEPROM_VERSION,   ev, sizeof(ev));
  nfcData.productVersion  = bytesToVersion(pv);
  nfcData.firmwareVersion = bytesToVersion(fv);
  nfcData.eepromVersion   = bytesToVersion(ev);
  nfcData.readerPresent   = pv[0] != 0xff || pv[1] != 0xff;

  if (!nfcData.readerPresent) { nfcData.lastStatus = "PN5180 non rilevato"; return; }
  nfcData.rfReady   = nfc14443.setupRF();
  nfcData.lastStatus = nfcData.rfReady ? "PN5180 pronto" : "Errore RF PN5180";
  Serial.println(nfcData.lastStatus);
}

void readNfcTag() {
  if (!nfcData.initialized) setupNfcReader();
  if (!nfcData.readerPresent) { nfcData.lastStatus = "PN5180 non pronto"; return; }

  if (readIso15693Tag()) return;

  resetNfcRf();
  uint8_t uid[10]={0}; uint8_t uidLength = 0;
  for (uint8_t attempt = 1; attempt <= NFC_READ_ATTEMPTS && uidLength < 4; attempt++) {
    uint8_t response[10]={0};
    uidLength = nfc14443.activateTypeA(response, 1);
    if (uidLength == 0) {
      resetNfcRf(); memset(response, 0, sizeof(response));
      uidLength = nfc14443.activateTypeA(response, 0);
    }
    if (uidLength >= 4) { for (uint8_t i = 0; i < uidLength; i++) uid[i] = response[3+i]; break; }
    resetNfcRf(); delay(NFC_RETRY_DELAY_MS);
  }

  if (uidLength >= 4) { setNfcTagData(uidToString(uid, uidLength), uidLength); }
  else { clearNfcReadState(); }
  resetNfcRf();
}

void clearNfcReadState() {
  nfcData.lastUid = "--"; nfcData.lastUidLength = 0;
  nfcData.tagType = "--"; nfcData.technologies = "--";
  nfcData.atqa = "--"; nfcData.sak = "--";
  nfcData.memoryInfo = "--"; nfcData.dataFormat = "--";
  nfcData.memoryDump = "--"; nfcData.openPrintTagColor = "#ffffff";
  nfcData.optParsed = false; nfcData.optBrand = ""; nfcData.optMaterial = "";
  nfcData.optMatType = 0; nfcData.optDryTemp = 0; nfcData.optDryTime = 0;
  nfcData.optNozzleMin = 0; nfcData.optNozzleMax = 0;
  nfcData.optBedMin = 0; nfcData.optBedMax = 0;
  nfcData.optDensity = 0.0f; nfcData.optDiameter = 0.0f;
  nfcData.optNomWeight = 0; nfcData.optActWeight = 0; nfcData.optEmptyWeight = 0;
  nfcData.optNomLength = 0; nfcData.optActLength = 0;
  nfcData.optSpoolW = 0; nfcData.optSpoolOd = 0; nfcData.optSpoolId = 0; nfcData.optSpoolHole = 0;
  nfcData.optCountry = "";
  nfcData.lastStatus = "Nessun tag";
}

void printNfcData() {
  Serial.print("UID: "); Serial.print(nfcData.lastUid);
  Serial.print(" | tipo: "); Serial.print(nfcData.tagType);
  Serial.print(" | stato: "); Serial.println(nfcData.lastStatus);
  if (!nfcData.optParsed) return;
  Serial.println("--- OpenPrintTag ---");
  Serial.print("Brand: ");    Serial.println(nfcData.optBrand);
  Serial.print("Materiale: ");Serial.println(nfcData.optMaterial);
  Serial.print("Colore: ");   Serial.println(nfcData.openPrintTagColor);
  Serial.print("Diametro: "); Serial.print(nfcData.optDiameter, 2); Serial.println(" mm");
  Serial.print("Densita: ");  Serial.print(nfcData.optDensity, 3); Serial.println(" g/cm3");
  Serial.print("Temp ugello: "); Serial.print(nfcData.optNozzleMin); Serial.print("-"); Serial.print(nfcData.optNozzleMax); Serial.println(" C");
  Serial.print("Temp piatto: "); Serial.print(nfcData.optBedMin); Serial.print("-"); Serial.print(nfcData.optBedMax); Serial.println(" C");
  Serial.print("Peso nominale: "); Serial.print(nfcData.optNomWeight); Serial.println(" g");
  Serial.print("Peso attuale: ");  Serial.print(nfcData.optActWeight); Serial.println(" g");
  Serial.print("Paese: "); Serial.println(nfcData.optCountry);
  Serial.println("--------------------");
}
