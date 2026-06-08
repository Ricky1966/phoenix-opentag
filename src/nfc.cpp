#include "nfc.h"
#include "utils.h"
#include <PN5180.h>
#include <PN5180ISO14443.h>
#include <PN5180ISO15693.h>

static PN5180ISO14443 nfc14443(PN5180_NSS, PN5180_BUSY, PN5180_RST);
static PN5180ISO15693 nfc15693(PN5180_NSS, PN5180_BUSY, PN5180_RST);
DryerSlots dryerSlots;
NfcData nfcData;

static constexpr uint8_t OPENPRINTTAG_MIME_TYPE[] = {
  'a','p','p','l','i','c','a','t','i','o','n','/',
  'v','n','d','.','o','p','e','n','p','r','i','n','t','t','a','g'
};

static void resetNfcRf()      { nfc14443.reset(); nfc14443.setupRF(); }
static void resetNfc15693Rf() { nfc15693.reset(); nfc15693.setupRF(); }

static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

static uint16_t appendCborUInt(uint8_t *buf, uint16_t pos, uint32_t v) {
  if      (v < 24)     { buf[pos++] = v; }
  else if (v <= 0xff)  { buf[pos++] = 0x18; buf[pos++] = v; }
  else if (v <= 0xffff){ buf[pos++] = 0x19; buf[pos++] = v >> 8; buf[pos++] = v & 0xff; }
  else { buf[pos++]=0x1A; buf[pos++]=(v>>24)&0xff; buf[pos++]=(v>>16)&0xff; buf[pos++]=(v>>8)&0xff; buf[pos++]=v&0xff; }
  return pos;
}

static uint16_t appendCborFloat32(uint8_t *buf, uint16_t pos, float value) {
  union { float f; uint32_t u; } d; d.f = value;
  buf[pos++]=0xFA; buf[pos++]=(d.u>>24)&0xff; buf[pos++]=(d.u>>16)&0xff;
  buf[pos++]=(d.u>>8)&0xff; buf[pos++]=d.u&0xff;
  return pos;
}

static uint16_t appendCborText(uint8_t *buf, uint16_t pos, String value) {
  value.trim();
  if (value.length() > 31) value = value.substring(0, 31);
  const uint8_t len = value.length();
  if (len < 24) buf[pos++] = 0x60 | len; else { buf[pos++]=0x78; buf[pos++]=len; }
  for (uint8_t i = 0; i < len; i++) buf[pos++] = value[i];
  return pos;
}

static uint16_t appendCborColor(uint8_t *buf, uint16_t pos, String color) {
  color.trim(); color.replace("#","");
  if (color.length() != 6 && color.length() != 8) color = "FFFFFF";
  const uint8_t bytes = color.length() == 8 ? 4 : 3;
  buf[pos++] = 0x40 | bytes;
  for (uint8_t i = 0; i < bytes; i++)
    buf[pos++] = (hexNibble(color[i*2]) << 4) | hexNibble(color[i*2+1]);
  return pos;
}

