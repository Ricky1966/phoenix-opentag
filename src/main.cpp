#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <PN5180.h>
#include <PN5180ISO14443.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t BME280_I2C_ADDRESS = 0x76;

constexpr uint8_t PN5180_NSS = 5;
constexpr uint8_t PN5180_BUSY = 16;
constexpr uint8_t PN5180_RST = 17;

constexpr const char *AP_SSID = "Essiccatore";
constexpr const char *AP_PASSWORD = "essiccatore";

constexpr unsigned long SENSOR_READ_INTERVAL_MS = 60000;
constexpr unsigned long NFC_SCAN_INTERVAL_MS = 1000;

Adafruit_BME280 bme;
PN5180ISO14443 nfc14443(PN5180_NSS, PN5180_BUSY, PN5180_RST);
WebServer server(80);

struct SensorData {
  float temperature = NAN;
  float humidity = NAN;
  float pressure = NAN;
  unsigned long lastReadMs = 0;
  bool valid = false;
};

struct NfcData {
  bool readerPresent = false;
  bool rfReady = false;
  String productVersion = "--";
  String firmwareVersion = "--";
  String eepromVersion = "--";
  String lastUid = "--";
  String lastStatus = "In attesa";
  uint8_t lastUidLength = 0;
  unsigned long lastScanMs = 0;
};

SensorData sensorData;
NfcData nfcData;

String bytesToVersion(const uint8_t *bytes) {
  return String(bytes[1]) + "." + String(bytes[0]);
}

String uidToString(const uint8_t *uid, uint8_t uidLength) {
  String value;

  for (uint8_t i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) {
      value += "0";
    }
    value += String(uid[i], HEX);
    if (i + 1 < uidLength) {
      value += ":";
    }
  }

  value.toUpperCase();
  return value;
}

String valueOrDash(float value, const char *suffix) {
  if (isnan(value)) {
    return "--";
  }

  return String(value, 1) + suffix;
}

void setupNfcReader() {
  Serial.println("Inizializzo PN5180 ISO14443A...");
  Serial.print("PN5180 NSS/CS: GPIO ");
  Serial.println(PN5180_NSS);
  Serial.print("PN5180 BUSY: GPIO ");
  Serial.println(PN5180_BUSY);
  Serial.print("PN5180 RST: GPIO ");
  Serial.println(PN5180_RST);

  nfc14443.begin();
  nfc14443.reset();

  uint8_t productVersion[2] = {0xff, 0xff};
  uint8_t firmwareVersion[2] = {0xff, 0xff};
  uint8_t eepromVersion[2] = {0xff, 0xff};

  nfc14443.readEEprom(PRODUCT_VERSION, productVersion, sizeof(productVersion));
  nfc14443.readEEprom(FIRMWARE_VERSION, firmwareVersion, sizeof(firmwareVersion));
  nfc14443.readEEprom(EEPROM_VERSION, eepromVersion, sizeof(eepromVersion));

  nfcData.productVersion = bytesToVersion(productVersion);
  nfcData.firmwareVersion = bytesToVersion(firmwareVersion);
  nfcData.eepromVersion = bytesToVersion(eepromVersion);
  nfcData.readerPresent = productVersion[0] != 0xff || productVersion[1] != 0xff;

  Serial.print("PN5180 product version: ");
  Serial.println(nfcData.productVersion);
  Serial.print("PN5180 firmware version: ");
  Serial.println(nfcData.firmwareVersion);
  Serial.print("PN5180 EEPROM version: ");
  Serial.println(nfcData.eepromVersion);

  if (!nfcData.readerPresent) {
    nfcData.lastStatus = "PN5180 non rilevato";
    Serial.println("PN5180 non rilevato. Controllare alimentazione e jumper SPI.");
    return;
  }

  nfcData.rfReady = nfc14443.setupRF();
  nfcData.lastStatus = nfcData.rfReady ? "ISO14443A pronto" : "Errore RF ISO14443A";
  Serial.println(nfcData.rfReady ? "PN5180 RF ISO14443A pronto." : "Errore setup RF PN5180.");
}

void resetNfcRf() {
  nfc14443.reset();
  nfc14443.setupRF();
}

