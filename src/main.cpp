#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "nfc.h"

static unsigned long lastPollMs = 0;
static String lastPrintedUid = "";

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
  setupNfcReader();
}

void loop() {
  if (millis() - lastPollMs < NFC_POLL_INTERVAL_MS) return;
  lastPollMs = millis();

  clearNfcReadState();
  readNfcTag();

  // Stampa solo quando cambia il tag (evita di spammare la seriale a ogni poll)
  if (nfcData.lastUid != "--" && nfcData.lastUid != lastPrintedUid) {
    lastPrintedUid = nfcData.lastUid;
    printNfcData();
  } else if (nfcData.lastUid == "--") {
    lastPrintedUid = "";
  }
}