static uint16_t buildOpenPrintTagPayload(uint8_t *payload, uint16_t payloadSize,
    const String &brandName, const String &materialName, uint8_t materialType,
    uint16_t minPrintTemp, uint16_t maxPrintTemp, uint16_t minBedTemp, uint16_t maxBedTemp,
    uint16_t dryingTemp, uint16_t dryingTime, const String &primaryColor,
    uint16_t nominalWeight, uint16_t actualWeight, uint16_t emptyWeight,
    float density, float filamentDiameter, uint32_t nominalLength, uint32_t actualLength,
    uint32_t manufacturedDate, const String &country,
    uint16_t spoolWidth, uint16_t spoolOuterDiameter, uint16_t spoolInnerDiameter, uint16_t spoolHoleDiameter) {
  uint16_t pos = 0;
  payload[pos++] = 0xA0; payload[pos++] = 0xB8; payload[pos++] = 0x18;
  pos = appendCborUInt(payload, pos, 8);  pos = appendCborUInt(payload, pos, 0);
  pos = appendCborUInt(payload, pos, 9);  pos = appendCborUInt(payload, pos, materialType);
  pos = appendCborUInt(payload, pos, 10); pos = appendCborText(payload, pos, materialName);
  pos = appendCborUInt(payload, pos, 11); pos = appendCborText(payload, pos, brandName);
  pos = appendCborUInt(payload, pos, 14); pos = appendCborUInt(payload, pos, manufacturedDate);
  pos = appendCborUInt(payload, pos, 16); pos = appendCborUInt(payload, pos, nominalWeight);
  pos = appendCborUInt(payload, pos, 17); pos = appendCborUInt(payload, pos, actualWeight);
  pos = appendCborUInt(payload, pos, 18); pos = appendCborUInt(payload, pos, emptyWeight);
  pos = appendCborUInt(payload, pos, 19); pos = appendCborColor(payload, pos, primaryColor);
  pos = appendCborUInt(payload, pos, 29); pos = appendCborFloat32(payload, pos, density);
  pos = appendCborUInt(payload, pos, 30); pos = appendCborFloat32(payload, pos, filamentDiameter);
  pos = appendCborUInt(payload, pos, 34); pos = appendCborUInt(payload, pos, minPrintTemp);
  pos = appendCborUInt(payload, pos, 35); pos = appendCborUInt(payload, pos, maxPrintTemp);
  pos = appendCborUInt(payload, pos, 37); pos = appendCborUInt(payload, pos, minBedTemp);
  pos = appendCborUInt(payload, pos, 38); pos = appendCborUInt(payload, pos, maxBedTemp);
  pos = appendCborUInt(payload, pos, 42); pos = appendCborUInt(payload, pos, spoolWidth);
  pos = appendCborUInt(payload, pos, 43); pos = appendCborUInt(payload, pos, spoolOuterDiameter);
  pos = appendCborUInt(payload, pos, 44); pos = appendCborUInt(payload, pos, spoolInnerDiameter);
  pos = appendCborUInt(payload, pos, 45); pos = appendCborUInt(payload, pos, spoolHoleDiameter);
  pos = appendCborUInt(payload, pos, 53); pos = appendCborUInt(payload, pos, nominalLength);
  pos = appendCborUInt(payload, pos, 54); pos = appendCborUInt(payload, pos, actualLength);
  pos = appendCborUInt(payload, pos, 55); pos = appendCborText(payload, pos, country);
  pos = appendCborUInt(payload, pos, 57); pos = appendCborUInt(payload, pos, dryingTemp);
  pos = appendCborUInt(payload, pos, 58); pos = appendCborUInt(payload, pos, dryingTime);
  return min<uint16_t>(pos, payloadSize);
}

static uint16_t buildOpenPrintTagMemory(uint8_t *memory, uint16_t memorySize,
    const String &brandName, const String &materialName, uint8_t materialType,
    uint16_t minPrintTemp, uint16_t maxPrintTemp, uint16_t minBedTemp, uint16_t maxBedTemp,
    uint16_t dryingTemp, uint16_t dryingTime, const String &primaryColor,
    uint16_t nominalWeight, uint16_t actualWeight, uint16_t emptyWeight,
    float density, float filamentDiameter, uint32_t nominalLength, uint32_t actualLength,
    uint32_t manufacturedDate, const String &country,
    uint16_t spoolWidth, uint16_t spoolOuterDiameter, uint16_t spoolInnerDiameter, uint16_t spoolHoleDiameter) {
  memset(memory, 0, memorySize);
  uint8_t payload[220] = {0};
  const uint16_t payloadLength = buildOpenPrintTagPayload(payload, sizeof(payload),
    brandName, materialName, materialType, minPrintTemp, maxPrintTemp, minBedTemp, maxBedTemp,
    dryingTemp, dryingTime, primaryColor, nominalWeight, actualWeight, emptyWeight,
    density, filamentDiameter, nominalLength, actualLength, manufacturedDate, country,
    spoolWidth, spoolOuterDiameter, spoolInnerDiameter, spoolHoleDiameter);

  memory[0]=0xE1; memory[1]=0x40; memory[2]=0x27; memory[3]=0x01;
  uint16_t pos = 4;
  const uint8_t ndefLength = 3 + sizeof(OPENPRINTTAG_MIME_TYPE) + payloadLength;
  memory[pos++]=0x03; memory[pos++]=ndefLength; memory[pos++]=0xD2;
  memory[pos++]=sizeof(OPENPRINTTAG_MIME_TYPE); memory[pos++]=payloadLength;
  memcpy(&memory[pos], OPENPRINTTAG_MIME_TYPE, sizeof(OPENPRINTTAG_MIME_TYPE)); pos += sizeof(OPENPRINTTAG_MIME_TYPE);
  memcpy(&memory[pos], payload, payloadLength); pos += payloadLength;
  memory[pos++] = 0xFE;
  return pos;
}

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
  if (uidText == "04:DE:7C:52:F2:6C:80") {
    nfcData.tagType    = "ISO 14443-3A / NXP NTAG215";
    nfcData.technologies = "NfcA, MifareUltralight, Ndef";
    nfcData.atqa = "0x0044"; nfcData.sak = "0x00";
    nfcData.memoryInfo = "540 bytes, 135 pagine da 4 bytes";
    nfcData.dataFormat = "NFC Forum Type 2";
  } else {
    nfcData.tagType = "ISO 14443A"; nfcData.technologies = "NfcA";
    nfcData.atqa = "--"; nfcData.sak = "--";
    nfcData.memoryInfo = "Da identificare"; nfcData.dataFormat = "Da identificare";
  }
}