void scanNfcTag() {
  if (!nfcData.readerPresent || !nfcData.rfReady) {
    return;
  }

  const unsigned long now = millis();
  if (now - nfcData.lastScanMs < NFC_SCAN_INTERVAL_MS) {
    return;
  }

  nfcData.lastScanMs = now;

  uint8_t uid[10] = {0};
  const uint8_t uidLength = nfc14443.readCardSerial(uid);

  if (uidLength >= 4) {
    const String uidText = uidToString(uid, uidLength);
    nfcData.lastStatus = "Tag ISO14443A letto";
    nfcData.lastUid = uidText;
    nfcData.lastUidLength = uidLength;

    Serial.print("Tag ISO14443A UID ");
    Serial.print(uidLength);
    Serial.print(" byte: ");
    Serial.println(nfcData.lastUid);
  } else {
    nfcData.lastStatus = "Nessun tag ISO14443A";
  }

  resetNfcRf();
}

void readSensor(bool force = false) {
  const unsigned long now = millis();

  if (!force && sensorData.valid && now - sensorData.lastReadMs < SENSOR_READ_INTERVAL_MS) {
    return;
  }

  sensorData.temperature = bme.readTemperature();
  sensorData.humidity = bme.readHumidity();
  sensorData.pressure = bme.readPressure() / 100.0F;
  sensorData.lastReadMs = now;
  sensorData.valid = !isnan(sensorData.temperature) && !isnan(sensorData.humidity) && !isnan(sensorData.pressure);

  Serial.print("Temperatura: ");
  Serial.print(sensorData.temperature);
  Serial.print(" C, Umidita: ");
  Serial.print(sensorData.humidity);
  Serial.print(" %, Pressione: ");
  Serial.print(sensorData.pressure);
  Serial.println(" hPa");
}

String sensorJson() {
  readSensor();

  String json = "{";
  json += "\"valid\":";
  json += sensorData.valid ? "true" : "false";
  json += ",\"temperature\":";
  json += sensorData.valid ? String(sensorData.temperature, 2) : "null";
  json += ",\"humidity\":";
  json += sensorData.valid ? String(sensorData.humidity, 2) : "null";
  json += ",\"pressure\":";
  json += sensorData.valid ? String(sensorData.pressure, 2) : "null";
  json += ",\"ageSeconds\":";
  json += sensorData.valid ? String((millis() - sensorData.lastReadMs) / 1000) : "null";
  json += "}";
  return json;
}

String nfcJson() {
  scanNfcTag();

  String json = "{";
  json += "\"readerPresent\":";
  json += nfcData.readerPresent ? "true" : "false";
  json += ",\"rfReady\":";
  json += nfcData.rfReady ? "true" : "false";
  json += ",\"productVersion\":\"";
  json += nfcData.productVersion;
  json += "\",\"firmwareVersion\":\"";
  json += nfcData.firmwareVersion;
  json += "\",\"eepromVersion\":\"";
  json += nfcData.eepromVersion;
  json += "\",\"lastStatus\":\"";
  json += nfcData.lastStatus;
  json += "\",\"lastUid\":\"";
  json += nfcData.lastUid;
  json += "\",\"lastUidLength\":";
  json += String(nfcData.lastUidLength);
  json += "}";
  return json;
}

