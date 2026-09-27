#pragma once
#include <Arduino.h>

// Firmware version
#define PHOENIX_FW_VERSION "0.2.0"

// PN5180 SPI — bus condiviso (SCK18/MISO19/MOSI23), CS/BUSY/RST dedicati
constexpr uint8_t PN5180_NSS  = 5;   // chip-select
constexpr uint8_t PN5180_BUSY = 16;  // input
constexpr uint8_t PN5180_RST  = 17;  // reset

constexpr unsigned long NFC_POLL_MS = 1500;  // cadenza polling lettore NFC

// WiFi (stazione, nessun Access Point). Credenziali vere in secrets.h (non versionato).
#include "secrets.h"
