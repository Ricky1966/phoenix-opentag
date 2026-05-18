#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <PN5180.h>
#include <PN5180ISO14443.h>
#include <PN5180ISO15693.h>
#include <Preferences.h>
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
constexpr float BED_SAFETY_LIMIT_C = 90.0F;
constexpr float PI_KP = 10.0F;
constexpr float PI_KI = 0.028F;
constexpr unsigned long PI_UPDATE_INTERVAL_MS = 3000;
constexpr unsigned long HEATER_WINDOW_MS = 10000;

#if defined(UI_LOCALE_DE)
constexpr const char *UI_LOCALE_CODE = "de";
constexpr const char *UI_LOCALE_NAME = "Deutsch";
#elif defined(UI_LOCALE_ES)
constexpr const char *UI_LOCALE_CODE = "es";
constexpr const char *UI_LOCALE_NAME = "Espanol";
#elif defined(UI_LOCALE_FR)
constexpr const char *UI_LOCALE_CODE = "fr";
constexpr const char *UI_LOCALE_NAME = "Francais";
#elif defined(UI_LOCALE_PT)
constexpr const char *UI_LOCALE_CODE = "pt";
constexpr const char *UI_LOCALE_NAME = "Portugues";
#else
constexpr const char *UI_LOCALE_CODE = "it";
constexpr const char *UI_LOCALE_NAME = "Italiano";
#endif

Adafruit_BME280 bme;
PN5180ISO14443 nfc14443(PN5180_NSS, PN5180_BUSY, PN5180_RST);
PN5180ISO15693 nfc15693(PN5180_NSS, PN5180_BUSY, PN5180_RST);
WebServer server(80);
Preferences preferences;

struct SensorData {
  float temperature = NAN;
  float humidity = NAN;
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
  uint32_t cycleDurationSeconds = 360UL * 60UL;
  uint32_t cycleRemainingSeconds = 360UL * 60UL;
  unsigned long cycleEndMs = 0;
  bool cycleRunning = false;
};

struct MaterialDryProfile {
  const char *name;
  float recommendedTemp;
  uint16_t recommendedMinutes;
};

struct CustomMaterialProfile {
  String name;
  float recommendedTemp = 0.0F;
  uint16_t recommendedMinutes = 0;
};

struct AppSettings {
  String wifiSsid = "";
  String wifiPassword = "";
  String language = UI_LOCALE_CODE;
  bool wifiClientEnabled = false;
};

SensorData sensorData;
NfcData nfcData;
BedData bedData;
HeaterControlData heaterControl;
AppSettings appSettings;
bool heaterEnabled = false;
uint8_t selectedMaterialProfile = 0;

constexpr MaterialDryProfile MATERIAL_PROFILES[] = {
  {"PLA", 45.0F, 360},
  {"PETG", 55.0F, 360},
  {"TPU", 60.0F, 240},
  {"ASA", 80.0F, 240},
  {"ABS", 80.0F, 240},
  {"PC", 85.0F, 300},
};
constexpr uint8_t MATERIAL_PROFILE_COUNT = sizeof(MATERIAL_PROFILES) / sizeof(MATERIAL_PROFILES[0]);
constexpr uint8_t MAX_CUSTOM_PROFILES = 8;
CustomMaterialProfile customProfiles[MAX_CUSTOM_PROFILES];
uint8_t customProfileCount = 0;

void clearNfcReadState();
enum PageMode : uint8_t { PAGE_HOME, PAGE_TAGS, PAGE_SETPOINT, PAGE_SETTINGS };
String dashboardHtml(PageMode pageMode = PAGE_HOME);

enum TextKey : uint8_t {
  TK_APP_TITLE,
  TK_APP_SUBTITLE,
  TK_NAV_HOME,
  TK_NAV_HOME_TIP,
  TK_NAV_TAG,
  TK_NAV_TAG_TIP,
  TK_NAV_SETPOINT,
  TK_NAV_SETPOINT_TIP,
  TK_NAV_SETTINGS,
  TK_NAV_SETTINGS_TIP,
  TK_SETTINGS_NETWORK,
  TK_SETTINGS_LANGUAGE,
  TK_SETTINGS_WIFI_SSID,
  TK_SETTINGS_WIFI_PASSWORD,
  TK_SETTINGS_WIFI_MODE,
  TK_SETTINGS_WIFI_DISABLED,
  TK_SETTINGS_WIFI_SAVED,
  TK_SETTINGS_SAVE,
  TK_SETTINGS_NOTE,
  TK_FIRMWARE_LOCALE,
  TK_FALLBACK_LOCALE
};

const __FlashStringHelper *textEn(TextKey key) {
  switch (key) {
    case TK_APP_TITLE: return F("Dryer");
    case TK_APP_SUBTITLE: return F("Temperature, humidity and NFC tag management");
    case TK_NAV_HOME: return F("Home");
    case TK_NAV_HOME_TIP: return F("Home: live chamber, humidity and bed readings");
    case TK_NAV_TAG: return F("Tag Reader");
    case TK_NAV_TAG_TIP: return F("Tag Reader: read and write NFC OpenPrintTag data");
    case TK_NAV_SETPOINT: return F("Material Setpoints");
    case TK_NAV_SETPOINT_TIP: return F("Material Setpoints: heater, cycles and material profiles");
    case TK_NAV_SETTINGS: return F("Settings");
    case TK_NAV_SETTINGS_TIP: return F("Settings: language, WiFi network and general options");
    case TK_SETTINGS_NETWORK: return F("Network and interface");
    case TK_SETTINGS_LANGUAGE: return F("Firmware language");
    case TK_SETTINGS_WIFI_SSID: return F("WiFi SSID");
    case TK_SETTINGS_WIFI_PASSWORD: return F("WiFi password");
    case TK_SETTINGS_WIFI_MODE: return F("Router WiFi connection");
    case TK_SETTINGS_WIFI_DISABLED: return F("Disabled");
    case TK_SETTINGS_WIFI_SAVED: return F("Saved, not active");
    case TK_SETTINGS_SAVE: return F("Save settings");
    case TK_SETTINGS_NOTE: return F("Credentials are only saved. The portal stays on the Essiccatore access point until router connection is enabled.");
    case TK_FIRMWARE_LOCALE: return F("Compiled language");
    case TK_FALLBACK_LOCALE: return F("Fallback language");
  }
  return F("");
}

const __FlashStringHelper *textLocal(TextKey key) {
#if defined(UI_LOCALE_IT)
  switch (key) {
    case TK_APP_TITLE: return F("Essiccatore");
    case TK_APP_SUBTITLE: return F("Temperatura, umidita e gestione tag NFC");
    case TK_NAV_HOME: return F("Home");
    case TK_NAV_HOME_TIP: return F("Home: mostra le letture live di camera, umidita e piatto");
    case TK_NAV_TAG: return F("Lettura TAG");
    case TK_NAV_TAG_TIP: return F("Lettura TAG: legge e scrive i tag NFC OpenPrintTag");
    case TK_NAV_SETPOINT: return F("SetPoint Materiali");
    case TK_NAV_SETPOINT_TIP: return F("SetPoint Materiali: gestisce riscaldatore, cicli e profili materiale");
    case TK_NAV_SETTINGS: return F("Settings");
    case TK_NAV_SETTINGS_TIP: return F("Settings: lingua, rete WiFi e impostazioni generali");
    case TK_SETTINGS_NETWORK: return F("Rete e interfaccia");
    case TK_SETTINGS_LANGUAGE: return F("Lingua firmware");
    case TK_SETTINGS_WIFI_SSID: return F("SSID WiFi");
    case TK_SETTINGS_WIFI_PASSWORD: return F("Password WiFi");
    case TK_SETTINGS_WIFI_MODE: return F("Connessione a router WiFi");
    case TK_SETTINGS_WIFI_DISABLED: return F("Disattivato");
    case TK_SETTINGS_WIFI_SAVED: return F("Salvata, non attiva");
    case TK_SETTINGS_SAVE: return F("Salva impostazioni");
    case TK_SETTINGS_NOTE: return F("Le credenziali vengono solo salvate. Il portale resta su Access Point Essiccatore finche non abilitiamo la connessione al router.");
    case TK_FIRMWARE_LOCALE: return F("Lingua compilata");
    case TK_FALLBACK_LOCALE: return F("Lingua fallback");
  }
#endif
  return textEn(key);
}

const __FlashStringHelper *tr(TextKey key) {
  if (appSettings.language == "en") {
    return textEn(key);
  }
  return textLocal(key);
}

uint8_t totalMaterialProfileCount() {
  return MATERIAL_PROFILE_COUNT + customProfileCount;
}

bool materialProfileExists(uint8_t id) {
  return id < totalMaterialProfileCount();
}

