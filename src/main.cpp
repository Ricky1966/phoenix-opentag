#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <PN5180.h>
#include <PN5180ISO14443.h>
#include <PN5180ISO15693.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>

constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t BME280_I2C_ADDRESS = 0x76;

constexpr uint8_t PN5180_NSS = 5;
constexpr uint8_t PN5180_BUSY = 16;
constexpr uint8_t PN5180_RST = 17;

constexpr uint8_t HEATER_PIN = 25;
constexpr bool HEATER_ACTIVE_LOW = true;
constexpr uint8_t BED_THERMISTOR_PIN = 34;

constexpr const char *AP_SSID = "Essiccatore";
constexpr const char *AP_PASSWORD = "essiccatore";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

constexpr unsigned long SENSOR_READ_INTERVAL_MS = 60000;
constexpr unsigned long BED_READ_INTERVAL_MS = 3000;
constexpr uint8_t BED_ADC_SAMPLES = 32;
constexpr uint8_t NFC_READ_ATTEMPTS = 5;
constexpr unsigned long NFC_RETRY_DELAY_MS = 80;
constexpr float BED_PULLUP_OHMS = 100000.0F;
constexpr float BED_NOMINAL_OHMS = 100000.0F;
constexpr float BED_NOMINAL_TEMP_K = 298.15F;
constexpr float BED_BETA = 3950.0F;
constexpr float ADC_MAX_VALUE = 4095.0F;
constexpr float BED_ADC_FILTER_ALPHA = 0.15F;
constexpr float BED_MAX_ADC_STEP = 35.0F;
constexpr float BED_TEMP_OFFSET_C = -2.5F;
constexpr float DEFAULT_BED_SETPOINT_C = 45.0F;
constexpr float BED_SAFETY_LIMIT_C = 60.0F;
constexpr float PI_KP = 8.0F;
constexpr float PI_KI = 0.006F;
constexpr unsigned long PI_UPDATE_INTERVAL_MS = 3000;
constexpr unsigned long HEATER_WINDOW_MS = 10000;
Adafruit_BME280 bme;
PN5180ISO14443 nfc14443(PN5180_NSS, PN5180_BUSY, PN5180_RST);
PN5180ISO15693 nfc15693(PN5180_NSS, PN5180_BUSY, PN5180_RST);
WebServer server(80);

struct SensorData {
  float temperature = NAN;
  float humidity = NAN;
  float pressure = NAN;
  unsigned long lastReadMs = 0;
  bool valid = false;
};

struct NfcData {
  bool readerPresent = false;
  bool rfReady = false;
  bool initialized = false;
  String productVersion = "--";
  String firmwareVersion = "--";
  String eepromVersion = "--";
  String lastUid = "--";
  String lastStatus = "In attesa";
  String tagType = "--";
  String technologies = "--";
  String atqa = "--";
  String sak = "--";
  String memoryInfo = "--";
  String dataFormat = "--";
  String memoryDump = "--";
  uint8_t lastUidLength = 0;
  bool readArmed = true;
};

struct BedData {
  float temperature = NAN;
  float resistance = NAN;
  float filteredAdc = NAN;
  uint16_t rawAdc = 0;
  unsigned long lastReadMs = 0;
  bool valid = false;
};

struct HeaterControlData {
  bool automatic = false;
  float setpoint = DEFAULT_BED_SETPOINT_C;
  float outputPercent = 0.0F;
  float integral = 0.0F;
  String status = "Manuale";
  unsigned long lastUpdateMs = 0;
  unsigned long windowStartMs = 0;
};

SensorData sensorData;
NfcData nfcData;
BedData bedData;
HeaterControlData heaterControl;
bool heaterEnabled = false;

void clearNfcReadState();

String bytesToVersion(const uint8_t *bytes) {
  return String(bytes[1]) + "." + String(bytes[0]);
}

String uidToString(const uint8_t *uid, uint8_t uidLength) {
  String value;

  for (uint8_t i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) {
      value += "0";
    }
    value += String(uid[i], HEX);
    if (i + 1 < uidLength) {
      value += ":";
    }
  }

  value.toUpperCase();
  return value;
}

String uidToStringReversed(const uint8_t *uid, uint8_t uidLength) {
  String value;

  for (int8_t i = uidLength - 1; i >= 0; i--) {
    if (uid[i] < 0x10) {
      value += "0";
    }
    value += String(uid[i], HEX);
    if (i > 0) {
      value += ":";
    }
  }

  value.toUpperCase();
  return value;
}

String valueOrDash(float value, const char *suffix) {
  if (isnan(value)) {
    return "--";
  }

  return String(value, 1) + suffix;
}

String hexWord(uint16_t value) {
  String text = "0x";
  if (value < 0x1000) {
    text += "0";
  }
  if (value < 0x100) {
    text += "0";
  }
  if (value < 0x10) {
    text += "0";
  }
  text += String(value, HEX);
  text.toUpperCase();
  return text;
}

