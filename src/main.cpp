#include <Adafruit_BME280.h>
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

constexpr uint8_t I2C_SDA = 21;
constexpr uint8_t I2C_SCL = 22;
constexpr uint8_t BME280_I2C_ADDRESS = 0x76;

constexpr const char *AP_SSID = "Essiccatore";
constexpr const char *AP_PASSWORD = "essiccatore";

constexpr unsigned long SENSOR_READ_INTERVAL_MS = 60000;

Adafruit_BME280 bme;
WebServer server(80);

struct SensorData {
  float temperature = NAN;
  float humidity = NAN;
  float pressure = NAN;
  unsigned long lastReadMs = 0;
  bool valid = false;
};

SensorData sensorData;

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

String valueOrDash(float value, const char *suffix) {
  if (isnan(value)) {
    return "--";
  }

  return String(value, 1) + suffix;
}

String dashboardHtml() {
  readSensor();

  String html;
  html.reserve(5000);

  html += F("<!doctype html><html lang=\"it\"><head>");
  html += F("<meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<meta http-equiv=\"refresh\" content=\"60\">");
  html += F("<title>Essiccatore</title>");
  html += F("<style>");
  html += F(":root{font-family:system-ui,-apple-system,Segoe UI,sans-serif;color:#1f2933;background:#f4f7f8}");
  html += F("body{margin:0;padding:24px}");
  html += F("main{max-width:820px;margin:auto}");
  html += F("h1{font-size:32px;margin:0 0 6px}");
  html += F(".sub{color:#52606d;margin:0 0 24px}");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px}");
  html += F(".card{background:white;border:1px solid #d9e2ec;border-radius:8px;padding:18px}");
  html += F(".label{font-size:14px;color:#52606d;margin-bottom:10px}");
  html += F(".value{font-size:34px;font-weight:700;color:#102a43}");
  html += F(".status{margin-top:18px;color:#52606d;font-size:14px}");
  html += F("</style>");
  html += F("<script>");
  html += F("async function refreshData(){try{const r=await fetch('/api/bme280');const d=await r.json();");
  html += F("if(!d.valid)return;document.getElementById('temperature').textContent=d.temperature.toFixed(1)+' C';");
  html += F("document.getElementById('humidity').textContent=d.humidity.toFixed(1)+' %';");
  html += F("document.getElementById('pressure').textContent=d.pressure.toFixed(1)+' hPa';");
  html += F("document.getElementById('updated').textContent='Aggiornato ora';}catch(e){}}");
  html += F("let secondsLeft=60;");
  html += F("function updateCountdown(){document.getElementById('countdown').textContent=secondsLeft;");
  html += F("secondsLeft--;if(secondsLeft<0){secondsLeft=60;refreshData();}}");
  html += F("setInterval(updateCountdown,1000);");
  html += F("window.addEventListener('load',updateCountdown);");
  html += F("</script>");
  html += F("</head><body><main>");
  html += F("<h1>Essiccatore</h1>");
  html += F("<p class=\"sub\">Dati camera da BME280</p>");
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

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Essiccatore - portale BME280");

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!bme.begin(BME280_I2C_ADDRESS, &Wire)) {
    Serial.println("BME280 non trovato a 0x76. Controllare collegamenti.");
  } else {
    Serial.println("BME280 trovato a 0x76.");
    readSensor(true);
  }

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
  server.begin();

  Serial.println("Server web avviato.");
}

void loop() {
  server.handleClient();
  readSensor();
}
