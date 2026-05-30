#include "utils.h"

String escapeJson(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (uint16_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c == '\\' || c == '"') escaped += '\\';
    escaped += c;
  }
  return escaped;
}

String escapeHtml(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (uint16_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if      (c == '&') escaped += F("&amp;");
    else if (c == '"') escaped += F("&quot;");
    else if (c == '<') escaped += F("&lt;");
    else if (c == '>') escaped += F("&gt;");
    else               escaped += c;
  }
  return escaped;
}

String hexByte(uint8_t value) {
  String text;
  if (value < 0x10) text += "0";
  text += String(value, HEX);
  text.toUpperCase();
  return text;
}

char printableAscii(uint8_t value) {
  if (value >= 32 && value <= 126 && value != '<' && value != '>' && value != '&' && value != '"')
    return (char)value;
  return '.';
}

String hexWord(uint16_t value) {
  String text = "0x";
  if (value < 0x1000) text += "0";
  if (value < 0x100)  text += "0";
  if (value < 0x10)   text += "0";
  text += String(value, HEX);
  text.toUpperCase();
  return text;
}

String valueOrDash(float value, const char *suffix) {
  if (isnan(value)) return "--";
  return String(value, 1) + suffix;
}

void printHexBuffer(const char *label, const uint8_t *buffer, uint8_t length) {
  Serial.print(label);
  for (uint8_t i = 0; i < length; i++) {
    if (i > 0) Serial.print(" ");
    Serial.print(hexByte(buffer[i]));
  }
  Serial.println();
}

String uidToString(const uint8_t *uid, uint8_t uidLength) {
  String value;
  for (uint8_t i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) value += "0";
    value += String(uid[i], HEX);
    if (i + 1 < uidLength) value += ":";
  }
  value.toUpperCase();
  return value;
}

String uidToStringReversed(const uint8_t *uid, uint8_t uidLength) {
  String value;
  for (int8_t i = uidLength - 1; i >= 0; i--) {
    if (uid[i] < 0x10) value += "0";
    value += String(uid[i], HEX);
    if (i > 0) value += ":";
  }
  value.toUpperCase();
  return value;
}

String bytesToVersion(const uint8_t *bytes) {
  return String(bytes[1]) + "." + String(bytes[0]);
}
