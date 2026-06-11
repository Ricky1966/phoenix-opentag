#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include "config.h"
#include "materials.h"
#include "sensor.h"
#include "heater.h"
#include "web_ui.h"
#include "log.h"
#include "translations.h"

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nODIN - avvio");

  loadAppSettings();
  loadCustomProfiles();
  Serial.print("Profili personalizzati: "); Serial.println(customProfileCount);

  pinMode(HEATER_PIN, OUTPUT);
  setHeater(false);

  Wire.begin(I2C_SDA, I2C_SCL);
  sensorSetup();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP: http://"); Serial.println(WiFi.softAPIP());

  connectWifiClient();

  // Scarica traduzioni da Pi5 dopo WiFi
  delay(1000); // Attendi che WiFi sia pronto
  downloadTranslationsFromPi5(appSettings.language.c_str());

  setupWebServer();
}

void loop() {
  webLoop();
  checkWifiReconnect();
  readSensor();
  readBedTemperature();
  updateHeaterControl();
  logCheck();
}
