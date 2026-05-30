#include "heater.h"
#include "sensor.h"

HeaterControlData heaterControl;
bool              heaterEnabled = false;

void setHeater(bool enabled) {
  heaterEnabled = enabled;
  digitalWrite(HEATER_PIN, enabled == HEATER_ACTIVE_LOW ? LOW : HIGH);
  Serial.print("MOSFET riscaldatore: "); Serial.println(enabled ? "ON" : "OFF");
}

void stopHeaterAutomation(const String &status) {
  heaterControl.automatic     = false;
  heaterControl.outputPercent = 0.0F;
  heaterControl.integral      = 0.0F;
  heaterControl.cycleRunning  = false;
  heaterControl.status        = status;
  setHeater(false);
}

void startHeaterAutomation() {
  const unsigned long now = millis();
  heaterControl.automatic     = true;
  heaterControl.outputPercent = 0.0F;
  heaterControl.integral      = 0.0F;
  heaterControl.lastUpdateMs  = now > PI_UPDATE_INTERVAL_MS ? now - PI_UPDATE_INTERVAL_MS : 0;
  heaterControl.windowStartMs = now;
  heaterControl.cycleEndMs    = now + (heaterControl.cycleDurationSeconds * 1000UL);
  heaterControl.cycleRemainingSeconds = heaterControl.cycleDurationSeconds;
  heaterControl.cycleRunning  = heaterControl.cycleDurationSeconds > 0;
  heaterControl.status        = "Automatico PI";
  setHeater(false);
}

void updateHeaterControl() {
  if (!heaterControl.automatic) return;

  readBedTemperature();

  if (!bedData.valid)                              { stopHeaterAutomation("STOP: NTC non valida");    return; }
  if (bedData.temperature >= BED_SAFETY_LIMIT_C)  { stopHeaterAutomation("STOP: limite sicurezza");  return; }

  const unsigned long now = millis();

  if (heaterControl.cycleRunning) {
    const long remainingMs = (long)(heaterControl.cycleEndMs - now);
    if (remainingMs <= 0) { heaterControl.cycleRemainingSeconds = 0; stopHeaterAutomation("Ciclo completato"); return; }
    heaterControl.cycleRemainingSeconds = (remainingMs + 999L) / 1000L;
  }

  if (now - heaterControl.windowStartMs >= HEATER_WINDOW_MS)
    heaterControl.windowStartMs = now;

  if (now - heaterControl.lastUpdateMs >= PI_UPDATE_INTERVAL_MS) {
    const float dt    = (now - heaterControl.lastUpdateMs) / 1000.0F;
    heaterControl.lastUpdateMs = now;

    const float error = heaterControl.setpoint - bedData.temperature;
    heaterControl.integral += error * PI_KI * dt;
    heaterControl.integral  = constrain(heaterControl.integral, 0.0F, 100.0F);

    heaterControl.outputPercent = constrain((PI_KP * error) + heaterControl.integral, 0.0F, 100.0F);
    heaterControl.status        = "Automatico PI";

    Serial.print("PI piatto: T "); Serial.print(bedData.temperature, 1);
    Serial.print(" C, set ");      Serial.print(heaterControl.setpoint, 1);
    Serial.print(" C, output ");   Serial.print(heaterControl.outputPercent, 1); Serial.println(" %");
  }

  const unsigned long onTimeMs = (HEATER_WINDOW_MS * heaterControl.outputPercent) / 100.0F;
  setHeater((now - heaterControl.windowStartMs) < onTimeMs);
}

uint32_t heaterCycleRemainingSeconds() {
  return heaterControl.cycleRunning ? heaterControl.cycleRemainingSeconds : heaterControl.cycleDurationSeconds;
}

uint32_t heaterCycleElapsedSeconds() {
  if (!heaterControl.cycleRunning || heaterControl.cycleDurationSeconds <= heaterControl.cycleRemainingSeconds)
    return 0;
  return heaterControl.cycleDurationSeconds - heaterControl.cycleRemainingSeconds;
}

String heaterJson() {
  String json = "{\"enabled\":";
  json += heaterEnabled ? "true" : "false";
  json += ",\"pin\":";             json += String(HEATER_PIN);
  json += ",\"automatic\":";       json += heaterControl.automatic ? "true" : "false";
  json += ",\"setpoint\":";        json += String(heaterControl.setpoint, 1);
  json += ",\"outputPercent\":";   json += String(heaterControl.outputPercent, 1);
  json += ",\"cycleDurationSeconds\":";  json += String(heaterControl.cycleDurationSeconds);
  json += ",\"cycleElapsedSeconds\":";   json += String(heaterCycleElapsedSeconds());
  json += ",\"cycleRemainingSeconds\":"; json += String(heaterCycleRemainingSeconds());
  json += ",\"cycleRunning\":";    json += heaterControl.cycleRunning ? "true" : "false";
  json += ",\"safetyLimit\":";     json += String(BED_SAFETY_LIMIT_C, 1);
  json += ",\"status\":\"";        json += heaterControl.status; json += "\"";
  json += "}";
  return json;
}