String materialProfileName(uint8_t id) {
  if (id < MATERIAL_PROFILE_COUNT) {
    return MATERIAL_PROFILES[id].name;
  }

  const uint8_t customIndex = id - MATERIAL_PROFILE_COUNT;
  if (customIndex < customProfileCount) {
    return customProfiles[customIndex].name;
  }

  return "Sconosciuto";
}

float materialProfileTemp(uint8_t id) {
  if (id < MATERIAL_PROFILE_COUNT) {
    return MATERIAL_PROFILES[id].recommendedTemp;
  }

  const uint8_t customIndex = id - MATERIAL_PROFILE_COUNT;
  if (customIndex < customProfileCount) {
    return customProfiles[customIndex].recommendedTemp;
  }

  return DEFAULT_BED_SETPOINT_C;
}

uint16_t materialProfileMinutes(uint8_t id) {
  if (id < MATERIAL_PROFILE_COUNT) {
    return MATERIAL_PROFILES[id].recommendedMinutes;
  }

  const uint8_t customIndex = id - MATERIAL_PROFILE_COUNT;
  if (customIndex < customProfileCount) {
    return customProfiles[customIndex].recommendedMinutes;
  }

  return 360;
}

bool addCustomMaterialProfile(String name, float temperature, uint16_t minutes, uint8_t &newId) {
  name.trim();
  if (name.length() == 0 || customProfileCount >= MAX_CUSTOM_PROFILES) {
    return false;
  }

  if (name.length() > 24) {
    name = name.substring(0, 24);
  }

  for (uint8_t i = 0; i < name.length(); i++) {
    const char c = name[i];
    const bool allowed = isAlphaNumeric(c) || c == ' ' || c == '.' || c == '_' || c == '-';
    if (!allowed) {
      name.setCharAt(i, ' ');
    }
  }
  name.trim();
  if (name.length() == 0) {
    return false;
  }

  for (uint8_t i = 0; i < totalMaterialProfileCount(); i++) {
    if (materialProfileName(i).equalsIgnoreCase(name)) {
      return false;
    }
  }

  customProfiles[customProfileCount].name = name;
  customProfiles[customProfileCount].recommendedTemp = constrain(temperature, 20.0F, BED_SAFETY_LIMIT_C);
  customProfiles[customProfileCount].recommendedMinutes = constrain(minutes, (uint16_t)1, (uint16_t)9999);
  newId = MATERIAL_PROFILE_COUNT + customProfileCount;
  customProfileCount++;
  return true;
}

void saveCustomProfiles() {
  preferences.begin("essicc", false);
  preferences.putUChar("profileCount", customProfileCount);

  for (uint8_t i = 0; i < MAX_CUSTOM_PROFILES; i++) {
    const String prefix = "p" + String(i);
    if (i < customProfileCount) {
      preferences.putString((prefix + "Name").c_str(), customProfiles[i].name);
      preferences.putFloat((prefix + "Temp").c_str(), customProfiles[i].recommendedTemp);
      preferences.putUShort((prefix + "Min").c_str(), customProfiles[i].recommendedMinutes);
    } else {
      preferences.remove((prefix + "Name").c_str());
      preferences.remove((prefix + "Temp").c_str());
      preferences.remove((prefix + "Min").c_str());
    }
  }

  preferences.end();
}

void loadCustomProfiles() {
  preferences.begin("essicc", true);
  customProfileCount = min<uint8_t>(preferences.getUChar("profileCount", 0), MAX_CUSTOM_PROFILES);

  for (uint8_t i = 0; i < customProfileCount; i++) {
    const String prefix = "p" + String(i);
    customProfiles[i].name = preferences.getString((prefix + "Name").c_str(), "");
    customProfiles[i].recommendedTemp = preferences.getFloat((prefix + "Temp").c_str(), DEFAULT_BED_SETPOINT_C);
    customProfiles[i].recommendedMinutes = preferences.getUShort((prefix + "Min").c_str(), 360);

    if (customProfiles[i].name.length() == 0) {
      customProfileCount = i;
      break;
    }
  }

  preferences.end();
}

void saveAppSettings() {
  preferences.begin("essicc", false);
  preferences.putString("wifiSsid", appSettings.wifiSsid);
  preferences.putString("wifiPass", appSettings.wifiPassword);
  preferences.putString("lang", appSettings.language);
  preferences.putBool("wifiClient", appSettings.wifiClientEnabled);
  preferences.end();
}

void loadAppSettings() {
  preferences.begin("essicc", true);
  appSettings.wifiSsid = preferences.getString("wifiSsid", "");
  appSettings.wifiPassword = preferences.getString("wifiPass", "");
  appSettings.language = preferences.getString("lang", UI_LOCALE_CODE);
  appSettings.wifiClientEnabled = preferences.getBool("wifiClient", false);
  preferences.end();

  if (appSettings.language != UI_LOCALE_CODE && appSettings.language != "en") {
    appSettings.language = UI_LOCALE_CODE;
  }
}

constexpr uint8_t OPENPRINTTAG_MIME_TYPE[] = {
  'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n', '/',
  'v', 'n', 'd', '.', 'o', 'p', 'e', 'n', 'p', 'r', 'i', 'n', 't', 't', 'a', 'g'
};

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

String escapeJson(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (uint16_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c == '\\' || c == '"') {
      escaped += '\\';
    }
    escaped += c;
  }

  return escaped;
}

String escapeHtml(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (uint16_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c == '&') {
      escaped += F("&amp;");
    } else if (c == '"') {
      escaped += F("&quot;");
    } else if (c == '<') {
      escaped += F("&lt;");
    } else if (c == '>') {
      escaped += F("&gt;");
    } else {
      escaped += c;
    }
  }

  return escaped;
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

uint16_t appendCborUInt(uint8_t *buffer, uint16_t pos, uint32_t value) {
  if (value < 24) {
    buffer[pos++] = value;
  } else if (value <= 0xff) {
    buffer[pos++] = 0x18;
    buffer[pos++] = value;
  } else if (value <= 0xffff) {
    buffer[pos++] = 0x19;
    buffer[pos++] = value >> 8;
    buffer[pos++] = value & 0xff;
  } else {
    buffer[pos++] = 0x1A;
    buffer[pos++] = (value >> 24) & 0xff;
    buffer[pos++] = (value >> 16) & 0xff;
    buffer[pos++] = (value >> 8) & 0xff;
    buffer[pos++] = value & 0xff;
  }

  return pos;
}

uint16_t appendCborFloat32(uint8_t *buffer, uint16_t pos, float value) {
  union {
    float f;
    uint32_t u;
  } data;
  data.f = value;

  buffer[pos++] = 0xFA;
  buffer[pos++] = (data.u >> 24) & 0xff;
  buffer[pos++] = (data.u >> 16) & 0xff;
  buffer[pos++] = (data.u >> 8) & 0xff;
  buffer[pos++] = data.u & 0xff;
  return pos;
}

uint16_t appendCborText(uint8_t *buffer, uint16_t pos, String value) {
  value.trim();
  if (value.length() > 31) {
    value = value.substring(0, 31);
  }

  const uint8_t len = value.length();
  if (len < 24) {
    buffer[pos++] = 0x60 | len;
  } else {
    buffer[pos++] = 0x78;
    buffer[pos++] = len;
  }

  for (uint8_t i = 0; i < len; i++) {
    buffer[pos++] = value[i];
  }

  return pos;
}

uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return 0;
}

uint16_t appendCborColor(uint8_t *buffer, uint16_t pos, String color) {
  color.trim();
  color.replace("#", "");
  if (color.length() != 6 && color.length() != 8) {
    color = "FFFFFF";
  }

  const uint8_t bytes = color.length() == 8 ? 4 : 3;
  buffer[pos++] = 0x40 | bytes;
  for (uint8_t i = 0; i < bytes; i++) {
    buffer[pos++] = (hexNibble(color[i * 2]) << 4) | hexNibble(color[(i * 2) + 1]);
  }

  return pos;
}

