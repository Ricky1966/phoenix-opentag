#pragma once
#include <Arduino.h>

// I2C
constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t BME280_I2C_ADDRESS = 0x76;

// PN5180 SPI
constexpr uint8_t PN5180_NSS  = 5;
constexpr uint8_t PN5180_BUSY = 16;
constexpr uint8_t PN5180_RST  = 17;

// Heater / NTC
constexpr uint8_t  HEATER_PIN         = 25;
constexpr bool     HEATER_ACTIVE_LOW  = true;
constexpr uint8_t  BED_THERMISTOR_PIN = 34;

// WiFi AP
constexpr const char *AP_SSID     = "ODIN";
constexpr const char *AP_PASSWORD = "odindryer";

// Timing
constexpr unsigned long SENSOR_READ_INTERVAL_MS = 60000;
constexpr unsigned long BED_READ_INTERVAL_MS    = 3000;
constexpr uint8_t       BED_ADC_SAMPLES         = 32;
constexpr uint8_t       NFC_READ_ATTEMPTS        = 5;
constexpr unsigned long NFC_RETRY_DELAY_MS       = 80;

// NTC / ADC
constexpr float BED_PULLUP_OHMS      = 100000.0F;
constexpr float BED_NOMINAL_OHMS     = 100000.0F;
constexpr float BED_NOMINAL_TEMP_K   = 298.15F;
constexpr float BED_BETA             = 3950.0F;
constexpr float ADC_MAX_VALUE        = 4095.0F;
constexpr float BED_ADC_FILTER_ALPHA = 0.15F;
constexpr float BED_MAX_ADC_STEP     = 35.0F;
constexpr float BED_TEMP_OFFSET_C    = -2.5F;
constexpr float DEFAULT_BED_SETPOINT_C = 45.0F;
constexpr float BED_SAFETY_LIMIT_C    = 90.0F;

// PI controller
constexpr float         PI_KP                = 10.0F;
constexpr float         PI_KI                = 0.028F;
constexpr unsigned long PI_UPDATE_INTERVAL_MS = 3000;
constexpr unsigned long HEATER_WINDOW_MS      = 10000;

// Raspberry Pi5 (proxy ODIN) — IP locale LAN
constexpr const char  *PI5_LOCAL_IP          = "192.168.1.198";
constexpr uint16_t     PI5_LOCAL_PORT        = 8000;
constexpr unsigned long PI5_CHECK_INTERVAL_MS = 30000;

// Locale (set via build flag -D UI_LOCALE_IT or UI_LOCALE_EN etc.)
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

// ── Structs ──────────────────────────────────────────────────────────────────

struct SensorData {
  float temperature = NAN;
  float humidity    = NAN;
  unsigned long lastReadMs = 0;
  bool valid = false;
};

struct BedData {
  float temperature = NAN;
  float resistance  = NAN;
  float filteredAdc = NAN;
  uint16_t rawAdc   = 0;
  unsigned long lastReadMs = 0;
  bool valid = false;
};

struct NfcData {
  bool readerPresent = false;
  bool rfReady       = false;
  bool initialized   = false;
  String productVersion  = "--";
  String firmwareVersion = "--";
  String eepromVersion   = "--";
  String lastUid         = "--";
  String lastStatus      = "In attesa";
  String tagType         = "--";
  String technologies    = "--";
  String atqa            = "--";
  String sak             = "--";
  String memoryInfo      = "--";
  String dataFormat      = "--";
  String memoryDump      = "--";
  String openPrintTagColor = "#ffffff";
  // OpenPrintTag parsed fields
  bool    optParsed    = false;
  String  optBrand;
  String  optMaterial;
  uint8_t optMatType   = 0;
  uint16_t optDryTemp  = 0;
  uint16_t optDryTime  = 0;
  uint16_t optNozzleMin = 0, optNozzleMax = 0;
  uint16_t optBedMin   = 0, optBedMax    = 0;
  float    optDensity  = 0.0f;
  float    optDiameter = 0.0f;
  uint16_t optNomWeight = 0, optActWeight = 0, optEmptyWeight = 0;
  uint32_t optNomLength = 0, optActLength = 0;
  uint16_t optSpoolW = 0, optSpoolOd = 0, optSpoolId = 0, optSpoolHole = 0;
  uint16_t optMdate    = 0;
  String   optCountry;
  uint8_t lastUidLength  = 0;
  bool readArmed         = true;
};

struct HeaterControlData {
  bool   automatic      = false;
  float  setpoint       = DEFAULT_BED_SETPOINT_C;
  float  outputPercent  = 0.0F;
  float  integral       = 0.0F;
  String status         = "Manuale";
  unsigned long lastUpdateMs  = 0;
  unsigned long windowStartMs = 0;
  uint32_t cycleDurationSeconds  = 360UL * 60UL;
  uint32_t cycleRemainingSeconds = 360UL * 60UL;
  unsigned long cycleEndMs       = 0;
  bool cycleRunning = false;
};

struct MaterialDryProfile {
  const char *name;
  float       recommendedTemp;
  uint16_t    recommendedMinutes;
};

struct CustomMaterialProfile {
  String   name;
  float    recommendedTemp    = 0.0F;
  uint16_t recommendedMinutes = 0;
};

struct AppSettings {
  String wifiSsid        = "";
  String wifiPassword    = "";
  String language        = UI_LOCALE_CODE;
  bool   wifiClientEnabled = false;
  bool   debugEnabled      = false;
};
