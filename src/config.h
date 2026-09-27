#pragma once
#include <Arduino.h>

// Firmware version
#define FIRMWARE_VERSION "0.1.0"

// PN5180 SPI
constexpr uint8_t PN5180_NSS  = 5;
constexpr uint8_t PN5180_BUSY = 16;
constexpr uint8_t PN5180_RST  = 17;
// SPI SCK/MOSI/MISO restano sui pin VSPI di default (18/23/19) impostati dal framework Arduino.

// Timing
constexpr uint8_t       NFC_READ_ATTEMPTS    = 5;
constexpr unsigned long NFC_RETRY_DELAY_MS   = 80;
constexpr unsigned long NFC_POLL_INTERVAL_MS = 500;

// WiFi (stazione, nessun Access Point). Credenziali vere in secrets.h (non versionato).
#include "secrets.h"

// ── Structs ──────────────────────────────────────────────────────────────────

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
  String   openPrintTagColor = "#ffffff";
  uint8_t  lastUidLength = 0;
};