uint16_t buildOpenPrintTagPayload(uint8_t *payload, uint16_t payloadSize, const String &brandName,
                                  const String &materialName, uint8_t materialType, uint16_t minPrintTemp,
                                  uint16_t maxPrintTemp, uint16_t minBedTemp, uint16_t maxBedTemp,
                                  uint16_t dryingTemp, uint16_t dryingTime, const String &primaryColor,
                                  uint16_t nominalWeight, uint16_t actualWeight, uint16_t emptyWeight,
                                  float density, float filamentDiameter, uint32_t nominalLength,
                                  uint32_t actualLength, uint32_t manufacturedDate, const String &country,
                                  uint16_t spoolWidth, uint16_t spoolOuterDiameter, uint16_t spoolInnerDiameter,
                                  uint16_t spoolHoleDiameter) {
  uint16_t pos = 0;
  payload[pos++] = 0xA0;  // Meta section: empty CBOR map.
  payload[pos++] = 0xB8;  // Main section: CBOR map with 24 pairs.
  payload[pos++] = 0x18;

  pos = appendCborUInt(payload, pos, 8);
  pos = appendCborUInt(payload, pos, 0);  // material_class = FFF
  pos = appendCborUInt(payload, pos, 9);
  pos = appendCborUInt(payload, pos, materialType);
  pos = appendCborUInt(payload, pos, 10);
  pos = appendCborText(payload, pos, materialName);
  pos = appendCborUInt(payload, pos, 11);
  pos = appendCborText(payload, pos, brandName);
  pos = appendCborUInt(payload, pos, 14);
  pos = appendCborUInt(payload, pos, manufacturedDate);
  pos = appendCborUInt(payload, pos, 16);
  pos = appendCborUInt(payload, pos, nominalWeight);
  pos = appendCborUInt(payload, pos, 17);
  pos = appendCborUInt(payload, pos, actualWeight);
  pos = appendCborUInt(payload, pos, 18);
  pos = appendCborUInt(payload, pos, emptyWeight);
  pos = appendCborUInt(payload, pos, 19);
  pos = appendCborColor(payload, pos, primaryColor);
  pos = appendCborUInt(payload, pos, 29);
  pos = appendCborFloat32(payload, pos, density);
  pos = appendCborUInt(payload, pos, 30);
  pos = appendCborFloat32(payload, pos, filamentDiameter);
  pos = appendCborUInt(payload, pos, 34);
  pos = appendCborUInt(payload, pos, minPrintTemp);
  pos = appendCborUInt(payload, pos, 35);
  pos = appendCborUInt(payload, pos, maxPrintTemp);
  pos = appendCborUInt(payload, pos, 37);
  pos = appendCborUInt(payload, pos, minBedTemp);
  pos = appendCborUInt(payload, pos, 38);
  pos = appendCborUInt(payload, pos, maxBedTemp);
  pos = appendCborUInt(payload, pos, 42);
  pos = appendCborUInt(payload, pos, spoolWidth);
  pos = appendCborUInt(payload, pos, 43);
  pos = appendCborUInt(payload, pos, spoolOuterDiameter);
  pos = appendCborUInt(payload, pos, 44);
  pos = appendCborUInt(payload, pos, spoolInnerDiameter);
  pos = appendCborUInt(payload, pos, 45);
  pos = appendCborUInt(payload, pos, spoolHoleDiameter);
  pos = appendCborUInt(payload, pos, 53);
  pos = appendCborUInt(payload, pos, nominalLength);
  pos = appendCborUInt(payload, pos, 54);
  pos = appendCborUInt(payload, pos, actualLength);
  pos = appendCborUInt(payload, pos, 55);
  pos = appendCborText(payload, pos, country);
  pos = appendCborUInt(payload, pos, 57);
  pos = appendCborUInt(payload, pos, dryingTemp);
  pos = appendCborUInt(payload, pos, 58);
  pos = appendCborUInt(payload, pos, dryingTime);

  return min<uint16_t>(pos, payloadSize);
}