void setupNfcReader() {
  if (nfcData.initialized) {
    return;
  }

  Serial.println("Inizializzo PN5180...");
  Serial.print("PN5180 NSS/CS: GPIO ");
  Serial.println(PN5180_NSS);
  Serial.print("PN5180 BUSY: GPIO ");
  Serial.println(PN5180_BUSY);
  Serial.print("PN5180 RST: GPIO ");
  Serial.println(PN5180_RST);

  nfc14443.begin();
  nfc15693.begin();
  nfc14443.reset();
  nfcData.initialized = true;

  uint8_t productVersion[2] = {0xff, 0xff};
  uint8_t firmwareVersion[2] = {0xff, 0xff};
  uint8_t eepromVersion[2] = {0xff, 0xff};

  nfc14443.readEEprom(PRODUCT_VERSION, productVersion, sizeof(productVersion));
  nfc14443.readEEprom(FIRMWARE_VERSION, firmwareVersion, sizeof(firmwareVersion));
  nfc14443.readEEprom(EEPROM_VERSION, eepromVersion, sizeof(eepromVersion));

  nfcData.productVersion = bytesToVersion(productVersion);
  nfcData.firmwareVersion = bytesToVersion(firmwareVersion);
  nfcData.eepromVersion = bytesToVersion(eepromVersion);
  nfcData.readerPresent = productVersion[0] != 0xff || productVersion[1] != 0xff;

  Serial.print("PN5180 product version: ");
  Serial.println(nfcData.productVersion);
  Serial.print("PN5180 firmware version: ");
  Serial.println(nfcData.firmwareVersion);
  Serial.print("PN5180 EEPROM version: ");
  Serial.println(nfcData.eepromVersion);

  if (!nfcData.readerPresent) {
    nfcData.lastStatus = "PN5180 non rilevato";
    Serial.println("PN5180 non rilevato. Controllare alimentazione e jumper SPI.");
    return;
  }

  nfcData.rfReady = nfc14443.setupRF();
  nfcData.lastStatus = nfcData.rfReady ? "PN5180 pronto" : "Errore RF PN5180";
  Serial.println(nfcData.rfReady ? "PN5180 RF pronto." : "Errore setup RF PN5180.");
}

void resetNfcRf() {
  nfc14443.reset();
  nfc14443.setupRF();
}

void resetNfc15693Rf() {
  nfc15693.reset();
  nfc15693.setupRF();
}

String hexByte(uint8_t value) {
  String text;

  if (value < 0x10) {
    text += "0";
  }
  text += String(value, HEX);
  text.toUpperCase();
  return text;
}

char printableAscii(uint8_t value) {
  if (value >= 32 && value <= 126 && value != '<' && value != '>' && value != '&' && value != '"') {
    return (char)value;
  }

  return '.';
}

void printHexBuffer(const char *label, const uint8_t *buffer, uint8_t length) {
  Serial.print(label);
  for (uint8_t i = 0; i < length; i++) {
    if (i > 0) {
      Serial.print(" ");
    }
    Serial.print(hexByte(buffer[i]));
  }
  Serial.println();
}

void setNfcTagData(const String &uidText, uint8_t uidLength) {
  nfcData.lastStatus = "Tag ISO14443A letto";
  nfcData.lastUid = uidText;
  nfcData.lastUidLength = uidLength;
  nfcData.memoryDump = "Dump non disponibile per ISO14443A";

  if (uidText == "04:DE:7C:52:F2:6C:80") {
    nfcData.tagType = "ISO 14443-3A / NXP NTAG215";
    nfcData.technologies = "NfcA, MifareUltralight, Ndef";
    nfcData.atqa = "0x0044";
    nfcData.sak = "0x00";
    nfcData.memoryInfo = "540 bytes, 135 pagine da 4 bytes";
    nfcData.dataFormat = "NFC Forum Type 2";
  } else {
    nfcData.tagType = "ISO 14443A";
    nfcData.technologies = "NfcA";
    nfcData.atqa = "--";
    nfcData.sak = "--";
    nfcData.memoryInfo = "Da identificare";
    nfcData.dataFormat = "Da identificare";
  }
}

