#include <Arduino.h>
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nPhoenix OpenTag reader - avvio");

  connectWifi();
  nfcSetup();
}

void loop() {
  nfcLoop();
  transportLoop();
  delay(1);
}