uint16_t buildOpenPrintTagMemory(uint8_t *memory, uint16_t memorySize, const String &brandName,
                                 const String &materialName, uint8_t materialType, uint16_t minPrintTemp,
                                 uint16_t maxPrintTemp, uint16_t minBedTemp, uint16_t maxBedTemp,
                                 uint16_t dryingTemp, uint16_t dryingTime, const String &primaryColor,
                                 uint16_t nominalWeight, uint16_t actualWeight, uint16_t emptyWeight,
                                 float density, float filamentDiameter, uint32_t nominalLength,
                                 uint32_t actualLength, uint32_t manufacturedDate, const String &country,
                                 uint16_t spoolWidth, uint16_t spoolOuterDiameter, uint16_t spoolInnerDiameter,
                                 uint16_t spoolHoleDiameter) {
  memset(memory, 0, memorySize);

  uint8_t payload[220] = {0};
  const uint16_t payloadLength = buildOpenPrintTagPayload(payload, sizeof(payload), brandName, materialName,
                                                          materialType, minPrintTemp, maxPrintTemp, minBedTemp,
                                                          maxBedTemp, dryingTemp, dryingTime, primaryColor,
                                                          nominalWeight, actualWeight, emptyWeight, density,
                                                          filamentDiameter, nominalLength, actualLength,
                                                          manufacturedDate, country, spoolWidth,
                                                          spoolOuterDiameter, spoolInnerDiameter,
                                                          spoolHoleDiameter);

  memory[0] = 0xE1;  // NFC-V capability container.
  memory[1] = 0x40;
  memory[2] = 0x27;  // 312 bytes available after the CC record.
  memory[3] = 0x01;

  uint16_t pos = 4;
  const uint8_t ndefLength = 3 + sizeof(OPENPRINTTAG_MIME_TYPE) + payloadLength;
  memory[pos++] = 0x03;  // NDEF Message TLV.
  memory[pos++] = ndefLength;
  memory[pos++] = 0xD2;  // MB + ME + SR + TNF MIME media.
  memory[pos++] = sizeof(OPENPRINTTAG_MIME_TYPE);
  memory[pos++] = payloadLength;
  memcpy(&memory[pos], OPENPRINTTAG_MIME_TYPE, sizeof(OPENPRINTTAG_MIME_TYPE));
  pos += sizeof(OPENPRINTTAG_MIME_TYPE);
  memcpy(&memory[pos], payload, payloadLength);
  pos += payloadLength;
  memory[pos++] = 0xFE;  // TLV terminator.

  return pos;
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

bool writeOpenPrintTag(uint8_t materialType, const String &brandName, const String &materialName,
                       uint16_t minPrintTemp, uint16_t maxPrintTemp, uint16_t minBedTemp,
                       uint16_t maxBedTemp, uint16_t dryingTemp, uint16_t dryingTime,
                       const String &primaryColor, uint16_t nominalWeight, uint16_t actualWeight,
                       uint16_t emptyWeight, float density, float filamentDiameter, uint32_t nominalLength,
                       uint32_t actualLength, uint32_t manufacturedDate, const String &country,
                       uint16_t spoolWidth, uint16_t spoolOuterDiameter, uint16_t spoolInnerDiameter,
                       uint16_t spoolHoleDiameter) {
  resetNfc15693Rf();

  uint8_t uid[8] = {0};
  ISO15693ErrorCode rc = EC_NO_CARD;
  for (uint8_t attempt = 1; attempt <= NFC_READ_ATTEMPTS; attempt++) {
    rc = nfc15693.getInventory(uid);
    if (rc == ISO15693_EC_OK) {
      break;
    }

    resetNfc15693Rf();
    delay(NFC_RETRY_DELAY_MS);
  }

  if (rc != ISO15693_EC_OK) {
    clearNfcReadState();
    nfcData.lastStatus = "Scrittura annullata: nessun tag ISO15693";
    return false;
  }

  uint8_t memory[256] = {0};
  const uint16_t usedBytes = buildOpenPrintTagMemory(memory, sizeof(memory), brandName, materialName, materialType,
                                                     minPrintTemp, maxPrintTemp, minBedTemp, maxBedTemp,
                                                     dryingTemp, dryingTime, primaryColor, nominalWeight,
                                                     actualWeight, emptyWeight, density, filamentDiameter,
                                                     nominalLength, actualLength, manufacturedDate, country,
                                                     spoolWidth, spoolOuterDiameter, spoolInnerDiameter,
                                                     spoolHoleDiameter);
  const uint8_t blocksToWrite = (usedBytes + 3) / 4;

  for (uint8_t blockNo = 0; blockNo < blocksToWrite; blockNo++) {
    rc = nfc15693.writeSingleBlock(uid, blockNo, &memory[blockNo * 4], 4);
    if (rc != ISO15693_EC_OK) {
      nfcData.lastStatus = "Errore scrittura blocco " + String(blockNo);
      Serial.print("Errore scrittura ISO15693 blocco ");
      Serial.print(blockNo);
      Serial.print(": ");
      Serial.println(rc);
      return false;
    }
    delay(20);
  }

  nfcData.lastStatus = "OpenPrintTag scritto";
  Serial.print("OpenPrintTag scritto, blocchi: ");
  Serial.println(blocksToWrite);
  readIso15693Tag();
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
  sensorData.valid = !isnan(sensorData.temperature) && !isnan(sensorData.humidity);

  if (!sensorData.valid) {
    Serial.println("BME280: lettura non valida");
    return;
  }

  Serial.print("Temperatura: ");
  Serial.print(sensorData.temperature);
  Serial.print(" C, Umidita: ");
  Serial.print(sensorData.humidity);
  Serial.println(" %");
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

String settingsJson() {
  String json = "{";
  json += "\"selected\":";
  json += String(selectedMaterialProfile);
  json += ",\"setpoint\":";
  json += String(heaterControl.setpoint, 1);
  json += ",\"cycleDurationSeconds\":";
  json += String(heaterControl.cycleDurationSeconds);
  json += ",\"cycleRemainingSeconds\":";
  json += String(heaterControl.cycleRunning ? heaterControl.cycleRemainingSeconds : heaterControl.cycleDurationSeconds);
  json += ",\"cycleRunning\":";
  json += heaterControl.cycleRunning ? "true" : "false";
  json += ",\"safetyLimit\":";
  json += String(BED_SAFETY_LIMIT_C, 1);
  json += ",\"language\":\"";
  json += escapeJson(appSettings.language);
  json += "\",\"compiledLocale\":\"";
  json += UI_LOCALE_CODE;
  json += "\",\"compiledLocaleName\":\"";
  json += UI_LOCALE_NAME;
  json += "\",\"wifiSsid\":\"";
  json += escapeJson(appSettings.wifiSsid);
  json += "\",\"wifiConfigured\":";
  json += appSettings.wifiSsid.length() > 0 ? "true" : "false";
  json += ",\"wifiClientEnabled\":";
  json += appSettings.wifiClientEnabled ? "true" : "false";
  json += ",\"profiles\":[";

  for (uint8_t i = 0; i < totalMaterialProfileCount(); i++) {
    if (i > 0) {
      json += ",";
    }
    json += "{\"id\":";
    json += String(i);
    json += ",\"name\":\"";
    json += escapeJson(materialProfileName(i));
    json += "\",\"temperature\":";
    json += String(materialProfileTemp(i), 1);
    json += ",\"minutes\":";
    json += String(materialProfileMinutes(i));
    json += ",\"available\":";
    json += (materialProfileTemp(i) <= BED_SAFETY_LIMIT_C) ? "true" : "false";
    json += ",\"custom\":";
    json += (i >= MATERIAL_PROFILE_COUNT) ? "true" : "false";
    json += "}";
  }

  json += "]}";
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
  json += escapeJson(nfcData.dataFormat);
  json += "\",\"memoryDump\":\"";
  json += escapeJson(nfcData.memoryDump);
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
  heaterControl.cycleRunning = false;
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
  heaterControl.cycleEndMs = now + (heaterControl.cycleDurationSeconds * 1000UL);
  heaterControl.cycleRemainingSeconds = heaterControl.cycleDurationSeconds;
  heaterControl.cycleRunning = heaterControl.cycleDurationSeconds > 0;
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

  if (heaterControl.cycleRunning) {
    const long remainingMs = (long)(heaterControl.cycleEndMs - now);
    if (remainingMs <= 0) {
      heaterControl.cycleRemainingSeconds = 0;
      stopHeaterAutomation("Ciclo completato");
      return;
    }
    heaterControl.cycleRemainingSeconds = (remainingMs + 999L) / 1000L;
  }

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
  json += ",\"cycleDurationSeconds\":";
  json += String(heaterControl.cycleDurationSeconds);
  json += ",\"cycleRemainingSeconds\":";
  json += String(heaterControl.cycleRunning ? heaterControl.cycleRemainingSeconds : heaterControl.cycleDurationSeconds);
  json += ",\"cycleRunning\":";
  json += heaterControl.cycleRunning ? "true" : "false";
  json += ",\"safetyLimit\":";
  json += String(BED_SAFETY_LIMIT_C, 1);
  json += ",\"status\":\"";
  json += heaterControl.status;
  json += "\"";
  json += "}";
  return json;
}

String dashboardHtml(PageMode pageMode) {
  readSensor();
  readBedTemperature();

  String html;
  html.reserve(10000);

  html += F("<!doctype html><html lang=\"");
  html += appSettings.language;
  html += F("\"><head>");
  html += F("<meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>");
  html += tr(TK_APP_TITLE);
  html += F("</title>");
  html += F("<style>");
  html += F(":root{font-family:system-ui,-apple-system,Segoe UI,sans-serif;color:#1f2933;background:#f4f7f8}");
  html += F("body{margin:0;padding:24px}");
  html += F("main{max-width:1180px;margin:auto}");
  html += F("h1{font-size:32px;margin:0 0 6px}");
  html += F(".sub{color:#52606d;margin:0 0 24px}");
  html += F("nav{display:flex;gap:10px;margin:0 0 10px;align-items:center;flex-wrap:wrap}");
  html += F("nav a{border-radius:6px;background:#d9e2ec;color:#102a43;text-decoration:none;padding:9px 12px;font-weight:700}");
  html += F("nav a.active{background:#0f766e;color:white}");
  html += F(".tags .home-panel,.tags .heater-panel,.tags .setpoint-panel,.tags .settings-panel,.home .tag-panel,.home .heater-panel,.home .setpoint-panel,.home .settings-panel,.setpoint .home-panel,.setpoint .tag-panel,.setpoint .settings-panel,.settings .home-panel,.settings .tag-panel,.settings .heater-panel,.settings .setpoint-panel{display:none}");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px}");
  html += F(".card{background:white;border:1px solid #d9e2ec;border-radius:8px;padding:18px}");
  html += F(".wide{margin-top:14px}");
  html += F(".label{font-size:14px;color:#52606d;margin-bottom:10px}");
  html += F(".value{font-size:34px;font-weight:700;color:#102a43}");
  html += F(".small{font-size:16px;line-height:1.55;color:#102a43}");
  html += F(".formgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px;margin-top:16px}");
  html += F("label{display:grid;gap:6px;font-size:13px;color:#52606d;font-weight:700}");
  html += F("input,select{border:1px solid #bcccdc;border-radius:6px;padding:9px 10px;font:inherit;color:#102a43;background:white}");
  html += F("input[readonly]{background:#f0f4f8;color:#52606d}");
  html += F("input[type=color]{width:100%;height:42px;padding:3px;cursor:pointer}");
  html += F(".colorline{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center}");
  html += F(".hex{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;color:#102a43;background:#f0f4f8;border:1px solid #d9e2ec;border-radius:6px;padding:9px 10px}");
  html += F("code{display:block;margin-top:6px;white-space:pre-wrap;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:13px;line-height:1.45}");
  html += F("button{margin-top:14px;border:0;border-radius:6px;background:#0f766e;color:white;padding:10px 14px;font-size:15px;font-weight:700}");
  html += F("button.secondary{background:#334e68;margin-left:8px}");
  html += F("button.danger{background:#b91c1c}");
  html += F(".status{margin-top:18px;color:#52606d;font-size:14px}");
  html += F(".warn{color:#b91c1c;font-weight:700}.ok{color:#0f766e;font-weight:700}");
  html += F("table{width:100%;border-collapse:collapse;margin-top:12px}th,td{text-align:left;border-bottom:1px solid #d9e2ec;padding:9px 6px}th{font-size:13px;color:#52606d}");
  html += F(".inline-control{display:flex;gap:8px;align-items:end;flex-wrap:wrap;margin-top:12px}.inline-control label{min-width:190px}.inline-control button{margin-top:0}");
  html += F(".profile-control{display:grid;grid-template-columns:1.2fr 1fr 1fr auto auto;gap:14px;align-items:end;margin-top:18px}");
  html += F(".profile-control label{font-size:15px;min-width:0}.profile-control select,.profile-control input,.profile-control .hex{height:46px;font-size:18px;font-weight:800;box-sizing:border-box}.profile-control button{height:46px;margin-top:0;padding:0 18px}");
  html += F(".heater-summary{display:flex;gap:22px;align-items:center;flex-wrap:wrap;margin:4px 0 12px}.metric{display:grid;gap:3px}.metric span{font-size:13px;color:#52606d;font-weight:700}.metric strong{font-size:22px;color:#102a43}");
  html += F(".button-row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:12px}.button-row button{margin-top:0}.output-inline{margin-left:auto;color:#52606d;font-size:14px}.output-inline strong{color:#102a43}");
  html += F(".cycle-timer{display:inline-block;margin:10px 0 6px;padding:12px 18px;border-radius:8px;background:#102a43;color:white;font-size:42px;font-weight:800;line-height:1;font-variant-numeric:tabular-nums;letter-spacing:0}");
  html += F(".cycle-caption{font-size:13px;color:#52606d;font-weight:700;margin-top:12px}");
  html += F("</style>");
  html += F("<script>");
  html += F("async function refreshData(){try{const r=await fetch('/api/bme280');const d=await r.json();");
  html += F("if(d.valid){document.getElementById('temperature').textContent=d.temperature.toFixed(1)+' C';");
  html += F("document.getElementById('humidity').textContent=d.humidity.toFixed(1)+' %';}");
  html += F("}catch(e){}}");
  html += F("async function refreshBed(){try{const r=await fetch('/api/bed');const d=await r.json();");
  html += F("document.getElementById('bed-temp').textContent=d.valid?d.temperature.toFixed(1)+' C':'--';}catch(e){}}");
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
  html += F("document.getElementById('pn5180-format').textContent=d.dataFormat;}");
  html += F("async function refreshNfc(){try{const r=await fetch('/api/nfc');updateNfc(await r.json());}catch(e){}}");
  html += F("async function readTag(){const b=document.getElementById('read-tag');b.disabled=true;b.textContent='Lettura...';");
  html += F("let done=false;const t=setTimeout(()=>{if(!done){b.disabled=false;b.textContent='Leggi tag';document.getElementById('pn5180-scan-status').textContent='Lettura senza risposta: premi EN/RST se il portale resta fermo';}},3000);");
  html += F("try{const r=await fetch('/api/nfc/read?ts='+Date.now());done=true;clearTimeout(t);updateNfc(await r.json());}catch(e){done=true;clearTimeout(t);document.getElementById('pn5180-scan-status').textContent='Errore richiesta lettura';}");
  html += F("b.disabled=false;b.textContent='Leggi tag';}");
  html += F("async function writeOpenPrintTag(){if(!confirm('Scrivo questi dati sul tag appoggiato. Continuo?'))return;");
  html += F("const b=document.getElementById('write-tag');const f=document.getElementById('tag-form');const q=new URLSearchParams(new FormData(f));");
  html += F("const md=f.elements['mdate'].value;if(md){q.set('mdate',Math.floor(new Date(md+'T00:00:00Z').getTime()/1000));}");
  html += F("b.disabled=true;b.textContent='Scrittura...';");
  html += F("try{const r=await fetch('/api/nfc/write?'+q.toString()+'&ts='+Date.now());updateNfc(await r.json());}catch(e){document.getElementById('pn5180-scan-status').textContent='Errore scrittura';}");
  html += F("b.disabled=false;b.textContent='Scrivi OpenPrintTag';}");
  html += F("function syncColor(){const c=document.getElementById('tag-color');const h=document.getElementById('tag-color-hex');if(c&&h)h.textContent=c.value.toUpperCase();}");
  html += F("function updateHeater(d){document.getElementById('heater-state').textContent=d.enabled?'ON':'OFF';");
  html += F("document.getElementById('heater-mode').textContent=d.automatic?'AUTO':'Manuale';");
  html += F("document.getElementById('heater-setpoint').textContent=d.setpoint.toFixed(1)+' C';");
  html += F("document.getElementById('heater-output').textContent=d.outputPercent.toFixed(1)+' %';");
  html += F("const c=document.getElementById('cycle-countdown');if(c)c.textContent=formatDuration(d.cycleRemainingSeconds);");
  html += F("const ht=document.getElementById('home-profile-temp');if(ht&&document.activeElement!==ht&&!d.automatic&&!manualHomeEdit)ht.value=d.setpoint.toFixed(1);");
  html += F("const pt=document.getElementById('profile-temp');if(pt&&document.activeElement!==pt&&!d.automatic)pt.value=d.setpoint.toFixed(1);");
  html += F("const hm=document.getElementById('home-cycle-minutes');if(hm&&document.activeElement!==hm&&!d.automatic&&!manualHomeEdit)hm.value=Math.round(d.cycleDurationSeconds/60);");
  html += F("const pm=document.getElementById('profile-minutes');if(pm&&document.activeElement!==pm&&!d.automatic)pm.value=Math.round(d.cycleDurationSeconds/60);");
  html += F("const hp=document.getElementById('home-profile');if(hp)hp.disabled=d.automatic;}");
  html += F("function formatDuration(s){s=Number(s)||0;const h=Math.floor(s/3600);const m=Math.floor((s%3600)/60);const sec=s%60;return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sec).padStart(2,'0');}");
  html += F("async function refreshHeater(){try{const r=await fetch('/api/heater');updateHeater(await r.json());}catch(e){}}");
  html += F("async function setHeaterState(v){try{const r=await fetch('/api/heater/'+(v?'on':'off')+'?ts='+Date.now());updateHeater(await r.json());}catch(e){document.getElementById('heater-state').textContent='Errore';}}");
  html += F("async function setHeaterAuto(v){try{if(v&&!(await applyHomeProfile(false)))return;const r=await fetch('/api/heater/auto/'+(v?'on':'off')+'?ts='+Date.now());updateHeater(await r.json());}catch(e){const n=document.getElementById('home-profile-note');if(n){n.className='warn';n.textContent='Errore comando AUTO';}}}");
  html += F("let manualHomeEdit=false;");
  html += F("const profiles=[");
  for (uint8_t i = 0; i < totalMaterialProfileCount(); i++) {
    if (i > 0) {
      html += F(",");
    }
    html += F("[\"");
    html += escapeJson(materialProfileName(i));
    html += F("\",");
    html += String(materialProfileTemp(i), 1);
    html += F(",");
    html += String(materialProfileMinutes(i));
    html += F("]");
  }
  html += F("];");
  html += F("const safetyLimit=");
  html += String(BED_SAFETY_LIMIT_C, 1);
  html += F(";");
  html += F("function readNumber(id,fallback){const e=document.getElementById(id);if(!e)return fallback;const v=String(e.value).replace(',','.');const n=parseFloat(v);return Number.isFinite(n)?n:fallback;}");
  html += F("function selectedProfile(){return profiles[Number(document.getElementById('material-profile').value)||0];}");
  html += F("function syncSetpointWarning(){const p=selectedProfile();const v=readNumber('profile-temp',p[1]);const n=document.getElementById('profile-note');");
  html += F("if(v>safetyLimit){n.className='warn';n.textContent='Sopra il limite sicurezza attuale: non applicabile.';return false;}");
  html += F("if(v>p[1]){n.className='warn';n.textContent='Temperatura superiore al profilo consigliato.';return true;}");
  html += F("n.className='ok';n.textContent='Valore nel profilo consigliato.';return true;}");
  html += F("function fillProfile(){const p=selectedProfile();document.getElementById('profile-recommended').textContent=p[1].toFixed(1)+' C / '+Math.round(p[2]/60)+' h';syncSetpointWarning();}");
  html += F("function loadRecommendedProfile(){const p=selectedProfile();document.getElementById('profile-temp').value=p[1];document.getElementById('profile-minutes').value=p[2];syncSetpointWarning();}");
  html += F("async function applySetpoint(){if(!syncSetpointWarning())return;const p=selectedProfile();const v=readNumber('profile-temp',p[1]);");
  html += F("if(v>p[1]&&!confirm('Temperatura superiore al profilo consigliato. Procedo comunque?'))return;");
  html += F("const minutes=Math.round(readNumber('profile-minutes',p[2]));");
  html += F("const r=await fetch('/api/settings/material?id='+document.getElementById('material-profile').value+'&setpoint='+v+'&minutes='+minutes+'&ts='+Date.now());const d=await r.json();document.getElementById('settings-status').textContent='Setpoint aggiornato a '+d.setpoint.toFixed(1)+' C, durata '+Math.round(d.cycleDurationSeconds/60)+' min';refreshHeater();}");
  html += F("async function addCustomProfile(ev){if(ev)ev.preventDefault();const name=document.getElementById('custom-profile-name').value.trim();const temp=readNumber('custom-profile-temp',NaN);const minutes=Math.round(readNumber('custom-profile-minutes',NaN));const s=document.getElementById('settings-status');");
  html += F("s.className='status';if(!name){s.textContent='Nome profilo mancante.';return;}if(!Number.isFinite(temp)||!Number.isFinite(minutes)||minutes<1){s.textContent='Temperatura o durata non validi.';return;}if(temp>safetyLimit){s.className='warn';s.textContent='Temperatura oltre il limite sicurezza.';return;}s.textContent='Aggiungo profilo...';");
  html += F("try{const q=new URLSearchParams({name:name,setpoint:temp,minutes:minutes,ts:Date.now()});const r=await fetch('/api/settings/profile/add?'+q.toString());const t=await r.text();if(!r.ok){s.className='warn';s.textContent='Profilo non aggiunto: '+t;return;}location.href='/settings?ts='+Date.now();}catch(e){s.className='warn';s.textContent='Errore rete mentre aggiungevo il profilo.';}}");
  html += F("async function saveGeneralSettings(){const s=document.getElementById('general-settings-status');s.className='status';s.textContent='Salvo impostazioni...';");
  html += F("const q=new URLSearchParams({language:document.getElementById('ui-language').value,wifiSsid:document.getElementById('wifi-ssid').value,wifiPassword:document.getElementById('wifi-password').value,wifiClient:document.getElementById('wifi-client').value,ts:Date.now()});");
  html += F("try{const r=await fetch('/api/settings/general?'+q.toString());if(!r.ok){s.className='warn';s.textContent='Impostazioni non salvate.';return;}s.className='ok';s.textContent='Impostazioni salvate. Il WiFi operativo resta in modalita AP per ora.';}catch(e){s.className='warn';s.textContent='Errore rete durante il salvataggio.';}}");
  html += F("function setManualHomeEdit(v){manualHomeEdit=v;const t=document.getElementById('home-profile-temp');const m=document.getElementById('home-cycle-minutes');const b=document.getElementById('home-manual-edit');if(t)t.readOnly=!v;if(m)m.readOnly=!v;if(b)b.textContent=v?'Blocca modifica':'Modifica manuale';}");
  html += F("function toggleManualHomeEdit(){setManualHomeEdit(!manualHomeEdit);const n=document.getElementById('home-profile-note');if(n&&manualHomeEdit){n.className='ok';n.textContent='Modifica manuale attiva: puoi cambiare temperatura e durata.';}}");
  html += F("function fillHomeProfile(){const s=document.getElementById('home-profile');if(!s)return;const p=profiles[Number(s.value)||0];const n=document.getElementById('home-profile-note');");
  html += F("if(!manualHomeEdit){document.getElementById('home-profile-temp').value=p[1].toFixed(1);document.getElementById('home-cycle-minutes').value=p[2];}");
  html += F("document.getElementById('home-profile-recommended').textContent=p[1].toFixed(1)+' C / '+Math.round(p[2]/60)+' h';");
  html += F("if(p[1]>safetyLimit){n.className='warn';n.textContent='Profilo oltre il limite sicurezza attuale.';}else{n.className='ok';n.textContent='Profilo applicabile.';}}");
  html += F("async function applyHomeProfile(showMessage=true){const s=document.getElementById('home-profile');const p=profiles[Number(s.value)||0];");
  html += F("const setpoint=readNumber('home-profile-temp',p[1]);");
  html += F("if(setpoint>safetyLimit){document.getElementById('home-profile-note').className='warn';document.getElementById('home-profile-note').textContent='Non applicato: oltre il limite sicurezza.';return;}");
  html += F("const minutes=Math.round(readNumber('home-cycle-minutes',p[2]));");
  html += F("const r=await fetch('/api/settings/material?id='+s.value+'&setpoint='+setpoint+'&minutes='+minutes+'&ts='+Date.now());const d=await r.json();document.getElementById('home-profile-note').className='ok';document.getElementById('home-profile-note').textContent=showMessage?('Ciclo '+p[0]+' applicato: '+d.setpoint.toFixed(1)+' C, '+Math.round(d.cycleDurationSeconds/60)+' min'):'Ciclo '+p[0]+' pronto';document.getElementById('heater-setpoint').textContent=d.setpoint.toFixed(1)+' C';document.getElementById('cycle-countdown').textContent=formatDuration(d.cycleRemainingSeconds);refreshHeater();return true;}");
  html += F("setInterval(()=>{refreshData();refreshBed();},60000);");
  html += F("setInterval(()=>{refreshBed();refreshHeater();},2000);");
  html += F("window.addEventListener('load',()=>{syncColor();if(document.getElementById('material-profile'))fillProfile();fillHomeProfile();refreshData();refreshBed();refreshNfc();refreshHeater();});");
  html += F("</script>");
  html += pageMode == PAGE_TAGS ? F("</head><body class=\"tags\"><main>") :
          (pageMode == PAGE_SETPOINT ? F("</head><body class=\"setpoint\"><main>") :
           (pageMode == PAGE_SETTINGS ? F("</head><body class=\"settings\"><main>") : F("</head><body class=\"home\"><main>")));
  html += F("<h1>");
  html += tr(TK_APP_TITLE);
  html += F("</h1>");
  html += F("<p class=\"sub\">");
  html += tr(TK_APP_SUBTITLE);
  html += F("</p>");
  html += F("<nav><a href=\"/temperature\" title=\"");
  html += tr(TK_NAV_HOME_TIP);
  html += F("\"");
  html += pageMode == PAGE_HOME ? F(" class=\"active\">") : F(">");
  html += tr(TK_NAV_HOME);
  html += F("</a>");
  html += F("<a href=\"/tags\" title=\"");
  html += tr(TK_NAV_TAG_TIP);
  html += F("\"");
  html += pageMode == PAGE_TAGS ? F(" class=\"active\">") : F(">");
  html += tr(TK_NAV_TAG);
  html += F("</a>");
  html += F("<a href=\"/setpoint\" title=\"");
  html += tr(TK_NAV_SETPOINT_TIP);
  html += F("\"");
  html += pageMode == PAGE_SETPOINT ? F(" class=\"active\">") : F(">");
  html += tr(TK_NAV_SETPOINT);
  html += F("</a>");
  html += F("<a href=\"/settings\" title=\"");
  html += tr(TK_NAV_SETTINGS_TIP);
  html += F("\"");
  html += pageMode == PAGE_SETTINGS ? F(" class=\"active\">") : F(">");
  html += tr(TK_NAV_SETTINGS);
  html += F("</a></nav>");
  html += F("<section class=\"grid home-panel\">");
  html += F("<article class=\"card\"><div class=\"label\">Temperatura</div><div class=\"value\" id=\"temperature\">");
  html += valueOrDash(sensorData.temperature, " C");
  html += F("</div></article>");
  html += F("<article class=\"card\"><div class=\"label\">Umidita</div><div class=\"value\" id=\"humidity\">");
  html += valueOrDash(sensorData.humidity, " %");
  html += F("</div></article>");
  html += F("<article class=\"card\"><div class=\"label\">Piatto</div><div class=\"value\" id=\"bed-temp\">");
  html += valueOrDash(bedData.temperature, " C");
  html += F("</div></article>");
  html += F("</section>");
  html += F("<section class=\"card wide tag-panel\"><div class=\"label\">PN5180</div><div class=\"small\">");
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
  html += F("</span><br><button id=\"read-tag\" onclick=\"readTag()\">Leggi tag</button>");
  html += F("<form id=\"tag-form\" class=\"formgrid\" onsubmit=\"event.preventDefault();writeOpenPrintTag();\">");
  html += F("<label>Brand<input name=\"brand\" maxlength=\"31\" value=\"Essiccatore\"></label>");
  html += F("<label>Materiale<input name=\"name\" maxlength=\"31\" value=\"Test PLA\"></label>");
  html += F("<label>Tipo<select name=\"type\"><option value=\"0\">PLA</option><option value=\"1\">PETG</option><option value=\"2\">TPU</option><option value=\"3\">ABS</option><option value=\"4\">ASA</option><option value=\"5\">PC</option></select></label>");
  html += F("<label>Colore<div class=\"colorline\"><input id=\"tag-color\" name=\"color\" type=\"color\" value=\"#ffffff\" oninput=\"syncColor()\"><span class=\"hex\" id=\"tag-color-hex\">#FFFFFF</span></div></label>");
  html += F("<label>Peso nominale g<input name=\"nomw\" type=\"number\" min=\"0\" max=\"9999\" value=\"1000\"></label>");
  html += F("<label>Peso reale g<input name=\"actw\" type=\"number\" min=\"0\" max=\"9999\" value=\"1000\"></label>");
  html += F("<label>Bobina vuota g<input name=\"emptyw\" type=\"number\" min=\"0\" max=\"9999\" value=\"250\"></label>");
  html += F("<label>Densita g/cm3<input name=\"density\" type=\"number\" min=\"0\" max=\"5\" step=\"0.01\" value=\"1.24\"></label>");
  html += F("<label>Diametro filo mm<input name=\"diameter\" type=\"number\" min=\"0\" max=\"5\" step=\"0.01\" value=\"1.75\"></label>");
  html += F("<label>Lunghezza nominale mm<input name=\"nomlen\" type=\"number\" min=\"0\" max=\"999999\" value=\"330000\"></label>");
  html += F("<label>Lunghezza reale mm<input name=\"actlen\" type=\"number\" min=\"0\" max=\"999999\" value=\"330000\"></label>");
  html += F("<label>Data produzione<input name=\"mdate\" type=\"date\"></label>");
  html += F("<label>Paese origine<input name=\"country\" maxlength=\"2\" value=\"IT\"></label>");
  html += F("<label>Nozzle min C<input name=\"nmin\" type=\"number\" min=\"0\" max=\"350\" value=\"205\"></label>");
  html += F("<label>Nozzle max C<input name=\"nmax\" type=\"number\" min=\"0\" max=\"350\" value=\"215\"></label>");
  html += F("<label>Piatto min C<input name=\"bmin\" type=\"number\" min=\"0\" max=\"140\" value=\"55\"></label>");
  html += F("<label>Piatto max C<input name=\"bmax\" type=\"number\" min=\"0\" max=\"140\" value=\"60\"></label>");
  html += F("<label>Essiccazione C<input name=\"drytemp\" type=\"number\" min=\"0\" max=\"120\" value=\"45\"></label>");
  html += F("<label>Essiccazione min<input name=\"drytime\" type=\"number\" min=\"0\" max=\"9999\" value=\"240\"></label>");
  html += F("<label>Larghezza bobina mm<input name=\"spoolw\" type=\"number\" min=\"0\" max=\"999\" value=\"68\"></label>");
  html += F("<label>Diametro esterno mm<input name=\"spoolod\" type=\"number\" min=\"0\" max=\"999\" value=\"200\"></label>");
  html += F("<label>Diametro interno mm<input name=\"spoolid\" type=\"number\" min=\"0\" max=\"999\" value=\"100\"></label>");
  html += F("<label>Foro centrale mm<input name=\"spoolhole\" type=\"number\" min=\"0\" max=\"999\" value=\"52\"></label>");
  html += F("</form><button id=\"write-tag\" onclick=\"writeOpenPrintTag()\">Scrivi OpenPrintTag</button>");
  html += F("</div></section>");
  html += F("<section class=\"card wide heater-panel\"><div class=\"label\">Riscaldatore</div><div class=\"small\">");
  html += F("<div class=\"heater-summary\"><div class=\"metric\"><span>Stato</span><strong id=\"heater-state\">");
  html += heaterEnabled ? "ON" : "OFF";
  html += F("</strong></div><div class=\"metric\"><span>Modo</span><strong id=\"heater-mode\">");
  html += heaterControl.automatic ? "AUTO" : "Manuale";
  html += F("</strong></div><div class=\"metric\"><span>Setpoint</span><strong id=\"heater-setpoint\">");
  html += String(heaterControl.setpoint, 1);
  html += F(" C</strong></div></div>");
  html += F("<div class=\"profile-control\"><label>Profilo materiale<select id=\"home-profile\" onchange=\"fillHomeProfile()\">");
  for (uint8_t i = 0; i < totalMaterialProfileCount(); i++) {
    html += F("<option value=\"");
    html += String(i);
    html += F("\"");
    if (i == selectedMaterialProfile) {
      html += F(" selected");
    }
    html += F(">");
    html += materialProfileName(i);
    html += F("</option>");
  }
  html += F("</select></label><label>Setpoint ciclo C<input id=\"home-profile-temp\" type=\"number\" min=\"20\" max=\"120\" step=\"0.5\" readonly value=\"");
  html += String(heaterControl.setpoint, 1);
  html += F("\"></label><label>Durata ciclo min<input id=\"home-cycle-minutes\" type=\"number\" min=\"1\" max=\"9999\" step=\"1\" readonly value=\"");
  html += String(heaterControl.cycleDurationSeconds / 60UL);
  html += F("\"></label><button type=\"button\" onclick=\"applyHomeProfile()\">Applica ciclo</button><button type=\"button\" class=\"secondary\" id=\"home-manual-edit\" onclick=\"toggleManualHomeEdit()\">Modifica manuale</button></div>");
  html += F("<p class=\"status\">Consigliato: <span id=\"home-profile-recommended\">--</span></p>");
  html += F("<p class=\"status\" id=\"home-profile-note\">--</p>");
  html += F("<div class=\"cycle-caption\">Countdown ciclo</div><div class=\"cycle-timer\" id=\"cycle-countdown\">--:--:--</div>");
  html += F("<div class=\"button-row\"><button onclick=\"setHeaterAuto(true)\">AUTO</button>");
  html += F("<button class=\"secondary\" onclick=\"setHeaterAuto(false)\">STOP AUTO</button>");
  html += F("<button class=\"danger\" onclick=\"setHeaterState(true)\">ON manuale</button>");
  html += F("<button class=\"secondary\" onclick=\"setHeaterState(false)\">OFF</button>");
  html += F("<span class=\"output-inline\">Output <strong id=\"heater-output\">");
  html += String(heaterControl.outputPercent, 1);
  html += F(" %</strong></span></div>");
  html += F("</div></section>");
  html += F("<section class=\"card wide setpoint-panel\"><div class=\"label\">Impostazioni materiale</div><div class=\"small\">");
  html += F("<div class=\"formgrid\"><label>Materiale<select id=\"material-profile\" onchange=\"loadRecommendedProfile()\">");
  for (uint8_t i = 0; i < totalMaterialProfileCount(); i++) {
    html += F("<option value=\"");
    html += String(i);
    html += F("\"");
    if (i == selectedMaterialProfile) {
      html += F(" selected");
    }
    html += F(">");
    html += materialProfileName(i);
    html += F("</option>");
  }
  html += F("</select></label>");
  html += F("<label>Setpoint essiccatore C<input id=\"profile-temp\" type=\"number\" min=\"20\" max=\"120\" step=\"0.5\" value=\"");
  html += String(heaterControl.setpoint, 1);
  html += F("\" oninput=\"syncSetpointWarning()\"></label>");
  html += F("<label>Durata ciclo min<input id=\"profile-minutes\" type=\"number\" min=\"1\" max=\"9999\" step=\"1\" value=\"");
  html += String(heaterControl.cycleDurationSeconds / 60UL);
  html += F("\"></label><label>Profilo consigliato<div class=\"hex\" id=\"profile-recommended\">--</div></label></div>");
  html += F("<p class=\"status\" id=\"profile-note\">--</p>");
  html += F("<button type=\"button\" onclick=\"applySetpoint()\">Applica setpoint</button>");
  html += F("<p class=\"status\" id=\"settings-status\">Scegli un materiale per caricare setpoint e durata consigliata.</p>");
  html += F("<div class=\"label\" style=\"margin-top:24px\">Nuovo profilo personalizzato</div>");
  html += F("<div class=\"formgrid\"><label>Nome profilo<input id=\"custom-profile-name\" maxlength=\"24\" placeholder=\"PLA 0.5KG\"></label>");
  html += F("<label>Setpoint C<input id=\"custom-profile-temp\" type=\"number\" min=\"20\" max=\"120\" step=\"0.5\" value=\"45\"></label>");
  html += F("<label>Durata min<input id=\"custom-profile-minutes\" type=\"number\" min=\"1\" max=\"9999\" step=\"1\" value=\"180\"></label>");
  html += F("<label>&nbsp;<button type=\"button\" onclick=\"addCustomProfile(event)\">Aggiungi profilo</button></label></div>");
  html += F("<table><thead><tr><th>Materiale</th><th>Temp.</th><th>Tempo</th><th>Stato</th></tr></thead><tbody>");
  for (uint8_t i = 0; i < totalMaterialProfileCount(); i++) {
    const bool available = materialProfileTemp(i) <= BED_SAFETY_LIMIT_C;
    html += F("<tr><td>");
    html += materialProfileName(i);
    html += F("</td><td>");
    html += String(materialProfileTemp(i), 0);
    html += F(" C</td><td>");
    html += String(materialProfileMinutes(i) / 60.0F, 1);
    html += F(" h</td><td class=\"");
    html += (available ? F("ok") : F("warn"));
    html += F("\">");
    html += (available ? F("Applicabile") : F("Oltre limite sicurezza"));
    html += F("</td></tr>");
  }
  html += F("</tbody></table>");
  html += F("</div></section>");
  html += F("<section class=\"card wide settings-panel\"><div class=\"label\">");
  html += tr(TK_SETTINGS_NETWORK);
  html += F("</div><div class=\"small\">");
  html += F("<div class=\"formgrid\"><label>");
  html += tr(TK_SETTINGS_LANGUAGE);
  html += F("<select id=\"ui-language\"><option value=\"");
  html += UI_LOCALE_CODE;
  html += F("\"");
  html += appSettings.language == UI_LOCALE_CODE ? F(" selected") : F("");
  html += F(">");
  html += UI_LOCALE_NAME;
  html += F("</option><option value=\"en\"");
  html += appSettings.language == "en" ? F(" selected") : F("");
  html += F(">English</option></select></label>");
  html += F("<label>");
  html += tr(TK_SETTINGS_WIFI_SSID);
  html += F("<input id=\"wifi-ssid\" maxlength=\"32\" value=\"");
  html += escapeHtml(appSettings.wifiSsid);
  html += F("\"></label>");
  html += F("<label>");
  html += tr(TK_SETTINGS_WIFI_PASSWORD);
  html += F("<input id=\"wifi-password\" type=\"password\" maxlength=\"64\" value=\"");
  html += escapeHtml(appSettings.wifiPassword);
  html += F("\"></label>");
  html += F("<label>");
  html += tr(TK_SETTINGS_WIFI_MODE);
  html += F("<select id=\"wifi-client\"><option value=\"0\"");
  html += appSettings.wifiClientEnabled ? F("") : F(" selected");
  html += F(">");
  html += tr(TK_SETTINGS_WIFI_DISABLED);
  html += F("</option><option value=\"1\"");
  html += appSettings.wifiClientEnabled ? F(" selected") : F("");
  html += F(">");
  html += tr(TK_SETTINGS_WIFI_SAVED);
  html += F("</option></select></label></div>");
  html += F("<div class=\"status\"><strong>");
  html += tr(TK_FIRMWARE_LOCALE);
  html += F(":</strong> ");
  html += UI_LOCALE_NAME;
  html += F(" / <strong>");
  html += tr(TK_FALLBACK_LOCALE);
  html += F(":</strong> English</div>");
  html += F("<button type=\"button\" onclick=\"saveGeneralSettings()\">");
  html += tr(TK_SETTINGS_SAVE);
  html += F("</button>");
  html += F("<p class=\"status\" id=\"general-settings-status\">");
  html += tr(TK_SETTINGS_NOTE);
  html += F("</p>");
  html += F("</div></section>");
  html += F("</main></body></html>");

  return html;
}

void handleRoot() {
  Serial.println("HTTP GET /");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", dashboardHtml(PAGE_HOME));
}

void handleTemperaturePage() {
  Serial.println("HTTP GET /temperature");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", dashboardHtml(PAGE_HOME));
}

void handleTagsPage() {
  Serial.println("HTTP GET /tags");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", dashboardHtml(PAGE_TAGS));
}

void handleSetpointPage() {
  Serial.println("HTTP GET /setpoint");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", dashboardHtml(PAGE_SETPOINT));
}

void handleSettingsPage() {
  Serial.println("HTTP GET /settings");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", dashboardHtml(PAGE_SETTINGS));
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

void handleApiSettingsMaterial() {
  Serial.println("HTTP GET /api/settings/material");
  if (server.hasArg("id")) {
    const int profileId = server.arg("id").toInt();
    if (profileId >= 0 && profileId < totalMaterialProfileCount()) {
      selectedMaterialProfile = (uint8_t)profileId;
      heaterControl.cycleDurationSeconds = (uint32_t)materialProfileMinutes(selectedMaterialProfile) * 60UL;
      if (!heaterControl.cycleRunning) {
        heaterControl.cycleRemainingSeconds = heaterControl.cycleDurationSeconds;
      }
    }
  }

  if (server.hasArg("setpoint")) {
    float requestedSetpoint = server.arg("setpoint").toFloat();
    requestedSetpoint = constrain(requestedSetpoint, 20.0F, BED_SAFETY_LIMIT_C);
    heaterControl.setpoint = requestedSetpoint;
    heaterControl.integral = 0.0F;
  }

  if (server.hasArg("minutes")) {
    const uint32_t requestedMinutes = constrain((uint32_t)server.arg("minutes").toInt(), (uint32_t)1, (uint32_t)9999);
    heaterControl.cycleDurationSeconds = requestedMinutes * 60UL;
    if (!heaterControl.cycleRunning) {
      heaterControl.cycleRemainingSeconds = heaterControl.cycleDurationSeconds;
    }
  }

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", settingsJson());
}

void handleApiSettingsProfileAdd() {
  Serial.println("HTTP GET /api/settings/profile/add");
  if (!server.hasArg("name") || !server.hasArg("setpoint") || !server.hasArg("minutes")) {
    server.send(400, "application/json", "{\"error\":\"missing_args\"}");
    return;
  }

  uint8_t newProfileId = 0;
  const float requestedSetpoint = server.arg("setpoint").toFloat();
  const uint16_t requestedMinutes = constrain((uint16_t)server.arg("minutes").toInt(), (uint16_t)1, (uint16_t)9999);

  if (isnan(requestedSetpoint) || requestedSetpoint < 20.0F || requestedSetpoint > BED_SAFETY_LIMIT_C ||
      requestedMinutes < 1) {
    server.send(400, "application/json", "{\"error\":\"invalid_args\"}");
    return;
  }

  if (!addCustomMaterialProfile(server.arg("name"), requestedSetpoint, requestedMinutes, newProfileId)) {
    server.send(400, "application/json", "{\"error\":\"profile_not_added\"}");
    return;
  }
  saveCustomProfiles();

  selectedMaterialProfile = newProfileId;
  heaterControl.setpoint = materialProfileTemp(selectedMaterialProfile);
  heaterControl.integral = 0.0F;
  heaterControl.cycleDurationSeconds = (uint32_t)materialProfileMinutes(selectedMaterialProfile) * 60UL;
  if (!heaterControl.cycleRunning) {
    heaterControl.cycleRemainingSeconds = heaterControl.cycleDurationSeconds;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", settingsJson());
}

void handleApiSettingsGeneral() {
  Serial.println("HTTP GET /api/settings/general");

  if (server.hasArg("language")) {
    const String language = server.arg("language");
    appSettings.language = (language == "en") ? "en" : UI_LOCALE_CODE;
  }

  if (server.hasArg("wifiSsid")) {
    appSettings.wifiSsid = server.arg("wifiSsid");
    appSettings.wifiSsid.trim();
    if (appSettings.wifiSsid.length() > 32) {
      appSettings.wifiSsid = appSettings.wifiSsid.substring(0, 32);
    }
  }

  if (server.hasArg("wifiPassword")) {
    appSettings.wifiPassword = server.arg("wifiPassword");
    if (appSettings.wifiPassword.length() > 64) {
      appSettings.wifiPassword = appSettings.wifiPassword.substring(0, 64);
    }
  }

  if (server.hasArg("wifiClient")) {
    appSettings.wifiClientEnabled = server.arg("wifiClient") == "1";
  }

  saveAppSettings();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", settingsJson());
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

void handleApiNfcWriteTest() {
  Serial.println("HTTP /api/nfc/write");
  const uint8_t materialType = constrain(server.arg("type").toInt(), 0, 255);
  String brandName = server.arg("brand");
  String materialName = server.arg("name");
  if (brandName.length() == 0) {
    brandName = "Essiccatore";
  }
  if (materialName.length() == 0) {
    materialName = "Test PLA";
  }

  writeOpenPrintTag(materialType, brandName, materialName, constrain(server.arg("nmin").toInt(), 0, 350),
                    constrain(server.arg("nmax").toInt(), 0, 350), constrain(server.arg("bmin").toInt(), 0, 140),
                    constrain(server.arg("bmax").toInt(), 0, 140), constrain(server.arg("drytemp").toInt(), 0, 120),
                    constrain(server.arg("drytime").toInt(), 0, 9999), server.arg("color"),
                    constrain(server.arg("nomw").toInt(), 0, 9999), constrain(server.arg("actw").toInt(), 0, 9999),
                    constrain(server.arg("emptyw").toInt(), 0, 9999), constrain(server.arg("density").toFloat(), 0.0F, 5.0F),
                    constrain(server.arg("diameter").toFloat(), 0.0F, 5.0F),
                    constrain((uint32_t)server.arg("nomlen").toInt(), (uint32_t)0, (uint32_t)999999),
                    constrain((uint32_t)server.arg("actlen").toInt(), (uint32_t)0, (uint32_t)999999),
                    server.arg("mdate").toInt() > 0 ? (uint32_t)server.arg("mdate").toInt() : 0, server.arg("country"),
                    constrain(server.arg("spoolw").toInt(), 0, 999), constrain(server.arg("spoolod").toInt(), 0, 999),
                    constrain(server.arg("spoolid").toInt(), 0, 999), constrain(server.arg("spoolhole").toInt(), 0, 999));
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
  loadAppSettings();
  loadCustomProfiles();
  Serial.print("Profili personalizzati caricati: ");
  Serial.println(customProfileCount);

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
  server.on("/temperature", handleTemperaturePage);
  server.on("/tags", handleTagsPage);
  server.on("/setpoint", handleSetpointPage);
  server.on("/settings", handleSettingsPage);
  server.on("/api/bme280", handleApiBme280);
  server.on("/api/bed", HTTP_GET, handleApiBed);
  server.on("/api/settings/material", HTTP_GET, handleApiSettingsMaterial);
  server.on("/api/settings/profile/add", HTTP_GET, handleApiSettingsProfileAdd);
  server.on("/api/settings/general", HTTP_GET, handleApiSettingsGeneral);
  server.on("/api/nfc", handleApiNfc);
  server.on("/api/nfc/read", HTTP_GET, handleApiNfcRead);
  server.on("/api/nfc/write", HTTP_GET, handleApiNfcWriteTest);
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
