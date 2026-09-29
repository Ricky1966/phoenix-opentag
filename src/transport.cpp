#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"
#include "nfc.h"
#include "delivery.h"
#include "transport.h"

void transportLoop() {
  static DeliveryState delivery;
  static unsigned long lastReconnect=0;
  if (!PHOENIX_TAG_ENDPOINT[0]) return;
  // This first transport targets a local HTTP bridge. Never downgrade HTTPS.
  static bool endpointChecked=false;
  static bool endpointOk=false;
  if(!endpointChecked) {
    endpointChecked=true;
    endpointOk=strncmp(PHOENIX_TAG_ENDPOINT,"http://",7)==0;
    if(!endpointOk) Serial.println("HTTP: endpoint non supportato, usare il bridge HTTP locale");
  }
  if(!endpointOk) return;
  const uint32_t now=millis();
  static unsigned long observedRead=0;
  static bool observedValid=false, observedPresent=false;
  if (observedRead!=spool.lastReadMs || observedValid!=spool.valid || observedPresent!=spool.present) {
    observedRead=spool.lastReadMs; observedValid=spool.valid; observedPresent=spool.present;
    delivery.observe(spool,spoolJson(spool,PHOENIX_READER_ID,PHOENIX_FW_VERSION));
  }
  if(WiFi.status()!=WL_CONNECTED) {
    if(uint32_t(now-lastReconnect)>=30000) { lastReconnect=now; WiFi.reconnect(); }
    return;
  }
  if(!delivery.ready(now)) return;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(1500);
  http.setTimeout(1500);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  int status=-1;
  if(http.begin(client,PHOENIX_TAG_ENDPOINT)) {
    http.addHeader("Content-Type","application/json");
    status=http.POST(String(delivery.body.c_str()));
    http.end();
  }
  delivery.finish(millis(),status>=200 && status<300);
  Serial.printf("HTTP tag: codice=%d%s\n",status,(status>=200 && status<300)?"":" (riprovo con attesa)");
}
