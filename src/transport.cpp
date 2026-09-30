#include <Arduino.h>
#include "transport.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include "config.h"
#include "delivery.h"
#include "nfc.h"
#include <esp_system.h>

enum Transport { T_USB, T_WIFI, T_AUTO };

static Transport mode = T_USB;
static String host;
static String readerId;
static String line;

static const char *modeName(Transport m) {
  return m == T_WIFI ? "wifi" : m == T_AUTO ? "auto" : "usb";
}

static bool parseMode(const String &s, Transport *out) {
  if (s == "usb")  { *out = T_USB;  return true; }
  if (s == "wifi") { *out = T_WIFI; return true; }
  if (s == "auto") { *out = T_AUTO; return true; }
  return false;
}

// Stessi caratteri ammessi da Spoolman per reader_id: [A-Za-z0-9._:-]{1,64}
static bool validReaderId(const String &s) {
  if (s.length() < 1 || s.length() > 64) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != ':' && c != '-') return false;
  }
  return true;
}

// getString() su una chiave mai salvata stampa un errore NOT_FOUND: meglio controllare prima.
static String load(Preferences &p, const char *key, const char *fallback) {
  return p.isKey(key) ? p.getString(key, fallback) : String(fallback);
}

static void save(const char *key, const String &value) {
  Preferences p;
  p.begin("opentag", false);
  p.putString(key, value);
  p.end();
}

// Latest validated observation only. USB acknowledges the event token, so a
// delayed ACK for an older spool cannot clear a newer pending observation.
static DeliveryState delivery;
static String eventToken;
static uint32_t bootId, sequence = 0;

static bool postWifi(const String &json) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(1500);
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, host + "/tag")) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  http.end();
  Serial.printf("Demone HTTP: %d\n", code);
  return code >= 200 && code < 300;
}

static void deliverLatest() {
  std::string json = spoolJson(spool, readerId.c_str(), PHOENIX_FW_VERSION);
  bool newEvent = spool.present && spool.valid &&
                  (json != delivery.body || eventToken.length() == 0);
  delivery.observe(spool, json);
  if (!spool.present) eventToken = "";
  if (newEvent) {
    char token[32];
    snprintf(token, sizeof(token), "%08lx-%08lx", (unsigned long)bootId,
             (unsigned long)++sequence);
    eventToken = token;
  }
  if (!delivery.ready(millis())) return;
  String payload(delivery.body.c_str());
  payload.remove(payload.length()-1);
  payload += ",\"event_id\":\"" + eventToken + "\"}";
  bool sent = mode != T_USB && postWifi(payload);
  if (!sent && mode != T_WIFI) {
    Serial.print("PHOENIX-TAG "); Serial.println(payload);
  }
  // USB remains pending until 'ack <event_id>' from the daemon.
  delivery.finish(millis(), sent);
}

// ───────────────────────── configurazione ─────────────────────────
static void show() {
  Serial.print("Modalita invio: "); Serial.println(modeName(mode));
  Serial.print("Host demone:    "); Serial.println(host);
  Serial.print("Reader id:      "); Serial.println(readerId);
  Serial.print("WiFi:           ");
  if (WiFi.status() == WL_CONNECTED) { Serial.print("connesso, IP "); Serial.println(WiFi.localIP()); }
  else Serial.println("non connesso");
}

static void help() {
  Serial.println("Comandi:");
  Serial.println("  show                     impostazioni correnti");
  Serial.println("  mode usb|wifi|auto       come inviare le letture (riavvio per applicare)");
  Serial.println("  host http://ip:porta     indirizzo del demone su Phoenix");
  Serial.println("  reader <id>              reader_id mostrato in Spoolman");
  Serial.println("  test                     rinvia l'ultimo tag letto");
  Serial.println("  reboot");
}

static void command(String cmd) {
  cmd.trim();
  if (!cmd.length()) return;
  int sp = cmd.indexOf(' ');
  String name = sp < 0 ? cmd : cmd.substring(0, sp);
  String arg = sp < 0 ? String() : cmd.substring(sp + 1);
  arg.trim();

  if (name == "ack") {
    if (arg == eventToken && delivery.available && delivery.pending)
      delivery.finish(millis(), true);
  }
  else if (name == "help") help();
  else if (name == "show") show();
  else if (name == "reboot") { Serial.println("Riavvio"); delay(100); ESP.restart(); }
  else if (name == "mode") {
    Transport m;
    if (!parseMode(arg, &m)) { Serial.println("mode: usa usb, wifi o auto"); return; }
    save("mode", arg);
    Serial.println("Salvato, 'reboot' per applicare.");
  } else if (name == "host") {
    if (!arg.startsWith("http://")) { Serial.println("host: deve iniziare con http://"); return; }
    while (arg.endsWith("/")) arg.remove(arg.length() - 1);
    host = arg;
    save("host", host);
    Serial.print("Host salvato: "); Serial.println(host);
  } else if (name == "reader") {
    if (!validReaderId(arg)) { Serial.println("reader: solo [A-Za-z0-9._:-], 1-64 caratteri"); return; }
    readerId = arg;
    save("reader", readerId);
    Serial.print("Reader id salvato: "); Serial.println(readerId);
  } else if (name == "test") {
    if (!spool.present || !spool.valid) { Serial.println("test: nessun tag valido presente"); return; }
    delivery.delivered.clear(); delivery.attempted = false;
    eventToken = "";
  } else {
    Serial.println("Comando sconosciuto, 'help' per l'elenco.");
  }
}

void transportLoop() {
  // Observe removal/new UID before accepting serial ACKs.
  deliverLatest();
  static unsigned long lastReconnect = 0;
  if (WiFi.status() != WL_CONNECTED && uint32_t(millis()-lastReconnect) >= 30000) {
    lastReconnect = millis(); WiFi.reconnect();
  }
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') { command(line); line = ""; }
    else if (c == '\b' || c == 0x7F) { if (line.length()) line.remove(line.length() - 1); }  // Backspace
    else if (line.length() < 128) line += c;
  }
}

void transportSetup() {
  bootId = esp_random();
  Preferences p;
  p.begin("opentag", false);  // lettura/scrittura: crea il namespace al primo avvio, niente errore NOT_FOUND
  String m = load(p, "mode", TRANSPORT_DEFAULT_MODE);
  host = load(p, "host", TRANSPORT_DEFAULT_HOST);
  readerId = load(p, "reader", TRANSPORT_DEFAULT_READER);
  p.end();
  if (!parseMode(m, &mode)) mode = T_USB;
  show();
  Serial.println("Digita 'help' per i comandi.");
}
