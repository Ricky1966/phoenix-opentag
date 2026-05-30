#include "sensor.h"
#include <Adafruit_BME280.h>

static Adafruit_BME280 bme;
SensorData sensorData;
BedData    bedData;

void sensorBegin() {
  if (!bme.begin(BME280_I2C_ADDRESS)) {
    Serial.println("BME280 non trovato a 0x76. Controllare collegamenti.");
  } else {
    Serial.println("BME280 trovato a 0x76.");
    readSensor(true);
  }
}

void readSensor(bool force) {
  const unsigned long now = millis();
  if (!force && now - sensorData.lastReadMs < SENSOR_READ_INTERVAL_MS) return;

  sensorData.lastReadMs  = now;
  sensorData.temperature = bme.readTemperature();
  sensorData.humidity    = bme.readHumidity();
  sensorData.valid       = !isnan(sensorData.temperature) && !isnan(sensorData.humidity);

  if (!sensorData.valid) { Serial.println("BME280: lettura non valida"); return; }

  Serial.print("Temperatura: "); Serial.print(sensorData.temperature);
  Serial.print(" C, Umidita: "); Serial.print(sensorData.humidity); Serial.println(" %");
}

void readBedTemperature(bool force) {
  const unsigned long now = millis();
  if (!force && now - bedData.lastReadMs < BED_READ_INTERVAL_MS) return;

  bedData.lastReadMs = now;

  uint16_t samples[BED_ADC_SAMPLES];
  for (uint8_t i = 0; i < BED_ADC_SAMPLES; i++) { samples[i] = analogRead(BED_THERMISTOR_PIN); delay(2); }

  for (uint8_t i = 1; i < BED_ADC_SAMPLES; i++) {
    const uint16_t v = samples[i]; int8_t j = i - 1;
    while (j >= 0 && samples[j] > v) { samples[j + 1] = samples[j]; j--; }
    samples[j + 1] = v;
  }

  uint32_t rawSum = 0;
  for (uint8_t i = 4; i < BED_ADC_SAMPLES - 4; i++) rawSum += samples[i];
  bedData.rawAdc = rawSum / (BED_ADC_SAMPLES - 8);
  bedData.valid  = bedData.rawAdc > 20 && bedData.rawAdc < 4075;

  if (!bedData.valid) {
    bedData.temperature = bedData.resistance = bedData.filteredAdc = NAN;
    Serial.print("NTC piatto: lettura non valida, ADC "); Serial.println(bedData.rawAdc);
    return;
  }

  if (isnan(bedData.filteredAdc)) {
    bedData.filteredAdc = bedData.rawAdc;
  } else {
    const float step = (bedData.rawAdc - bedData.filteredAdc) * BED_ADC_FILTER_ALPHA;
    bedData.filteredAdc += constrain(step, -BED_MAX_ADC_STEP, BED_MAX_ADC_STEP);
  }

  bedData.resistance  = BED_PULLUP_OHMS * bedData.filteredAdc / (ADC_MAX_VALUE - bedData.filteredAdc);
  const float steinhart = (1.0F / BED_NOMINAL_TEMP_K) + (log(bedData.resistance / BED_NOMINAL_OHMS) / BED_BETA);
  bedData.temperature = ((1.0F / steinhart) - 273.15F) + BED_TEMP_OFFSET_C;

  Serial.print("NTC piatto: "); Serial.print(bedData.temperature, 1);
  Serial.print(" C, ADC "); Serial.print(bedData.rawAdc);
  Serial.print(" -> "); Serial.print(bedData.filteredAdc, 1);
  Serial.print(", R "); Serial.print(bedData.resistance, 0); Serial.println(" ohm");
}

String sensorJson() {
  readSensor();
  String json = "{\"valid\":";
  json += sensorData.valid ? "true" : "false";
  json += ",\"temperature\":"; json += sensorData.valid ? String(sensorData.temperature, 2) : "null";
  json += ",\"humidity\":";    json += sensorData.valid ? String(sensorData.humidity, 2)    : "null";
  json += ",\"ageSeconds\":";  json += sensorData.valid ? String((millis() - sensorData.lastReadMs) / 1000) : "null";
  json += "}";
  return json;
}

String bedJson() {
  readBedTemperature();
  String json = "{\"valid\":";
  json += bedData.valid ? "true" : "false";
  json += ",\"temperature\":"; json += bedData.valid ? String(bedData.temperature, 2) : "null";
  json += ",\"resistance\":";  json += bedData.valid ? String(bedData.resistance, 0)  : "null";
  json += ",\"rawAdc\":";      json += String(bedData.rawAdc);
  json += ",\"filteredAdc\":"; json += bedData.valid ? String(bedData.filteredAdc, 1) : "null";
  json += ",\"pin\":";         json += String(BED_THERMISTOR_PIN);
  json += "}";
  return json;
}

// Called from main setup()
void sensorSetup() {
  analogReadResolution(12);
  analogSetPinAttenuation(BED_THERMISTOR_PIN, ADC_11db);
  readBedTemperature(true);
  sensorBegin();
}
