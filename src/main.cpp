#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include "config.h"
#include "nfc.h"
#include "transport.h"

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  Serial.print("Connessione WiFi a "); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
    delay(250); Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connesso, IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi non connesso (continuo comunque, la lettura NFC non dipende dalla rete).");
  }
}

// Aggiornamento firmware via WiFi (espota, porta UDP 3232). Attivo solo con
// OTA_PASSWORD definita in secrets.h; parte appena il WiFi e' connesso.
static bool otaStarted = false;

static void otaLoop() {
#ifdef OTA_PASSWORD
  if (!otaStarted) {
    if (WiFi.status() != WL_CONNECTED) return;
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() { Serial.println("OTA: aggiornamento in corso, lettura NFC sospesa"); });
    ArduinoOTA.onEnd([]() { Serial.println("OTA: completato, riavvio"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA: errore %u\n", e); });
    ArduinoOTA.begin();
    otaStarted = true;
    Serial.printf("OTA attivo: %s.local (%s)\n", OTA_HOSTNAME, WiFi.localIP().toString().c_str());
  }
  ArduinoOTA.handle();
#endif
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nPhoenix OpenTag reader - avvio");

  transportSetup();
  // WiFi sempre attivo: serve all'OTA anche in modalita' usb.
  connectWifi();
#ifndef OTA_PASSWORD
  Serial.println("OTA disattivato: definisci OTA_PASSWORD in secrets.h");
#endif
  nfcSetup();
}

void loop() {
  otaLoop();
  nfcLoop();
  transportLoop();
  delay(1);
}