static bool readIso15693Tag() {
  resetNfc15693Rf();
  uint8_t uid[8] = {0};
  ISO15693ErrorCode rc = EC_NO_CARD;
  for (uint8_t attempt = 1; attempt <= NFC_READ_ATTEMPTS; attempt++) {
    Serial.print("Tentativo ISO15693 "); Serial.print(attempt); Serial.print("/"); Serial.println(NFC_READ_ATTEMPTS);
    rc = nfc15693.getInventory(uid);
    if (rc == ISO15693_EC_OK) break;
    resetNfc15693Rf(); delay(NFC_RETRY_DELAY_MS);
  }
  if (rc != ISO15693_EC_OK) { Serial.print("Nessun tag ISO15693, errore "); Serial.println(rc); return false; }

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
      if (blockNo > 0) nfcData.memoryDump += "<br>";
      String blockLabel = String(blockNo, HEX); blockLabel.toUpperCase();
      if (blockNo < 0x10) nfcData.memoryDump += "0";
      nfcData.memoryDump += blockLabel + ": ";
      if (rc != ISO15693_EC_OK) { nfcData.memoryDump += "errore lettura"; continue; }
      if (tagMemoryLength + blockSize <= sizeof(tagMemory)) {
        memcpy(&tagMemory[tagMemoryLength], blockData, blockSize); tagMemoryLength += blockSize;
      }
      for (uint8_t i = 0; i < blockSize; i++) {
        if (i > 0) nfcData.memoryDump += " ";
        nfcData.memoryDump += hexByte(blockData[i]);
      }
      nfcData.memoryDump += " | ";
      for (uint8_t i = 0; i < blockSize; i++) nfcData.memoryDump += printableAscii(blockData[i]);
    }
    if (parseOpenPrintTag(tagMemory, tagMemoryLength)) {
      Serial.print("OpenPrintTag parsed: "); Serial.print(nfcData.optBrand);
      Serial.print(" / "); Serial.println(nfcData.optMaterial);
    } else { nfcData.openPrintTagColor = "#ffffff"; }
  } else { nfcData.memoryDump = "Memoria non identificata"; nfcData.openPrintTagColor = "#ffffff"; }

  Serial.print("Tag ISO15693 UID: "); Serial.println(nfcData.lastUid);
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
}

