#include "log.h"
#include "sensor.h"
#include "heater.h"
#include <cmath>

static constexpr uint8_t       LOG_MAX         = 120;   // 4h a 2 min
static constexpr unsigned long LOG_INTERVAL_MS = 120000UL;

static LogEntry      logBuf[LOG_MAX];
static uint8_t       logHead  = 0;
static uint8_t       logCount = 0;
static unsigned long lastLogMs = 0;

void logCheck() {
  if (millis() - lastLogMs < LOG_INTERVAL_MS) return;
  lastLogMs = millis();
  logBuf[logHead] = { millis(), sensorData.temperature, sensorData.humidity,
                      bedData.temperature, heaterControl.outputPercent };
  logHead = (logHead + 1) % LOG_MAX;
  if (logCount < LOG_MAX) logCount++;
}

String logJson() {
  const unsigned long nowMs = millis();
  String json = "{\"nowMs\":"; json += String(nowMs); json += ",\"entries\":[";
  const uint8_t oldest = (logCount < LOG_MAX) ? 0 : logHead;
  for (uint8_t i = 0; i < logCount; i++) {
    const uint8_t idx = (oldest + i) % LOG_MAX;
    const LogEntry &e = logBuf[idx];
    if (i > 0) json += ",";
    json += "{\"t\":";   json += String(e.timestampMs);
    json += ",\"temp\":"; json += isnan(e.temperature)   ? "null" : String(e.temperature, 1);
    json += ",\"hum\":";  json += isnan(e.humidity)      ? "null" : String(e.humidity, 1);
    json += ",\"bed\":";  json += isnan(e.bedTemp)        ? "null" : String(e.bedTemp, 1);
    json += ",\"pct\":";  json += String(e.outputPercent, 1);
    json += "}";
  }
  json += "]}";
  return json;
}