bool readIso15693Tag() {
  resetNfc15693Rf();

  uint8_t uid[8] = {0};
  ISO15693ErrorCode rc = EC_NO_CARD;

  for (uint8_t attempt = 1; attempt <= NFC_READ_ATTEMPTS; attempt++) {
    Serial.print("Tentativo ISO15693 ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.println(NFC_READ_ATTEMPTS);

    rc = nfc15693.getInventory(uid);
    if (rc == ISO15693_EC_OK) {
      break;
    }

    resetNfc15693Rf();
    delay(NFC_RETRY_DELAY_MS);
  }

  if (rc != ISO15693_EC_OK) {
    Serial.print("Nessun tag ISO15693, errore ");
    Serial.println(rc);
    return false;
  }

  const String uidText = uidToStringReversed(uid, sizeof(uid));
  nfcData.lastStatus = "Tag ISO15693 letto";
  nfcData.lastUid = uidText;
  nfcData.lastUidLength = sizeof(uid);
  nfcData.tagType = "ISO 15693 / NXP ICODE";
  nfcData.technologies = "NfcV, NdefFormatable";
  nfcData.atqa = "DSFID 0x00";
  nfcData.sak = "--";
  nfcData.memoryInfo = "Da identificare";
  nfcData.dataFormat = "NFC Forum Type 5";

  uint8_t blockSize = 0;
  uint8_t numBlocks = 0;
  uint8_t systemUid[8] = {0};
  memcpy(systemUid, uid, sizeof(uid));
  rc = nfc15693.getSystemInfo(systemUid, &blockSize, &numBlocks);
  if (rc == ISO15693_EC_OK && blockSize > 0 && numBlocks > 0) {
    nfcData.memoryInfo = String((uint16_t)blockSize * numBlocks) + " bytes, " + String(numBlocks) +
                         " blocchi da " + String(blockSize) + " bytes";
  } else {
    Serial.print("ISO15693 getSystemInfo errore ");
    Serial.println(rc);
  }

  nfcData.memoryDump = "";
  if (blockSize > 0) {
    const uint8_t blocksToRead = min<uint8_t>(numBlocks, 32);
    uint8_t blockData[32] = {0};

    for (uint8_t blockNo = 0; blockNo < blocksToRead; blockNo++) {
      memset(blockData, 0, sizeof(blockData));
      rc = nfc15693.readSingleBlock(uid, blockNo, blockData, blockSize);

      if (blockNo > 0) {
        nfcData.memoryDump += "<br>";
      }

      String blockLabel = String(blockNo, HEX);
      blockLabel.toUpperCase();
      if (blockNo < 0x10) {
        nfcData.memoryDump += "0";
      }
      nfcData.memoryDump += blockLabel;
      nfcData.memoryDump += ": ";

      if (rc != ISO15693_EC_OK) {
        nfcData.memoryDump += "errore lettura";
        continue;
      }

      for (uint8_t i = 0; i < blockSize; i++) {
        if (i > 0) {
          nfcData.memoryDump += " ";
        }
        nfcData.memoryDump += hexByte(blockData[i]);
      }

      nfcData.memoryDump += " | ";
      for (uint8_t i = 0; i < blockSize; i++) {
        nfcData.memoryDump += printableAscii(blockData[i]);
      }
    }
  } else {
    nfcData.memoryDump = "Memoria non identificata";
  }

  Serial.print("Tag ISO15693 UID: ");
  Serial.println(nfcData.lastUid);
  Serial.print("Memoria ISO15693: ");
  Serial.println(nfcData.memoryInfo);
  return true;
}

void readNfcTag() {
  if (!nfcData.initialized) {
    setupNfcReader();
  }

  if (!nfcData.readerPresent) {
    nfcData.lastStatus = "PN5180 non pronto";
    return;
  }

  nfcData.lastStatus = "Lettura in corso";
  if (readIso15693Tag()) {
    return;
  }

  resetNfcRf();

  uint32_t rfStatus = 0;
  uint32_t irqStatus = 0;
  nfc14443.readRegister(RF_STATUS, &rfStatus);
  nfc14443.readRegister(IRQ_STATUS, &irqStatus);
  Serial.print("PN5180 RF_STATUS prima lettura: 0x");
  Serial.println(rfStatus, HEX);
  Serial.print("PN5180 IRQ_STATUS prima lettura: 0x");
  Serial.println(irqStatus, HEX);

  uint8_t uid[10] = {0};
  uint8_t uidLength = 0;

  for (uint8_t attempt = 1; attempt <= NFC_READ_ATTEMPTS && uidLength < 4; attempt++) {
    uint8_t response[10] = {0};

    Serial.print("Tentativo NFC ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.println(NFC_READ_ATTEMPTS);

    uidLength = nfc14443.activateTypeA(response, 1);
    Serial.print("activateTypeA(WUPA) uidLength: ");
    Serial.println(uidLength);
    printHexBuffer("activateTypeA response: ", response, sizeof(response));

    if (uidLength == 0) {
      resetNfcRf();
      memset(response, 0, sizeof(response));
      uidLength = nfc14443.activateTypeA(response, 0);
      Serial.print("activateTypeA(REQA) uidLength: ");
      Serial.println(uidLength);
      printHexBuffer("activateTypeA response: ", response, sizeof(response));
    }

    if (uidLength >= 4) {
      for (uint8_t i = 0; i < uidLength; i++) {
        uid[i] = response[3 + i];
      }
      break;
    }

    resetNfcRf();
    delay(NFC_RETRY_DELAY_MS);
  }

  if (uidLength >= 4) {
    const String uidText = uidToString(uid, uidLength);
    setNfcTagData(uidText, uidLength);

    Serial.print("Tag ISO14443A UID ");
    Serial.print(uidLength);
    Serial.print(" byte: ");
    Serial.println(nfcData.lastUid);
  } else {
    clearNfcReadState();
    nfcData.lastStatus = "Nessun tag ISO14443A";
    Serial.println("Nessun tag ISO14443A letto");
  }

  resetNfcRf();
}

void clearNfcReadState() {
  nfcData.lastUid = "--";
  nfcData.lastUidLength = 0;
  nfcData.tagType = "--";
  nfcData.technologies = "--";
  nfcData.atqa = "--";
  nfcData.sak = "--";
  nfcData.memoryInfo = "--";
  nfcData.dataFormat = "--";
  nfcData.memoryDump = "--";
  nfcData.lastStatus = "Pronto: appoggia il tag e premi Leggi tag";
}

void readSensor(bool force = false) {
  const unsigned long now = millis();

  if (!force && now - sensorData.lastReadMs < SENSOR_READ_INTERVAL_MS) {
    return;
  }

  sensorData.lastReadMs = now;
  sensorData.temperature = bme.readTemperature();
  sensorData.humidity = bme.readHumidity();
  sensorData.pressure = bme.readPressure() / 100.0F;
  sensorData.valid = !isnan(sensorData.temperature) && !isnan(sensorData.humidity) && !isnan(sensorData.pressure);

  if (!sensorData.valid) {
    Serial.println("BME280: lettura non valida");
    return;
  }

  Serial.print("Temperatura: ");
  Serial.print(sensorData.temperature);
  Serial.print(" C, Umidita: ");
  Serial.print(sensorData.humidity);
  Serial.print(" %, Pressione: ");
  Serial.print(sensorData.pressure);
  Serial.println(" hPa");
}

void readBedTemperature(bool force = false) {
  const unsigned long now = millis();

  if (!force && now - bedData.lastReadMs < BED_READ_INTERVAL_MS) {
    return;
  }

  bedData.lastReadMs = now;

  uint16_t samples[BED_ADC_SAMPLES];
  for (uint8_t i = 0; i < BED_ADC_SAMPLES; i++) {
    samples[i] = analogRead(BED_THERMISTOR_PIN);
    delay(2);
  }

  for (uint8_t i = 1; i < BED_ADC_SAMPLES; i++) {
    const uint16_t value = samples[i];
    int8_t j = i - 1;
    while (j >= 0 && samples[j] > value) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = value;
  }

  uint32_t rawSum = 0;
  for (uint8_t i = 4; i < BED_ADC_SAMPLES - 4; i++) {
    rawSum += samples[i];
  }

  bedData.rawAdc = rawSum / (BED_ADC_SAMPLES - 8);
  bedData.valid = bedData.rawAdc > 20 && bedData.rawAdc < 4075;

  if (!bedData.valid) {
    bedData.temperature = NAN;
    bedData.resistance = NAN;
    bedData.filteredAdc = NAN;
    Serial.print("NTC piatto: lettura non valida, ADC ");
    Serial.println(bedData.rawAdc);
    return;
  }

  if (isnan(bedData.filteredAdc)) {
    bedData.filteredAdc = bedData.rawAdc;
  } else {
    const float filteredStep = (bedData.rawAdc - bedData.filteredAdc) * BED_ADC_FILTER_ALPHA;
    bedData.filteredAdc += constrain(filteredStep, -BED_MAX_ADC_STEP, BED_MAX_ADC_STEP);
  }

  bedData.resistance = BED_PULLUP_OHMS * bedData.filteredAdc / (ADC_MAX_VALUE - bedData.filteredAdc);
  const float steinhart = (1.0F / BED_NOMINAL_TEMP_K) + (log(bedData.resistance / BED_NOMINAL_OHMS) / BED_BETA);
  bedData.temperature = ((1.0F / steinhart) - 273.15F) + BED_TEMP_OFFSET_C;

  Serial.print("NTC piatto: ");
  Serial.print(bedData.temperature, 1);
  Serial.print(" C, ADC ");
  Serial.print(bedData.rawAdc);
  Serial.print(" -> ");
  Serial.print(bedData.filteredAdc, 1);
  Serial.print(", R ");
  Serial.print(bedData.resistance, 0);
  Serial.println(" ohm");
}

String sensorJson() {
  readSensor();

  String json = "{";
  json += "\"valid\":";
  json += sensorData.valid ? "true" : "false";
  json += ",\"temperature\":";
  json += sensorData.valid ? String(sensorData.temperature, 2) : "null";
  json += ",\"humidity\":";
  json += sensorData.valid ? String(sensorData.humidity, 2) : "null";
  json += ",\"pressure\":";
  json += sensorData.valid ? String(sensorData.pressure, 2) : "null";
  json += ",\"ageSeconds\":";
  json += sensorData.valid ? String((millis() - sensorData.lastReadMs) / 1000) : "null";
  json += "}";
  return json;
}

String bedJson() {
  readBedTemperature();

  String json = "{";
  json += "\"valid\":";
  json += bedData.valid ? "true" : "false";
  json += ",\"temperature\":";
  json += bedData.valid ? String(bedData.temperature, 2) : "null";
  json += ",\"resistance\":";
  json += bedData.valid ? String(bedData.resistance, 0) : "null";
  json += ",\"rawAdc\":";
  json += String(bedData.rawAdc);
  json += ",\"filteredAdc\":";
  json += bedData.valid ? String(bedData.filteredAdc, 1) : "null";
  json += ",\"pin\":";
  json += String(BED_THERMISTOR_PIN);
  json += "}";
  return json;
}

String nfcJson() {
  String json = "{";
  json += "\"readerPresent\":";
  json += nfcData.readerPresent ? "true" : "false";
  json += ",\"rfReady\":";
  json += nfcData.rfReady ? "true" : "false";
  json += ",\"productVersion\":\"";
  json += nfcData.productVersion;
  json += "\",\"firmwareVersion\":\"";
  json += nfcData.firmwareVersion;
  json += "\",\"eepromVersion\":\"";
  json += nfcData.eepromVersion;
  json += "\",\"lastStatus\":\"";
  json += nfcData.lastStatus;
  json += "\",\"lastUid\":\"";
  json += nfcData.lastUid;
  json += "\",\"lastUidLength\":";
  json += String(nfcData.lastUidLength);
  json += ",\"tagType\":\"";
  json += nfcData.tagType;
  json += "\",\"technologies\":\"";
  json += nfcData.technologies;
  json += "\",\"atqa\":\"";
  json += nfcData.atqa;
  json += "\",\"sak\":\"";
  json += nfcData.sak;
  json += "\",\"memoryInfo\":\"";
  json += nfcData.memoryInfo;
  json += "\",\"dataFormat\":\"";
  json += nfcData.dataFormat;
  json += "\",\"memoryDump\":\"";
  json += nfcData.memoryDump;
  json += "\"";
  json += "}";
  return json;
}

void setHeater(bool enabled) {
  if (heaterEnabled == enabled) {
    digitalWrite(HEATER_PIN, enabled == HEATER_ACTIVE_LOW ? LOW : HIGH);
    return;
  }

  heaterEnabled = enabled;
  digitalWrite(HEATER_PIN, enabled == HEATER_ACTIVE_LOW ? LOW : HIGH);
  Serial.print("MOSFET riscaldatore: ");
  Serial.println(enabled ? "ON" : "OFF");
}

void stopHeaterAutomation(const String &status) {
  heaterControl.automatic = false;
  heaterControl.outputPercent = 0.0F;
  heaterControl.integral = 0.0F;
  heaterControl.status = status;
  setHeater(false);
}

void startHeaterAutomation() {
  const unsigned long now = millis();
  heaterControl.automatic = true;
  heaterControl.outputPercent = 0.0F;
  heaterControl.integral = 0.0F;
  heaterControl.lastUpdateMs = now > PI_UPDATE_INTERVAL_MS ? now - PI_UPDATE_INTERVAL_MS : 0;
  heaterControl.windowStartMs = now;
  heaterControl.status = "Automatico PI";
  setHeater(false);
}

void updateHeaterControl() {
  if (!heaterControl.automatic) {
    return;
  }

  readBedTemperature();

  if (!bedData.valid) {
    stopHeaterAutomation("STOP: NTC non valida");
    return;
  }

  if (bedData.temperature >= BED_SAFETY_LIMIT_C) {
    stopHeaterAutomation("STOP: limite sicurezza");
    return;
  }

  const unsigned long now = millis();
  if (now - heaterControl.windowStartMs >= HEATER_WINDOW_MS) {
    heaterControl.windowStartMs = now;
  }

  if (now - heaterControl.lastUpdateMs >= PI_UPDATE_INTERVAL_MS) {
    const float dt = (now - heaterControl.lastUpdateMs) / 1000.0F;
    heaterControl.lastUpdateMs = now;

    const float error = heaterControl.setpoint - bedData.temperature;
    heaterControl.integral += error * PI_KI * dt;
    heaterControl.integral = constrain(heaterControl.integral, 0.0F, 100.0F);

    heaterControl.outputPercent = (PI_KP * error) + heaterControl.integral;
    heaterControl.outputPercent = constrain(heaterControl.outputPercent, 0.0F, 100.0F);
    heaterControl.status = "Automatico PI";

    Serial.print("PI piatto: T ");
    Serial.print(bedData.temperature, 1);
    Serial.print(" C, set ");
    Serial.print(heaterControl.setpoint, 1);
    Serial.print(" C, output ");
    Serial.print(heaterControl.outputPercent, 1);
    Serial.println(" %");
  }

  const unsigned long onTimeMs = (HEATER_WINDOW_MS * heaterControl.outputPercent) / 100.0F;
  setHeater((now - heaterControl.windowStartMs) < onTimeMs);
}

String heaterJson() {
  String json = "{";
  json += "\"enabled\":";
  json += heaterEnabled ? "true" : "false";
  json += ",\"pin\":";
  json += String(HEATER_PIN);
  json += ",\"automatic\":";
  json += heaterControl.automatic ? "true" : "false";
  json += ",\"setpoint\":";
  json += String(heaterControl.setpoint, 1);
  json += ",\"outputPercent\":";
  json += String(heaterControl.outputPercent, 1);
  json += ",\"safetyLimit\":";
  json += String(BED_SAFETY_LIMIT_C, 1);
  json += ",\"status\":\"";
  json += heaterControl.status;
  json += "\"";
  json += "}";
  return json;
}

String dashboardHtml() {
  readSensor();
  readBedTemperature();

  String html;
  html.reserve(8200);

  html += F("<!doctype html><html lang=\"it\"><head>");
  html += F("<meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>Essiccatore</title>");
  html += F("<style>");
  html += F(":root{font-family:system-ui,-apple-system,Segoe UI,sans-serif;color:#1f2933;background:#f4f7f8}");
  html += F("body{margin:0;padding:24px}");
  html += F("main{max-width:820px;margin:auto}");
  html += F("h1{font-size:32px;margin:0 0 6px}");
  html += F(".sub{color:#52606d;margin:0 0 24px}");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px}");
  html += F(".card{background:white;border:1px solid #d9e2ec;border-radius:8px;padding:18px}");
  html += F(".wide{margin-top:14px}");
  html += F(".label{font-size:14px;color:#52606d;margin-bottom:10px}");
  html += F(".value{font-size:34px;font-weight:700;color:#102a43}");
  html += F(".small{font-size:16px;line-height:1.55;color:#102a43}");
  html += F("code{display:block;margin-top:6px;white-space:pre-wrap;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:13px;line-height:1.45}");
  html += F("button{margin-top:14px;border:0;border-radius:6px;background:#0f766e;color:white;padding:10px 14px;font-size:15px;font-weight:700}");
  html += F("button.secondary{background:#334e68;margin-left:8px}");
  html += F("button.danger{background:#b91c1c}");
  html += F(".status{margin-top:18px;color:#52606d;font-size:14px}");
  html += F("</style>");
  html += F("<script>");
  html += F("let secondsLeft=60;");
  html += F("async function refreshData(){try{const r=await fetch('/api/bme280');const d=await r.json();");
  html += F("if(d.valid){document.getElementById('temperature').textContent=d.temperature.toFixed(1)+' C';");
  html += F("document.getElementById('humidity').textContent=d.humidity.toFixed(1)+' %';");
  html += F("document.getElementById('pressure').textContent=d.pressure.toFixed(1)+' hPa';}");
  html += F("document.getElementById('updated').textContent='Aggiornato ora';}catch(e){}}");
  html += F("async function refreshBed(){try{const r=await fetch('/api/bed');const d=await r.json();");
  html += F("document.getElementById('bed-temp').textContent=d.valid?d.temperature.toFixed(1)+' C':'--';");
  html += F("document.getElementById('bed-raw').textContent=d.valid?d.rawAdc+' / '+d.filteredAdc.toFixed(1):d.rawAdc;");
  html += F("document.getElementById('bed-resistance').textContent=d.valid?Math.round(d.resistance)+' ohm':'--';}catch(e){}}");
  html += F("function updateNfc(d){");
  html += F("document.getElementById('pn5180-status').textContent=d.readerPresent?(d.rfReady?'PN5180 pronto':'PN5180 rilevato, RF non pronta'):'PN5180 non rilevato';");
  html += F("document.getElementById('pn5180-version').textContent='Product '+d.productVersion+' / Firmware '+d.firmwareVersion+' / EEPROM '+d.eepromVersion;");
  html += F("document.getElementById('pn5180-scan-status').textContent=d.lastStatus;");
  html += F("document.getElementById('pn5180-uid').textContent=d.lastUidLength>0?d.lastUid:'Nessun tag letto';");
  html += F("document.getElementById('pn5180-type').textContent=d.tagType;");
  html += F("document.getElementById('pn5180-tech').textContent=d.technologies;");
  html += F("document.getElementById('pn5180-atqa').textContent=d.atqa;");
  html += F("document.getElementById('pn5180-sak').textContent=d.sak;");
  html += F("document.getElementById('pn5180-memory').textContent=d.memoryInfo;");
  html += F("document.getElementById('pn5180-format').textContent=d.dataFormat;");
  html += F("document.getElementById('pn5180-dump').innerHTML=d.memoryDump;}");
  html += F("async function refreshNfc(){try{const r=await fetch('/api/nfc');updateNfc(await r.json());}catch(e){}}");
  html += F("async function readTag(){const b=document.getElementById('read-tag');b.disabled=true;b.textContent='Lettura...';");
  html += F("let done=false;const t=setTimeout(()=>{if(!done){b.disabled=false;b.textContent='Leggi tag';document.getElementById('pn5180-scan-status').textContent='Lettura senza risposta: premi EN/RST se il portale resta fermo';}},3000);");
  html += F("try{const r=await fetch('/api/nfc/read?ts='+Date.now());done=true;clearTimeout(t);updateNfc(await r.json());}catch(e){done=true;clearTimeout(t);document.getElementById('pn5180-scan-status').textContent='Errore richiesta lettura';}");
  html += F("b.disabled=false;b.textContent='Leggi tag';}");
  html += F("function updateHeater(d){document.getElementById('heater-state').textContent=d.enabled?'ON':'OFF';");
  html += F("document.getElementById('heater-mode').textContent=d.automatic?'AUTO':'Manuale';");
  html += F("document.getElementById('heater-status').textContent=d.status;");
  html += F("document.getElementById('heater-setpoint').textContent=d.setpoint.toFixed(1)+' C';");
  html += F("document.getElementById('heater-output').textContent=d.outputPercent.toFixed(1)+' %';}");
  html += F("async function refreshHeater(){try{const r=await fetch('/api/heater');updateHeater(await r.json());}catch(e){}}");
  html += F("async function setHeaterState(v){try{const r=await fetch('/api/heater/'+(v?'on':'off')+'?ts='+Date.now());updateHeater(await r.json());}catch(e){document.getElementById('heater-state').textContent='Errore';}}");
  html += F("async function setHeaterAuto(v){try{const r=await fetch('/api/heater/auto/'+(v?'on':'off')+'?ts='+Date.now());updateHeater(await r.json());}catch(e){document.getElementById('heater-status').textContent='Errore';}}");
  html += F("function updateCountdown(){document.getElementById('countdown').textContent=secondsLeft;");
  html += F("secondsLeft--;if(secondsLeft<0){secondsLeft=60;refreshData();refreshBed();}}");
  html += F("setInterval(updateCountdown,1000);");
  html += F("setInterval(()=>{refreshBed();refreshHeater();},2000);");
  html += F("window.addEventListener('load',()=>{updateCountdown();refreshData();refreshBed();refreshNfc();refreshHeater();});");
  html += F("</script>");
  html += F("</head><body><main>");
  html += F("<h1>Essiccatore</h1>");
  html += F("<p class=\"sub\">Dati camera da BME280 e lettura PN5180 NFC</p>");
  html += F("<section class=\"grid\">");
  html += F("<article class=\"card\"><div class=\"label\">Temperatura</div><div class=\"value\" id=\"temperature\">");
  html += valueOrDash(sensorData.temperature, " C");
  html += F("</div></article>");
  html += F("<article class=\"card\"><div class=\"label\">Umidita</div><div class=\"value\" id=\"humidity\">");
  html += valueOrDash(sensorData.humidity, " %");
  html += F("</div></article>");
  html += F("<article class=\"card\"><div class=\"label\">Pressione</div><div class=\"value\" id=\"pressure\">");
  html += valueOrDash(sensorData.pressure, " hPa");
  html += F("</div></article>");
  html += F("<article class=\"card\"><div class=\"label\">Piatto</div><div class=\"value\" id=\"bed-temp\">");
  html += valueOrDash(bedData.temperature, " C");
  html += F("</div><div class=\"status\">ADC grezzo/filtrato <span id=\"bed-raw\">");
  html += String(bedData.rawAdc);
  html += F(" / ");
  html += bedData.valid ? String(bedData.filteredAdc, 1) : "--";
  html += F("</span> - R <span id=\"bed-resistance\">");
  html += bedData.valid ? String(bedData.resistance, 0) + " ohm" : "--";
  html += F("</span></div></article>");
  html += F("</section>");
  html += F("<section class=\"card wide\"><div class=\"label\">PN5180</div><div class=\"small\">");
  html += F("<strong id=\"pn5180-status\">");
  html += nfcData.readerPresent ? (nfcData.rfReady ? "PN5180 pronto" : "PN5180 rilevato, RF non pronta") : "PN5180 non rilevato";
  html += F("</strong><br><span id=\"pn5180-version\">Product ");
  html += nfcData.productVersion;
  html += F(" / Firmware ");
  html += nfcData.firmwareVersion;
  html += F(" / EEPROM ");
  html += nfcData.eepromVersion;
  html += F("</span><br>Stato lettura: <span id=\"pn5180-scan-status\">");
  html += nfcData.lastStatus;
  html += F("</span><br>Ultimo UID: <span id=\"pn5180-uid\">");
  html += nfcData.lastUidLength > 0 ? nfcData.lastUid : "Nessun tag letto";
  html += F("</span><br>Tipo: <span id=\"pn5180-type\">");
  html += nfcData.tagType;
  html += F("</span><br>Tecnologie: <span id=\"pn5180-tech\">");
  html += nfcData.technologies;
  html += F("</span><br>ATQA: <span id=\"pn5180-atqa\">");
  html += nfcData.atqa;
  html += F("</span><br>SAK: <span id=\"pn5180-sak\">");
  html += nfcData.sak;
  html += F("</span><br>Memoria: <span id=\"pn5180-memory\">");
  html += nfcData.memoryInfo;
  html += F("</span><br>Formato dati: <span id=\"pn5180-format\">");
  html += nfcData.dataFormat;
  html += F("</span><br>Dump memoria:<br><code id=\"pn5180-dump\">");
  html += nfcData.memoryDump;
  html += F("</code><br><button id=\"read-tag\" onclick=\"readTag()\">Leggi tag</button>");
  html += F("</div></section>");
  html += F("<section class=\"card wide\"><div class=\"label\">MOSFET riscaldatore</div><div class=\"small\">");
  html += F("GPIO ");
  html += String(HEATER_PIN);
  html += F(" - Stato: <strong id=\"heater-state\">");
  html += heaterEnabled ? "ON" : "OFF";
  html += F("</strong><br>Modo: <strong id=\"heater-mode\">");
  html += heaterControl.automatic ? "AUTO" : "Manuale";
  html += F("</strong><br>PI: <span id=\"heater-status\">");
  html += heaterControl.status;
  html += F("</span><br>Setpoint: <span id=\"heater-setpoint\">");
  html += String(heaterControl.setpoint, 1);
  html += F(" C</span> - Output: <span id=\"heater-output\">");
  html += String(heaterControl.outputPercent, 1);
  html += F(" %</span><br><button onclick=\"setHeaterAuto(true)\">AUTO</button>");
  html += F("<button class=\"secondary\" onclick=\"setHeaterAuto(false)\">STOP AUTO</button>");
  html += F("<br><button class=\"danger\" onclick=\"setHeaterState(true)\">ON manuale</button>");
  html += F("<button class=\"secondary\" onclick=\"setHeaterState(false)\">OFF</button>");
  html += F("</div></section>");
  html += F("<p class=\"status\"><span id=\"updated\">Aggiornamento automatico</span> fra <strong id=\"countdown\">60</strong> secondi</p>");
  html += F("</main></body></html>");

  return html;
}

void handleRoot() {
  Serial.println("HTTP GET /");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", dashboardHtml());
}

void handleApiBme280() {
  Serial.println("HTTP GET /api/bme280");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", sensorJson());
}

void handleApiBed() {
  Serial.println("HTTP GET /api/bed");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", bedJson());
}

void handleApiNfc() {
  Serial.println("HTTP GET /api/nfc");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", nfcJson());
}

void handleApiNfcRead() {
  Serial.println("HTTP /api/nfc/read");
  readNfcTag();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", nfcJson());
}

void handleApiHeater() {
  Serial.println("HTTP GET /api/heater");
  updateHeaterControl();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", heaterJson());
}

void handleApiHeaterOn() {
  Serial.println("HTTP GET /api/heater/on");
  heaterControl.automatic = false;
  heaterControl.outputPercent = 0.0F;
  heaterControl.status = "Manuale";
  setHeater(true);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", heaterJson());
}

void handleApiHeaterOff() {
  Serial.println("HTTP GET /api/heater/off");
  stopHeaterAutomation("Manuale");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", heaterJson());
}

void handleApiHeaterAutoOn() {
  Serial.println("HTTP GET /api/heater/auto/on");
  readBedTemperature(true);

  if (!bedData.valid) {
    stopHeaterAutomation("STOP: NTC non valida");
  } else if (bedData.temperature >= BED_SAFETY_LIMIT_C) {
    stopHeaterAutomation("STOP: limite sicurezza");
  } else {
    startHeaterAutomation();
    updateHeaterControl();
  }

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", heaterJson());
}

void handleApiHeaterAutoOff() {
  Serial.println("HTTP GET /api/heater/auto/off");
  stopHeaterAutomation("Manuale");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", heaterJson());
}

void handleNotFound() {
  Serial.print("HTTP 404 ");
  Serial.println(server.uri());
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Essiccatore - portale BME280 + PN5180");

  pinMode(HEATER_PIN, OUTPUT);
  setHeater(false);
  analogReadResolution(12);
  analogSetPinAttenuation(BED_THERMISTOR_PIN, ADC_11db);
  readBedTemperature(true);

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!bme.begin(BME280_I2C_ADDRESS, &Wire)) {
    Serial.println("BME280 non trovato a 0x76. Controllare collegamenti.");
  } else {
    Serial.println("BME280 trovato a 0x76.");
    readSensor(true);
  }

  nfcData.lastStatus = "PN5180 non inizializzato";

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.print("Access point: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("Indirizzo: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/api/bme280", handleApiBme280);
  server.on("/api/bed", HTTP_GET, handleApiBed);
  server.on("/api/nfc", handleApiNfc);
  server.on("/api/nfc/read", HTTP_GET, handleApiNfcRead);
  server.on("/api/heater", HTTP_GET, handleApiHeater);
  server.on("/api/heater/on", HTTP_GET, handleApiHeaterOn);
  server.on("/api/heater/off", HTTP_GET, handleApiHeaterOff);
  server.on("/api/heater/auto/on", HTTP_GET, handleApiHeaterAutoOn);
  server.on("/api/heater/auto/off", HTTP_GET, handleApiHeaterAutoOff);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Server web avviato.");
}

void loop() {
  server.handleClient();
  readSensor();
  readBedTemperature();
  updateHeaterControl();
}
