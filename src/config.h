#pragma once
#include <Arduino.h>

// Firmware version
#define PHOENIX_FW_VERSION "0.4.0"

// PN5180 SPI — bus condiviso (SCK18/MISO19/MOSI23), CS/BUSY/RST dedicati
constexpr uint8_t PN5180_NSS  = 5;   // chip-select
constexpr uint8_t PN5180_BUSY = 16;  // input
constexpr uint8_t PN5180_RST  = 17;  // reset

constexpr unsigned long NFC_POLL_MS = 1500;  // cadenza polling lettore NFC

// Invio letture al demone su Phoenix: valori iniziali, modificabili da seriale
// (comandi mode/host/reader, vedi transport.h) e poi conservati in flash.
#define TRANSPORT_DEFAULT_MODE   "usb"                        // usb | wifi | auto
#define TRANSPORT_DEFAULT_HOST   "http://192.168.1.36:7913"   // demone su Phoenix
#define TRANSPORT_DEFAULT_READER "phoenix-opentag"

// Nome di rete per l'OTA: phoenix-opentag.local (password OTA_PASSWORD in secrets.h)
#define OTA_HOSTNAME "phoenix-opentag"

// WiFi (stazione, nessun Access Point). Credenziali vere in secrets.h (non versionato).
#include "secrets.h"
