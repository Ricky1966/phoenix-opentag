#pragma once
#include <Arduino.h>

String escapeJson(const String &value);
String escapeHtml(const String &value);
String hexByte(uint8_t value);
char   printableAscii(uint8_t value);
String hexWord(uint16_t value);
String valueOrDash(float value, const char *suffix);
void   printHexBuffer(const char *label, const uint8_t *buffer, uint8_t length);
String uidToString(const uint8_t *uid, uint8_t uidLength);
String uidToStringReversed(const uint8_t *uid, uint8_t uidLength);
String bytesToVersion(const uint8_t *bytes);