void readNfcTag() {
  if (!nfcData.initialized) setupNfcReader();
  if (!nfcData.readerPresent) { nfcData.lastStatus = "PN5180 non pronto"; return; }

  nfcData.lastStatus = "Lettura in corso";
  if (readIso15693Tag()) {
    int8_t freeSlot = findFreeSlot();
    if (freeSlot >= 0) {
      dryerSlots.bobina[freeSlot] = nfcData;
      dryerSlots.slotOccupied[freeSlot] = true;
      dryerSlots.activeSlotCount++;
      if (nfcData.optParsed) {
        dryerSlots.dryingTemp[freeSlot] = nfcData.optDryTemp > 0 ? nfcData.optDryTemp : 45;
        dryerSlots.dryingMinutes[freeSlot] = nfcData.optDryTime > 0 ? nfcData.optDryTime : 360;
      }
      Serial.print("Tag assegnato allo slot "); Serial.println(freeSlot);
    }
    return;
  }

  resetNfcRf();
  uint8_t uid[10]={0}; uint8_t uidLength = 0;
  for (uint8_t attempt = 1; attempt <= NFC_READ_ATTEMPTS && uidLength < 4; attempt++) {
    uint8_t response[10]={0};
    Serial.print("Tentativo NFC "); Serial.print(attempt); Serial.print("/"); Serial.println(NFC_READ_ATTEMPTS);
    uidLength = nfc14443.activateTypeA(response, 1);
    printHexBuffer("activateTypeA response: ", response, sizeof(response));
    if (uidLength == 0) {
      resetNfcRf(); memset(response, 0, sizeof(response));
      uidLength = nfc14443.activateTypeA(response, 0);
    }
    if (uidLength >= 4) { for (uint8_t i = 0; i < uidLength; i++) uid[i] = response[3+i]; break; }
    resetNfcRf(); delay(NFC_RETRY_DELAY_MS);
  }

  if (uidLength >= 4) { setNfcTagData(uidToString(uid, uidLength), uidLength); }
  else { clearNfcReadState(); nfcData.lastStatus = "Nessun tag ISO14443A"; }
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
  nfcData.lastStatus = "Pronto: appoggia il tag e premi Leggi tag";
}

bool writeOpenPrintTag(uint8_t materialType, const String &brandName, const String &materialName,
    uint16_t minPrintTemp, uint16_t maxPrintTemp, uint16_t minBedTemp, uint16_t maxBedTemp,
    uint16_t dryingTemp, uint16_t dryingTime, const String &primaryColor,
    uint16_t nominalWeight, uint16_t actualWeight, uint16_t emptyWeight,
    float density, float filamentDiameter, uint32_t nominalLength, uint32_t actualLength,
    uint32_t manufacturedDate, const String &country,
    uint16_t spoolWidth, uint16_t spoolOuterDiameter, uint16_t spoolInnerDiameter, uint16_t spoolHoleDiameter) {
  resetNfc15693Rf();
  uint8_t uid[8]={0}; ISO15693ErrorCode rc = EC_NO_CARD;
  for (uint8_t attempt = 1; attempt <= NFC_READ_ATTEMPTS; attempt++) {
    rc = nfc15693.getInventory(uid);
    if (rc == ISO15693_EC_OK) break;
    resetNfc15693Rf(); delay(NFC_RETRY_DELAY_MS);
  }
  if (rc != ISO15693_EC_OK) { clearNfcReadState(); nfcData.lastStatus = "Scrittura annullata: nessun tag ISO15693"; return false; }

  uint8_t memory[256]={0};
  const uint16_t usedBytes = buildOpenPrintTagMemory(memory, sizeof(memory), brandName, materialName, materialType,
    minPrintTemp, maxPrintTemp, minBedTemp, maxBedTemp, dryingTemp, dryingTime, primaryColor,
    nominalWeight, actualWeight, emptyWeight, density, filamentDiameter, nominalLength, actualLength,
    manufacturedDate, country, spoolWidth, spoolOuterDiameter, spoolInnerDiameter, spoolHoleDiameter);
  const uint8_t blocksToWrite = (usedBytes + 3) / 4;
  for (uint8_t blockNo = 0; blockNo < blocksToWrite; blockNo++) {
    rc = nfc15693.writeSingleBlock(uid, blockNo, &memory[blockNo*4], 4);
    if (rc != ISO15693_EC_OK) { nfcData.lastStatus = "Errore scrittura blocco " + String(blockNo); return false; }
    delay(20);
  }
  nfcData.lastStatus = "OpenPrintTag scritto";
  readIso15693Tag();
  return true;
}