String dashboardHtml() {
  readSensor();

  String html;
  html.reserve(7000);

  html += F("<!doctype html><html lang=\"it\"><head>");
  html += F("<meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>Essiccatore</title>");
  html += F("<style>");
  html += F(":root{font-family:system-ui,-apple-system,Segoe UI,sans-serif;color:#1f2933;background:#f4f7f8}");
  html += F("body{margin:0;padding:24px}");
  html += F("main{max-width:820px;margin:auto}");
  html += F("h1{font-size:32px;margin:0 0 6px}");
  html += F(".sub{color:#52606d;margin:0 0 24px}");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px}");
  html += F(".card{background:white;border:1px solid #d9e2ec;border-radius:8px;padding:18px}");
  html += F(".wide{margin-top:14px}");
  html += F(".label{font-size:14px;color:#52606d;margin-bottom:10px}");
  html += F(".value{font-size:34px;font-weight:700;color:#102a43}");
  html += F(".small{font-size:16px;line-height:1.55;color:#102a43}");
  html += F(".status{margin-top:18px;color:#52606d;font-size:14px}");
  html += F("</style>");
  html += F("<script>");
  html += F("let secondsLeft=60;");
  html += F("async function refreshData(){try{const r=await fetch('/api/bme280');const d=await r.json();");
  html += F("if(d.valid){document.getElementById('temperature').textContent=d.temperature.toFixed(1)+' C';");
  html += F("document.getElementById('humidity').textContent=d.humidity.toFixed(1)+' %';");
  html += F("document.getElementById('pressure').textContent=d.pressure.toFixed(1)+' hPa';}");
  html += F("document.getElementById('updated').textContent='Aggiornato ora';}catch(e){}}");
  html += F("async function refreshNfc(){try{const r=await fetch('/api/nfc');const d=await r.json();");
  html += F("document.getElementById('pn5180-status').textContent=d.readerPresent?(d.rfReady?'PN5180 pronto':'PN5180 rilevato, RF non pronta'):'PN5180 non rilevato';");
  html += F("document.getElementById('pn5180-version').textContent='Product '+d.productVersion+' / Firmware '+d.firmwareVersion+' / EEPROM '+d.eepromVersion;");
  html += F("document.getElementById('pn5180-scan-status').textContent=d.lastStatus;");
  html += F("document.getElementById('pn5180-uid').textContent=d.lastUidLength>0?d.lastUid:'Nessun tag letto';}catch(e){}}");
  html += F("function updateCountdown(){document.getElementById('countdown').textContent=secondsLeft;");
  html += F("secondsLeft--;if(secondsLeft<0){secondsLeft=60;refreshData();}}");
  html += F("setInterval(updateCountdown,1000);setInterval(refreshNfc,1000);");
  html += F("window.addEventListener('load',()=>{updateCountdown();refreshData();refreshNfc();});");
  html += F("</script>");
  html += F("</head><body><main>");
  html += F("<h1>Essiccatore</h1>");
  html += F("<p class=\"sub\">Dati camera da BME280 e lettura PN5180 ISO14443A</p>");
  html += F("<section class=\"grid\">");
  html += F("<article class=\"card\"><div class=\"label\">Temperatura</div><div class=\"value\" id=\"temperature\">");
  html += valueOrDash(sensorData.temperature, " C");
  html += F("</div></article>");
  html += F("<article class=\"card\"><div class=\"label\">Umidita</div><div class=\"value\" id=\"humidity\">");
  html += valueOrDash(sensorData.humidity, " %");
  html += F("</div></article>");
  html += F("<article class=\"card\"><div class=\"label\">Pressione</div><div class=\"value\" id=\"pressure\">");
  html += valueOrDash(sensorData.pressure, " hPa");
  html += F("</div></article>");
  html += F("</section>");
  html += F("<section class=\"card wide\"><div class=\"label\">PN5180</div><div class=\"small\">");
  html += F("<strong id=\"pn5180-status\">");
  html += nfcData.readerPresent ? (nfcData.rfReady ? "PN5180 pronto" : "PN5180 rilevato, RF non pronta") : "PN5180 non rilevato";
  html += F("</strong><br><span id=\"pn5180-version\">Product ");
  html += nfcData.productVersion;
  html += F(" / Firmware ");
  html += nfcData.firmwareVersion;
  html += F(" / EEPROM ");
  html += nfcData.eepromVersion;
  html += F("</span><br>Stato lettura: <span id=\"pn5180-scan-status\">");
  html += nfcData.lastStatus;
  html += F("</span><br>Ultimo UID: <span id=\"pn5180-uid\">");
  html += nfcData.lastUidLength > 0 ? nfcData.lastUid : "Nessun tag letto";
  html += F("</span></div></section>");
  html += F("<p class=\"status\"><span id=\"updated\">Aggiornamento automatico</span> fra <strong id=\"countdown\">60</strong> secondi</p>");
  html += F("</main></body></html>");

  return html;
}

void handleRoot() {
  server.send(200, "text/html", dashboardHtml());
}

void handleApiBme280() {
  server.send(200, "application/json", sensorJson());
}

void handleApiNfc() {
  server.send(200, "application/json", nfcJson());
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Essiccatore - portale BME280 + PN5180 ISO14443A");

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!bme.begin(BME280_I2C_ADDRESS, &Wire)) {
    Serial.println("BME280 non trovato a 0x76. Controllare collegamenti.");
  } else {
    Serial.println("BME280 trovato a 0x76.");
    readSensor(true);
  }

  setupNfcReader();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.print("Access point: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("Indirizzo: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/api/bme280", handleApiBme280);
  server.on("/api/nfc", handleApiNfc);
  server.begin();

  Serial.println("Server web avviato.");
}

void loop() {
  server.handleClient();
  readSensor();
}
