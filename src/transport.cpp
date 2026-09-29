#include "transport.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include "config.h"

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

static void save(const char *key, const String &value) {
  Preferences p;
  p.begin("opentag", false);
  p.putString(key, value);
  p.end();
}

// ───────────────────────── JSON ─────────────────────────
static void addStr(String &j, const char *key, const char *val) {
  if (!val[0]) return;
  j += ",\""; j += key; j += "\":\"";
  for (const char *c = val; *c; c++) {
    if (*c == '"' || *c == '\\') { j += '\\'; j += *c; }
    else if ((unsigned char)*c < 0x20) j += ' ';
    else j += *c;
  }
  j += '"';
}

static void addInt(String &j, const char *key, int val) {
  if (!val) return;
  j += ",\""; j += key; j += "\":"; j += val;
}

static void addFloat(String &j, const char *key, float val, int digits) {
  if (isnan(val)) return;
  j += ",\""; j += key; j += "\":"; j += String(val, digits);
}

static String tagJson(const SpoolData &s) {
  String j = "{\"uid\":\"";
  j += s.uid;
  j += "\"";
  addStr(j, "reader_id", readerId.c_str());
  addStr(j, "fw", PHOENIX_FW_VERSION);
  addStr(j, "brand", s.brand);
  addStr(j, "material", s.material);
  if (s.hasColor) {
    char hex[7]; sprintf(hex, "%02X%02X%02X", s.color[0], s.color[1], s.color[2]);
    addStr(j, "color", hex);
  }
  addFloat(j, "diameter", s.diameter, 2);
  addFloat(j, "density", s.density, 3);
  addInt(j, "nozzle_min", s.nozzleMin);
  addInt(j, "nozzle_max", s.nozzleMax);
  addInt(j, "bed_min", s.bedMin);
  addInt(j, "bed_max", s.bedMax);
  addInt(j, "weight", s.weight);
  addStr(j, "country", s.country);
  j += "}";
  return j;
}

// ───────────────────────── invio ─────────────────────────
static bool postWifi(const String &json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Invio WiFi: non connesso");
    return false;
  }
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  if (!http.begin(host + "/tag")) {
    Serial.println("Invio WiFi: host non valido");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  String body = code > 0 ? http.getString() : "";
  http.end();
  if (code <= 0) {
    Serial.print("Invio WiFi fallito: "); Serial.println(HTTPClient::errorToString(code));
    return false;
  }
  Serial.print("Demone HTTP "); Serial.print(code); Serial.print(": "); Serial.println(body);
  return code >= 200 && code < 300;
}

void transportSendTag(const SpoolData &s) {
  String json = tagJson(s);
  if (mode != T_USB && postWifi(json)) return;
  if (mode == T_WIFI) return;
  // usb, oppure auto con WiFi non disponibile: la riga la legge il demone.
  Serial.print("PHOENIX-TAG "); Serial.println(json);
}

// ───────────────────────── configurazione ─────────────────────────
static void show() {
  Serial.print("Modalita invio: "); Serial.println(modeName(mode));
  Serial.print("Host demone:    "); Serial.println(host);
  Serial.print("Reader id:      "); Serial.println(readerId);
  if (mode != T_USB) {
    Serial.print("WiFi:           ");
    if (WiFi.status() == WL_CONNECTED) { Serial.print("connesso, IP "); Serial.println(WiFi.localIP()); }
    else Serial.println("non connesso");
  }
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

  if (name == "help") help();
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
    if (!spool.uid[0]) { Serial.println("test: nessun tag letto finora"); return; }
    transportSendTag(spool);
  } else {
    Serial.println("Comando sconosciuto, 'help' per l'elenco.");
  }
}

void transportLoop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') { command(line); line = ""; }
    else if (line.length() < 128) line += c;
  }
}

bool transportWantsWifi() { return mode != T_USB; }

void transportSetup() {
  Preferences p;
  p.begin("opentag", true);
  String m = p.getString("mode", TRANSPORT_DEFAULT_MODE);
  host = p.getString("host", TRANSPORT_DEFAULT_HOST);
  readerId = p.getString("reader", TRANSPORT_DEFAULT_READER);
  p.end();
  if (!parseMode(m, &mode)) mode = T_USB;
  show();
  Serial.println("Digita 'help' per i comandi.");
}