String nfcJson() {
  String json = "{\"readerPresent\":"; json += nfcData.readerPresent ? "true" : "false";
  json += ",\"rfReady\":";          json += nfcData.rfReady ? "true" : "false";
  json += ",\"productVersion\":\""; json += nfcData.productVersion;
  json += "\",\"firmwareVersion\":\""; json += nfcData.firmwareVersion;
  json += "\",\"eepromVersion\":\"";   json += nfcData.eepromVersion;
  json += "\",\"lastStatus\":\"";      json += nfcData.lastStatus;
  json += "\",\"lastUid\":\"";         json += nfcData.lastUid;
  json += "\",\"lastUidLength\":";     json += String(nfcData.lastUidLength);
  json += ",\"tagType\":\"";      json += nfcData.tagType;
  json += "\",\"technologies\":\""; json += nfcData.technologies;
  json += "\",\"atqa\":\"";          json += nfcData.atqa;
  json += "\",\"sak\":\"";           json += nfcData.sak;
  json += "\",\"memoryInfo\":\"";    json += nfcData.memoryInfo;
  json += "\",\"dataFormat\":\"";    json += escapeJson(nfcData.dataFormat);
  json += "\",\"memoryDump\":\"";    json += escapeJson(nfcData.memoryDump);
  json += "\",\"openPrintTagColor\":\""; json += escapeJson(nfcData.openPrintTagColor);
  json += "\",\"optParsed\":";  json += nfcData.optParsed ? "true" : "false";
  if (nfcData.optParsed) {
    json += ",\"optBrand\":\"";    json += escapeJson(nfcData.optBrand);
    json += "\",\"optMaterial\":\""; json += escapeJson(nfcData.optMaterial);
    json += "\",\"optMatType\":";  json += String(nfcData.optMatType);
    json += ",\"optDryTemp\":";    json += String(nfcData.optDryTemp);
    json += ",\"optDryTime\":";    json += String(nfcData.optDryTime);
    json += ",\"optNozzleMin\":";  json += String(nfcData.optNozzleMin);
    json += ",\"optNozzleMax\":";  json += String(nfcData.optNozzleMax);
    json += ",\"optBedMin\":";     json += String(nfcData.optBedMin);
    json += ",\"optBedMax\":";     json += String(nfcData.optBedMax);
    json += ",\"optDensity\":";    json += String(nfcData.optDensity, 3);
    json += ",\"optDiameter\":";   json += String(nfcData.optDiameter, 2);
    json += ",\"optNomWeight\":";  json += String(nfcData.optNomWeight);
    json += ",\"optActWeight\":";  json += String(nfcData.optActWeight);
    json += ",\"optEmptyWeight\":";json += String(nfcData.optEmptyWeight);
    json += ",\"optNomLength\":";  json += String(nfcData.optNomLength);
    json += ",\"optActLength\":";  json += String(nfcData.optActLength);
    json += ",\"optSpoolW\":";     json += String(nfcData.optSpoolW);
    json += ",\"optSpoolOd\":";    json += String(nfcData.optSpoolOd);
    json += ",\"optSpoolId\":";    json += String(nfcData.optSpoolId);
    json += ",\"optSpoolHole\":";  json += String(nfcData.optSpoolHole);
    json += ",\"optCountry\":\"";  json += escapeJson(nfcData.optCountry);
    json += "\"";
  }
  json += "}";
  return json;
}

int8_t findFreeSlot() {
  for (uint8_t i = 0; i < DryerSlots::NUM_SLOTS; i++) {
    if (!dryerSlots.slotOccupied[i]) return i;
  }
  return -1;
}

String slotJson() {
  String json = "{\"slots\":[";
  for (uint8_t i = 0; i < DryerSlots::NUM_SLOTS; i++) {
    if (i > 0) json += ",";
    json += "{\"index\":"; json += String(i);
    json += ",\"occupied\":"; json += dryerSlots.slotOccupied[i] ? "true" : "false";
    json += ",\"dryingTemp\":"; json += String(dryerSlots.dryingTemp[i]);
    json += ",\"dryingMinutes\":"; json += String(dryerSlots.dryingMinutes[i]);
    json += ",\"cycleRunning\":"; json += dryerSlots.cycleRunning[i] ? "true" : "false";
    json += ",\"material\":\""; json += escapeJson(dryerSlots.bobina[i].optMaterial);
    json += "\",\"brand\":\""; json += escapeJson(dryerSlots.bobina[i].optBrand);
    json += "\",\"uid\":\""; json += escapeJson(dryerSlots.bobina[i].lastUid);
    json += "\"";
    if (dryerSlots.cycleRunning[i] && dryerSlots.cycleEndTime[i] > 0) {
      uint32_t now = millis();
      int32_t remaining = (int32_t)(dryerSlots.cycleEndTime[i] - now) / 1000;
      json += ",\"cycleRemainingSeconds\":"; json += String(max(0L, (long)remaining));
    }
    json += "}";
  }
  json += "],\"freeSlot\":"; json += String(findFreeSlot());
  json += "}";
  return json;
}
