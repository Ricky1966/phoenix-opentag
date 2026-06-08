#include "web_ui.h"
#include "config.h"
#include "utils.h"
#include "i18n.h"
#include "materials.h"
#include "sensor.h"
#include "heater.h"
#include "nfc.h"
#include "log.h"
#include <WiFi.h>
#include <ElegantOTA.h>
#include <HTTPClient.h>

WebServer server(80);

// ── Pi5 proxy helpers ─────────────────────────────────────────────────────────

static String pi5ProxyRequest(const String& method, const String& path) {
  if (!wifiClientConnected()) return "";
  WiFiClient wifiClient;
  HTTPClient http;
  String url = "http://";
  url += PI5_LOCAL_IP; url += ":"; url += String(PI5_LOCAL_PORT); url += path;
  Serial.print("pi5Proxy "); Serial.print(method); Serial.print(" "); Serial.println(url);
  if (!http.begin(wifiClient, url)) {
    Serial.println("pi5Proxy: begin() failed");
    return "";
  }
  http.setTimeout(8000);
  int code;
  if      (method == "DELETE") code = http.sendRequest("DELETE");
  else if (method == "PUT")    code = http.sendRequest("PUT");
  else if (method == "POST")   code = http.POST("");
  else                         code = http.GET();
  Serial.print("pi5Proxy code: "); Serial.println(code);
  String result = (code > 0 && code < 400) ? http.getString() : "";
  http.end();
  return result;
}

static String urlEncode(const String& s) {
  String enc = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      enc += c;
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", (unsigned char)c);
      enc += buf;
    }
  }
  return enc;
}

static String profilesFallbackJson() {
  String json = "[";
  for (uint8_t i = 0; i < totalMaterialProfileCount(); i++) {
    if (i > 0) json += ",";
    json += "{\"id\":"; json += String(i);
    json += ",\"name\":\""; json += escapeJson(materialProfileName(i));
    json += "\",\"temperature\":"; json += String(materialProfileTemp(i), 1);
    json += ",\"minutes\":"; json += String(materialProfileMinutes(i));
    json += ",\"custom\":"; json += i >= MATERIAL_PROFILE_COUNT ? "true" : "false";
    json += "}";
  }
  json += "]";
  return json;
}

static bool          wifiReconnectPending = false;
static unsigned long wifiReconnectAtMs    = 0;

static bool pi5Reachable  = false;
static bool pi5CheckDone  = false;

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

// ── WiFi helpers ─────────────────────────────────────────────────────────────

bool wifiClientConnected() {
  return appSettings.wifiClientEnabled && WiFi.status() == WL_CONNECTED;
}

String wifiClientIp() {
  return wifiClientConnected() ? WiFi.localIP().toString() : String("--");
}

void connectWifiClient(uint16_t timeoutMs) {
  if (!appSettings.wifiClientEnabled || appSettings.wifiSsid.length() == 0) {
    WiFi.disconnect(false); return;
  }
  WiFi.mode(WIFI_AP_STA); WiFi.setSleep(false);
  Serial.print("Connessione WiFi a "); Serial.println(appSettings.wifiSsid);
  WiFi.begin(appSettings.wifiSsid.c_str(), appSettings.wifiPassword.c_str());
  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) { delay(250); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) { Serial.print("WiFi connesso, IP: "); Serial.println(WiFi.localIP()); }
  else { Serial.println("WiFi non connesso: AP di emergenza resta attivo."); }
}

static void checkPi5Reachable() {
  static unsigned long lastCheckMs = 0;
  if (!wifiClientConnected()) { pi5Reachable = false; return; }
  if (pi5CheckDone && millis() - lastCheckMs < PI5_CHECK_INTERVAL_MS) return;
  lastCheckMs = millis();
  pi5CheckDone = true;
  WiFiClient client;
  pi5Reachable = client.connect(PI5_LOCAL_IP, PI5_LOCAL_PORT, 500);
  if (pi5Reachable) client.stop();
}

static void scheduleWifiReconnect(uint16_t delayMs = 1500) {
  wifiReconnectPending = true;
  wifiReconnectAtMs = millis() + delayMs;
}

void checkWifiReconnect() {
  if (wifiReconnectPending && (long)(millis() - wifiReconnectAtMs) >= 0) {
    wifiReconnectPending = false;
    connectWifiClient();
  }
}

// ── JSON serializers ──────────────────────────────────────────────────────────

String settingsJson() {
  String json = "{\"selected\":"; json += String(selectedMaterialProfile);
  json += ",\"setpoint\":"; json += String(heaterControl.setpoint, 1);
  json += ",\"cycleDurationSeconds\":";  json += String(heaterControl.cycleDurationSeconds);
  json += ",\"cycleElapsedSeconds\":";   json += String(heaterCycleElapsedSeconds());
  json += ",\"cycleRemainingSeconds\":"; json += String(heaterCycleRemainingSeconds());
  json += ",\"cycleRunning\":"; json += heaterControl.cycleRunning ? "true" : "false";
  json += ",\"safetyLimit\":";  json += String(BED_SAFETY_LIMIT_C, 1);
  json += ",\"language\":\""; json += escapeJson(appSettings.language);
  json += "\",\"compiledLocale\":\""; json += UI_LOCALE_CODE;
  json += "\",\"compiledLocaleName\":\""; json += UI_LOCALE_NAME;
  json += "\",\"wifiSsid\":\""; json += escapeJson(appSettings.wifiSsid);
  json += "\",\"wifiConfigured\":"; json += appSettings.wifiSsid.length() > 0 ? "true" : "false";
  json += ",\"wifiClientEnabled\":"; json += appSettings.wifiClientEnabled ? "true" : "false";
  json += ",\"wifiConnected\":"; json += wifiClientConnected() ? "true" : "false";
  json += ",\"wifiIp\":\""; json += wifiClientIp();
  json += "\",\"wifiRssi\":"; json += wifiClientConnected() ? String(WiFi.RSSI()) : "null";
  json += ",\"debugEnabled\":"; json += appSettings.debugEnabled ? "true" : "false";
  json += ",\"profiles\":[";
  for (uint8_t i = 0; i < totalMaterialProfileCount(); i++) {
    if (i > 0) json += ",";
    json += "{\"id\":"; json += String(i);
    json += ",\"name\":\""; json += escapeJson(materialProfileName(i));
    json += "\",\"temperature\":"; json += String(materialProfileTemp(i), 1);
    json += ",\"minutes\":"; json += String(materialProfileMinutes(i));
    json += ",\"available\":"; json += materialProfileTemp(i) <= BED_SAFETY_LIMIT_C ? "true" : "false";
    json += ",\"custom\":"; json += i >= MATERIAL_PROFILE_COUNT ? "true" : "false";
    json += "}";
  }
  json += "]}";
  return json;
}

// ── Dashboard HTML ────────────────────────────────────────────────────────────

String dashboardHtml(PageMode pageMode) {
  readSensor(); readBedTemperature();
  String html; html.reserve(10000);

  html += F("<!doctype html><html lang=\""); html += appSettings.language; html += F("\"><head>");
  html += F("<meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>"); html += tr(TK_APP_TITLE); html += F("</title>");
  html += F("<style>");
  html += F(":root{font-family:system-ui,-apple-system,Segoe UI,sans-serif;font-size:17px;color:#1f2933;background:#f4f7f8}");
  html += F("body{margin:0;padding:24px}main{max-width:1180px;margin:auto}");
  html += F("h1{font-size:34px;margin:0 0 6px}.sub{color:#52606d;margin:0 0 24px;font-size:16px}");
  html += F("nav{display:flex;gap:10px;margin:0 0 12px;align-items:center;flex-wrap:wrap}");
  html += F("nav a{border-radius:6px;background:#d9e2ec;color:#102a43;text-decoration:none;padding:10px 15px;font-weight:700;font-size:16px}");
  html += F("nav a.active{background:#0f766e;color:white}");
  html += F(".tags .home-panel,.tags .heater-panel,.tags .setpoint-panel,.tags .settings-panel,.tags .slots-panel,.slots .home-panel,.slots .tag-panel,.slots .heater-panel,.slots .setpoint-panel,.slots .settings-panel,.home .tag-panel,.home .heater-panel,.home .setpoint-panel,.home .settings-panel,.home .slots-panel,.setpoint .home-panel,.setpoint .tag-panel,.setpoint .settings-panel,.setpoint .slots-panel,.settings .home-panel,.settings .tag-panel,.settings .heater-panel,.settings .setpoint-panel,.settings .slots-panel{display:none}");
  html += F(".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:14px}");
  html += F(".card{background:white;border:1px solid #d9e2ec;border-radius:8px;padding:20px}.wide{margin-top:14px}");
  html += F(".label{font-size:15px;color:#52606d;margin-bottom:10px;font-weight:600}.value{font-size:36px;font-weight:700;color:#102a43}");
  html += F(".small{font-size:16px;line-height:1.6;color:#102a43}");
  html += F(".formgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:14px;margin-top:16px}");
  html += F("label{display:grid;gap:6px;font-size:15px;color:#52606d;font-weight:700}");
  html += F("input,select{border:1px solid #bcccdc;border-radius:6px;padding:10px 12px;font:inherit;font-size:16px;color:#102a43;background:white}");
  html += F("input[readonly]{background:#f0f4f8;color:#52606d}");
  html += F("input[type=color]{width:100%;height:44px;padding:3px;cursor:pointer}");
  html += F(".colorline{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center}");
  html += F(".hex{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:15px;color:#102a43;background:#f0f4f8;border:1px solid #d9e2ec;border-radius:6px;padding:10px 12px}");
  html += F("code{display:block;margin-top:6px;white-space:pre-wrap;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:14px;line-height:1.45}");
  html += F("button{margin-top:14px;border:0;border-radius:6px;background:#0f766e;color:white;padding:11px 16px;font-size:16px;font-weight:700;cursor:pointer}");
  html += F("button.secondary{background:#334e68;margin-left:8px}button.danger{background:#b91c1c}");
  html += F(".status{margin-top:18px;color:#52606d;font-size:15px}.warn{color:#b91c1c;font-weight:700}.ok{color:#0f766e;font-weight:700}");
  html += F("table{width:100%;border-collapse:collapse;margin-top:12px}th,td{text-align:left;border-bottom:1px solid #d9e2ec;padding:10px 8px;font-size:15px}th{font-size:14px;color:#52606d;font-weight:700}");
  html += F(".inline-control{display:flex;gap:8px;align-items:end;flex-wrap:wrap;margin-top:12px}.inline-control label{min-width:200px}.inline-control button{margin-top:0}");
  html += F(".profile-control{display:grid;grid-template-columns:1.2fr 1fr 1fr auto auto;gap:14px;align-items:end;margin-top:18px}");
  html += F(".profile-control label{font-size:16px;min-width:0}.profile-control select,.profile-control input,.profile-control .hex{height:48px;font-size:18px;font-weight:800;box-sizing:border-box}.profile-control button{height:48px;margin-top:0;padding:0 18px}");
  html += F(".heater-summary{display:flex;gap:22px;align-items:center;flex-wrap:wrap;margin:4px 0 14px}.metric{display:grid;gap:3px}.metric span{font-size:14px;color:#52606d;font-weight:700}.metric strong{font-size:24px;color:#102a43}");
  html += F(".button-row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:14px}.button-row button{margin-top:0}.output-inline{margin-left:auto;color:#52606d;font-size:15px}.output-inline strong{color:#102a43}");
  html += F(".cycle-timer{display:inline-block;margin:10px 0 6px;padding:14px 20px;border-radius:8px;background:#102a43;color:white;font-size:46px;font-weight:800;line-height:1;font-variant-numeric:tabular-nums;letter-spacing:0}");
  html += F(".cycle-caption{font-size:14px;color:#52606d;font-weight:700;margin-top:12px}");
  html += F(".tag-summary{display:flex;gap:18px;align-items:center;flex-wrap:wrap;margin-bottom:14px}.tag-summary span{font-size:16px}");
  html += F(".debug-box{background:#f8fafc;border:1px solid #d9e2ec;border-radius:6px;padding:14px;margin:0 0 14px}.debug-box .label{margin-bottom:6px}");
  html += F(".tag-actions{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:14px}.tag-actions button{margin-top:0}");
  html += F(".hum-bar-wrap{height:10px;border-radius:5px;background:#d9e2ec;margin-top:12px;overflow:hidden}.hum-bar{height:100%;border-radius:5px;transition:width .5s,background .4s}");
  html += F(".stat{font-size:14px;color:#52606d;font-weight:700;margin-bottom:4px}.stat-val{font-size:22px;font-weight:800;color:#102a43}");
  html += F(".hcard-row{display:flex;gap:16px;margin-top:8px;flex-wrap:wrap}");
  html += F(".badge{display:inline-block;padding:4px 12px;border-radius:20px;font-size:15px;font-weight:700;background:#d9e2ec;color:#334e68}");
  html += F(".badge.on{background:#dcfce7;color:#166534}.badge.auto{background:#dbeafe;color:#1e40af}");
  html += F("details{border:1px solid #d9e2ec;border-radius:8px;margin-top:14px;overflow:hidden}");
  html += F("details summary{padding:12px 16px;font-weight:700;font-size:14px;cursor:pointer;background:#f8fafc;list-style:none;display:flex;justify-content:space-between;align-items:center;color:#334e68}");
  html += F("details summary::-webkit-details-marker{display:none}details summary::after{content:'\\25BC';font-size:11px;color:#52606d;transition:transform .2s}");
  html += F("details[open] summary::after{transform:rotate(-180deg)}");
  html += F("details .formgrid{padding:14px 16px 16px;border-top:1px solid #d9e2ec}");
  html += F(".help{margin-top:16px;padding:14px 16px;background:#f0f9ff;border:1px solid #bae6fd;border-radius:8px;font-size:15px;color:#0c4a6e;line-height:1.7}");
  html += F(".help strong{color:#0369a1;display:block;margin-bottom:6px;font-size:16px}");
  html += F(".help ul{margin:4px 0 0;padding-left:20px}.help li{margin-bottom:5px}");
  html += F("</style><script>");
  html += F("async function refreshData(){try{const r=await fetch('/api/bme280');const d=await r.json();");
  html += F("if(d.valid){document.getElementById('temperature').textContent=d.temperature.toFixed(1)+' C';");
  html += F("document.getElementById('humidity').textContent=d.humidity.toFixed(1)+' %';");
  html += F("const bar=document.getElementById('humidity-bar');if(bar){const p=Math.min(d.humidity,100);bar.style.width=p+'%';bar.style.background=p<=15?'#0f766e':p<=30?'#d97706':'#b91c1c';}}}catch(e){}}");
  html += F("async function refreshBed(){try{const r=await fetch('/api/bed');const d=await r.json();document.getElementById('bed-temp').textContent=d.valid?d.temperature.toFixed(1)+' C':'--';}catch(e){}}");
  html += F("function setText(id,value){const e=document.getElementById(id);if(e)e.textContent=value;}");
  html += F("function updateNfc(d){setText('pn5180-status',d.readerPresent?(d.rfReady?'");
  html += tr(TK_NFC_READY); html += F("':'"); html += tr(TK_NFC_RF_NOT_READY); html += F("'):'"); html += tr(TK_NFC_NOT_DETECTED); html += F("');");
  html += F("setText('pn5180-version','Product '+d.productVersion+' / Firmware '+d.firmwareVersion+' / EEPROM '+d.eepromVersion);");
  html += F("setText('pn5180-scan-status',d.lastStatus);");
  html += F("setText('pn5180-uid',d.lastUidLength>0?d.lastUid:'"); html += tr(TK_TAG_NO_UID); html += F("');");
  html += F("setText('pn5180-type',d.tagType);setText('pn5180-tech',d.technologies);setText('pn5180-atqa',d.atqa);setText('pn5180-sak',d.sak);setText('pn5180-memory',d.memoryInfo);setText('pn5180-format',d.dataFormat);");
  html += F("const color=document.getElementById('tag-color');if(color&&d.openPrintTagColor){color.value=d.openPrintTagColor;syncColor();}");
  html += F("if(d.optParsed){");
  html += F("setVal('brand',d.optBrand);setVal('name',d.optMaterial);");
  html += F("const ts=document.getElementById('type-select');if(ts)ts.value=String(d.optMatType);");
  html += F("if(d.optDryTemp)setVal('drytemp',d.optDryTemp);");
  html += F("if(d.optDryTime)setVal('drytime',d.optDryTime);");
  html += F("if(d.optNozzleMin)setVal('nmin',d.optNozzleMin);");
  html += F("if(d.optNozzleMax)setVal('nmax',d.optNozzleMax);");
  html += F("if(d.optBedMin)setVal('bmin',d.optBedMin);");
  html += F("if(d.optBedMax)setVal('bmax',d.optBedMax);");
  html += F("if(d.optDensity)setVal('density',d.optDensity.toFixed(3));");
  html += F("if(d.optDiameter)setVal('diameter',d.optDiameter.toFixed(2));");
  html += F("if(d.optNomWeight)setVal('nomw',d.optNomWeight);");
  html += F("if(d.optActWeight)setVal('actw',d.optActWeight);");
  html += F("if(d.optEmptyWeight)setVal('emptyw',d.optEmptyWeight);");
  html += F("if(d.optNomLength)setVal('nomlen',d.optNomLength);");
  html += F("if(d.optActLength)setVal('actlen',d.optActLength);");
  html += F("if(d.optSpoolW)setVal('spoolw',d.optSpoolW);");
  html += F("if(d.optSpoolOd)setVal('spoolod',d.optSpoolOd);");
  html += F("if(d.optSpoolId)setVal('spoolid',d.optSpoolId);");
  html += F("if(d.optSpoolHole)setVal('spoolhole',d.optSpoolHole);");
  html += F("if(d.optCountry)setVal('country',d.optCountry);");
  html += F("}}");
  html += F("async function refreshNfc(){try{const r=await fetch('/api/nfc');updateNfc(await r.json());}catch(e){}}");
  html += F("async function readTag(){const b=document.getElementById('read-tag');const label=b.dataset.label||b.textContent;b.disabled=true;b.textContent='...';");
  html += F("let done=false;const t=setTimeout(()=>{if(!done){b.disabled=false;b.textContent=label;setText('pn5180-scan-status','Lettura senza risposta: premi EN/RST se il portale resta fermo');}},3000);");
  html += F("try{const r=await fetch('/api/nfc/read?ts='+Date.now());done=true;clearTimeout(t);updateNfc(await r.json());}catch(e){done=true;clearTimeout(t);setText('pn5180-scan-status','Errore richiesta lettura');}");
  html += F("b.disabled=false;b.textContent=label;}");
  html += F("async function writeOpenPrintTag(){if(!confirm('Scrivo questi dati sul tag appoggiato. Continuo?'))return;");
  html += F("const b=document.getElementById('write-tag');const f=document.getElementById('tag-form');const q=new URLSearchParams(new FormData(f));");
  html += F("const md=f.elements['mdate'].value;if(md){q.set('mdate',Math.floor(new Date(md+'T00:00:00Z').getTime()/1000));}");
  html += F("const label=b.dataset.label||b.textContent;b.disabled=true;b.textContent='...';");
  html += F("try{const r=await fetch('/api/nfc/write?'+q.toString()+'&ts='+Date.now());updateNfc(await r.json());}catch(e){setText('pn5180-scan-status','Errore scrittura');}");
  html += F("b.disabled=false;b.textContent=label;}");
  html += F("function syncColor(){const c=document.getElementById('tag-color');const h=document.getElementById('tag-color-hex');if(c&&h)h.textContent=c.value.toUpperCase();}");
  html += F("function setVal(name,val){const e=document.querySelector('[name=\"'+name+'\"]');if(e)e.value=val;}");
  html += F("function updateHeater(d){document.getElementById('heater-state').textContent=d.enabled?'ON':'OFF';");
  html += F("document.getElementById('heater-mode').textContent=d.automatic?'AUTO':'"); html += tr(TK_MANUAL); html += F("';");
  html += F("document.getElementById('heater-setpoint').textContent=d.setpoint.toFixed(1)+' C';");
  html += F("document.getElementById('heater-output').textContent=d.outputPercent.toFixed(1)+' %';");
  // aggiorna anche la card riepilogativa in Home
  html += F("const hs=document.getElementById('hcard-state');if(hs){hs.textContent=d.enabled?'ON':'OFF';hs.className='badge'+(d.enabled?' on':'');}");
  html += F("const hm2=document.getElementById('hcard-mode');if(hm2){hm2.textContent=d.automatic?'AUTO':'MAN';hm2.className='badge'+(d.automatic?' auto':'');}");
  html += F("const ho=document.getElementById('hcard-out');if(ho)ho.textContent=d.outputPercent.toFixed(1)+' %';");
  html += F("const hsp=document.getElementById('hcard-sp');if(hsp)hsp.textContent=d.setpoint.toFixed(1)+' C';");
  html += F("const c=document.getElementById('cycle-countdown');if(c)c.textContent=formatDuration(d.cycleRemainingSeconds);");
  html += F("const hc=document.getElementById('home-cycle-countdown');if(hc)hc.textContent=formatDuration(d.cycleRemainingSeconds);");
  html += F("const ht=document.getElementById('home-profile-temp');if(ht&&document.activeElement!==ht&&!d.automatic&&!manualHomeEdit)ht.value=d.setpoint.toFixed(1);");
  html += F("const pt=document.getElementById('profile-temp');if(pt&&document.activeElement!==pt&&!d.automatic)pt.value=d.setpoint.toFixed(1);");
  html += F("const hm=document.getElementById('home-cycle-minutes');if(hm&&document.activeElement!==hm&&!d.automatic&&!manualHomeEdit)hm.value=Math.round(d.cycleDurationSeconds/60);");
  html += F("const pm=document.getElementById('profile-minutes');if(pm&&document.activeElement!==pm&&!d.automatic)pm.value=Math.round(d.cycleDurationSeconds/60);");
  html += F("const hp=document.getElementById('home-profile');if(hp)hp.disabled=d.automatic;");
  html += F("liveSetpoint=d.setpoint||0;liveDurMin=Math.round((d.cycleDurationSeconds||0)/60);liveRunning=!!d.cycleRunning;");
  html += F("if(document.getElementById('profile-note'))syncSetpointWarning();}");
  html += F("function formatDuration(s){s=Number(s)||0;const h=Math.floor(s/3600);const m=Math.floor((s%3600)/60);const sec=s%60;return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sec).padStart(2,'0');}");
  html += F("async function refreshHeater(){try{const r=await fetch('/api/heater');updateHeater(await r.json());}catch(e){}}");
  html += F("async function setHeaterState(v){try{const r=await fetch('/api/heater/'+(v?'on':'off')+'?ts='+Date.now());updateHeater(await r.json());}catch(e){document.getElementById('heater-state').textContent='Errore';}}");
  html += F("async function setHeaterAuto(v){try{if(v&&!(await applyHomeProfile(false)))return;const r=await fetch('/api/heater/auto/'+(v?'on':'off')+'?ts='+Date.now());updateHeater(await r.json());}catch(e){const n=document.getElementById('home-profile-note');if(n){n.className='warn';n.textContent='Errore comando AUTO';}}}");
  html += F("let manualHomeEdit=false,profiles=[],liveSetpoint=0,liveDurMin=0,liveRunning=false;");
  html += F("const safetyLimit="); html += String(BED_SAFETY_LIMIT_C, 1); html += F(";");
  // Inject translated strings for JS dynamic content
  String appl = tr(TK_APPLICABLE);
  String overlim = tr(TK_OVER_LIMIT);
  String restartConf = tr(TK_RESTART_CONFIRM);
  String restarting = tr(TK_RESTARTING);
  html += F("const T={applicable:\""); html += appl; html += F("\",overlimit:\""); html += overlim;
  html += F("\",restartConfirm:\""); html += restartConf; html += F("\",restarting:\""); html += restarting; html += F("\"};");
  html += F("function readNumber(id,fallback){const e=document.getElementById(id);if(!e)return fallback;const v=String(e.value).replace(',','.');const n=parseFloat(v);return Number.isFinite(n)?n:fallback;}");
  html += F("function profileById(id){return profiles.find(p=>p.id===id)||profiles[0];}");
  html += F("function selectedProfile(){return profileById(Number(document.getElementById('material-profile').value));}");
  html += F("function selectedHomeProfile(){const s=document.getElementById('home-profile');return profileById(Number(s?s.value:0));}");
  html += F("function buildProfileOptions(){['material-profile','home-profile'].forEach(id=>{const sel=document.getElementById(id);if(!sel)return;const prev=Number(sel.value)||0;sel.innerHTML='';profiles.forEach(p=>{const o=document.createElement('option');o.value=p.id;o.textContent=p.name+(p.custom?' ★':'');if(p.id===prev)o.selected=true;sel.appendChild(o);});});}");
  html += F("function buildProfileTable(){const tb=document.getElementById('profiles-tbody');if(!tb)return;tb.innerHTML='';profiles.forEach(p=>{const a=p.temperature<=safetyLimit;const d=p.custom?'<button class=\"danger\" style=\"margin-top:0;padding:4px 10px;font-size:13px\" onclick=\"deleteProfile('+p.id+')\">&#x2715;</button>':'';tb.innerHTML+='<tr><td>'+p.name+(p.custom?' ★':'')+'</td><td>'+p.temperature.toFixed(0)+' C</td><td>'+(p.minutes/60).toFixed(1)+' h</td><td class=\"'+(a?'ok':'warn')+'\">'+(a?T.applicable:T.overlimit)+'</td><td>'+d+'</td></tr>';});}");
  html += F("async function loadProfiles(){try{const r=await fetch('/api/profiles?ts='+Date.now());profiles=await r.json();buildProfileOptions();if(document.getElementById('material-profile'))fillProfile();fillHomeProfile();buildProfileTable();}catch(e){}}");
  html += F("function syncSetpointWarning(){const p=selectedProfile();if(!p)return false;");
  html += F("const v=readNumber('profile-temp',p.temperature);const m=readNumber('profile-minutes',p.minutes);");
  html += F("const n=document.getElementById('profile-note');if(!n)return true;");
  html += F("if(v>safetyLimit){n.className='warn';n.textContent='"); html += tr(TK_SETPOINT_HELP3); html += F("';return false;}");
  // Matches running cycle (within tolerance: 0.5°C, 1 min)
  html += F("if(liveRunning&&Math.abs(v-liveSetpoint)<0.5&&Math.abs(m-liveDurMin)<=1){");
  html += F("n.className='ok';n.textContent='"); html += tr(TK_SP_CYCLE_MATCH); html += F(" ('+liveSetpoint.toFixed(1)+' C, '+liveDurMin+' min)';return true;}");
  // Values differ from running cycle
  html += F("if(liveRunning&&(Math.abs(v-liveSetpoint)>=0.5||Math.abs(m-liveDurMin)>1)){");
  html += F("n.className='warn';n.textContent='"); html += tr(TK_SP_CYCLE_DIFFERS); html += F("';return true;}");
  // Above profile recommendation
  html += F("if(v>p.temperature){n.className='warn';n.textContent='Temperatura superiore al profilo consigliato.';return true;}");
  html += F("n.className='ok';n.textContent='"); html += tr(TK_PROFILE_WITHIN_RECOMMENDED); html += F("';return true;}");
  html += F("function fillProfile(){const p=selectedProfile();if(!p)return;document.getElementById('profile-recommended').textContent=p.temperature.toFixed(1)+' C / '+Math.round(p.minutes/60)+' h';syncSetpointWarning();}");
  html += F("function loadRecommendedProfile(){const p=selectedProfile();if(!p)return;document.getElementById('profile-temp').value=p.temperature;document.getElementById('profile-minutes').value=p.minutes;syncSetpointWarning();}");
  html += F("async function applySetpoint(){if(!syncSetpointWarning())return;const p=selectedProfile();if(!p)return;const v=readNumber('profile-temp',p.temperature);");
  html += F("if(v>p.temperature&&!confirm('Temperatura superiore al profilo consigliato. Procedo comunque?'))return;");
  html += F("const minutes=Math.round(readNumber('profile-minutes',p.minutes));");
  html += F("const r=await fetch('/api/settings/material?id='+p.id+'&setpoint='+v+'&minutes='+minutes+'&ts='+Date.now());const d=await r.json();document.getElementById('settings-status').textContent='Setpoint aggiornato a '+d.setpoint.toFixed(1)+' C, durata '+Math.round(d.cycleDurationSeconds/60)+' min';refreshHeater();}");
  html += F("async function addCustomProfile(ev){if(ev)ev.preventDefault();const name=document.getElementById('custom-profile-name').value.trim();const temp=readNumber('custom-profile-temp',NaN);const minutes=Math.round(readNumber('custom-profile-minutes',NaN));const s=document.getElementById('settings-status');");
  html += F("s.className='status';if(!name){s.textContent=T.profileMissing;return;}if(!Number.isFinite(temp)||!Number.isFinite(minutes)||minutes<1){s.textContent=T.invalidInput;return;}if(temp>safetyLimit){s.className='warn';s.textContent=T.overSafety;return;}s.textContent=T.adding;");
  html += F("try{const q=new URLSearchParams({name,temperature:temp,minutes});const r=await fetch('/api/profiles?'+q.toString(),{method:'POST'});if(!r.ok){s.className='warn';s.textContent='Errore: '+(await r.text());return;}await loadProfiles();s.className='ok';s.textContent=T.added;}catch(e){s.className='warn';s.textContent=T.netError;}}");
  html += F("async function deleteProfile(id){const p=profiles.find(x=>x.id===id);if(!p||!confirm(T.confirmDelete+p.name+'\"?'))return;try{const r=await fetch('/api/profiles?id='+id,{method:'DELETE'});if(r.ok)await loadProfiles();}catch(e){}}");
  html += F("async function saveGeneralSettings(){const s=document.getElementById('general-settings-status');s.className='status';s.textContent='"); html += tr(TK_SETTINGS_SAVING); html += F("';");
  html += F("const q=new URLSearchParams({language:document.getElementById('ui-language').value,wifiSsid:document.getElementById('wifi-ssid').value,wifiPassword:document.getElementById('wifi-password').value,wifiClient:document.getElementById('wifi-client').value,debug:document.getElementById('debug-enabled').value,ts:Date.now()});");
  html += F("try{const r=await fetch('/api/settings/general?'+q.toString());if(!r.ok){s.className='warn';s.textContent='"); html += tr(TK_SETTINGS_SAVE_FAILED); html += F("';return;}");
  html += F("const d=await r.json();const connected=d.wifiConnected?'"); html += tr(TK_SETTINGS_WIFI_CONNECTED); html += F(" '+d.wifiIp:'"); html += tr(TK_SETTINGS_WIFI_NOT_CONNECTED); html += F(" --';");
  html += F("const ws=document.getElementById('wifi-status-value');if(ws)ws.textContent=connected;s.className=d.wifiConnected?'ok':'status';s.textContent='"); html += tr(TK_SETTINGS_SAVED); html += F(" '+connected+'. "); html += tr(TK_SETTINGS_AP_FALLBACK); html += F("';setTimeout(()=>location.reload(),500);}");
  html += F("catch(e){s.className='warn';s.textContent='"); html += tr(TK_SETTINGS_SAVE_ERROR); html += F("';}}");
  html += F("async function scanWifiNetworks(){const s=document.getElementById('general-settings-status');const list=document.getElementById('wifi-networks');s.className='status';s.textContent='"); html += tr(TK_SETTINGS_SCAN_START); html += F("';list.innerHTML='';");
  html += F("try{const r=await fetch('/api/wifi/scan?ts='+Date.now());const d=await r.json();if(!d.networks.length){s.className='warn';s.textContent='"); html += tr(TK_SETTINGS_SCAN_NONE); html += F("';return;}");
  html += F("d.networks.forEach((n,i)=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm'+(n.secure?', "); html += tr(TK_SETTINGS_SECURED); html += F("':'')+')';list.appendChild(o);});");
  html += F("list.disabled=false;document.getElementById('wifi-ssid').value=d.networks[0].ssid;s.className='ok';s.textContent='"); html += tr(TK_SETTINGS_SCAN_FOUND); html += F(": '+d.networks.length;}");
  html += F("catch(e){s.className='warn';s.textContent='"); html += tr(TK_SETTINGS_SCAN_ERROR); html += F("';}}");
  html += F("function chooseWifiNetwork(){const list=document.getElementById('wifi-networks');if(list&&list.value)document.getElementById('wifi-ssid').value=list.value;}");
  html += F("async function restartDevice(){if(!confirm(T.restartConfirm))return;");
  html += F("const s=document.getElementById('general-settings-status');if(s){s.className='status';s.textContent=T.restarting;}");
  html += F("try{await fetch('/api/restart');}catch(e){}");
  html += F("setTimeout(()=>location.reload(),5000);}");
  html += F("function setManualHomeEdit(v){manualHomeEdit=v;const t=document.getElementById('home-profile-temp');const m=document.getElementById('home-cycle-minutes');const b=document.getElementById('home-manual-edit');if(t)t.readOnly=!v;if(m)m.readOnly=!v;if(b)b.textContent=v?'"); html += tr(TK_LOCK_EDIT); html += F("':'"); html += tr(TK_MANUAL_EDIT); html += F("';}");
  html += F("function toggleManualHomeEdit(){setManualHomeEdit(!manualHomeEdit);const n=document.getElementById('home-profile-note');if(n&&manualHomeEdit){n.className='ok';n.textContent='"); html += tr(TK_MANUAL_EDIT_ACTIVE); html += F("';}}");
  html += F("function fillHomeProfile(){const p=selectedHomeProfile();if(!p)return;const n=document.getElementById('home-profile-note');");
  html += F("if(!manualHomeEdit){document.getElementById('home-profile-temp').value=p.temperature.toFixed(1);document.getElementById('home-cycle-minutes').value=p.minutes;}");
  html += F("document.getElementById('home-profile-recommended').textContent=p.temperature.toFixed(1)+' C / '+Math.round(p.minutes/60)+' h';");
  html += F("if(p.temperature>safetyLimit){n.className='warn';n.textContent='Profilo oltre il limite sicurezza attuale.';}else{n.className='ok';n.textContent='"); html += tr(TK_PROFILE_APPLICABLE); html += F("';}}");
  html += F("async function applyHomeProfile(showMessage=true){const p=selectedHomeProfile();if(!p)return false;");
  html += F("const setpoint=readNumber('home-profile-temp',p.temperature);");
  html += F("if(setpoint>safetyLimit){document.getElementById('home-profile-note').className='warn';document.getElementById('home-profile-note').textContent='Non applicato: oltre il limite sicurezza.';return false;}");
  html += F("const minutes=Math.round(readNumber('home-cycle-minutes',p.minutes));");
  html += F("const r=await fetch('/api/settings/material?id='+p.id+'&setpoint='+setpoint+'&minutes='+minutes+'&ts='+Date.now());const d=await r.json();");
  html += F("document.getElementById('home-profile-note').className='ok';document.getElementById('home-profile-note').textContent=showMessage?('Ciclo '+p.name+' applicato: '+d.setpoint.toFixed(1)+' C, '+Math.round(d.cycleDurationSeconds/60)+' min'):'Ciclo '+p.name+' pronto';");
  html += F("document.getElementById('heater-setpoint').textContent=d.setpoint.toFixed(1)+' C';document.getElementById('cycle-countdown').textContent=formatDuration(d.cycleRemainingSeconds);refreshHeater();return true;}");
  html += F("setInterval(()=>{refreshData();refreshBed();},60000);setInterval(()=>{refreshBed();refreshHeater();},2000);");
  html += F("window.addEventListener('load',()=>{syncColor();loadProfiles();refreshData();refreshBed();refreshNfc();refreshHeater();});");
  html += F("</script>");

  html += pageMode == PAGE_TAGS     ? F("</head><body class=\"tags\"><main>")     :
          pageMode == PAGE_SLOTS    ? F("</head><body class=\"slots\"><main>")    :
          pageMode == PAGE_SETPOINT ? F("</head><body class=\"setpoint\"><main>") :
          pageMode == PAGE_SETTINGS ? F("</head><body class=\"settings\"><main>") :
                                      F("</head><body class=\"home\"><main>");
  html += F("<div style=\"display:flex;align-items:center;gap:14px;margin-bottom:6px\">");
  html += F("<img src=\"data:image/jpeg;base64,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\" style=\"height:72px;border-radius:8px;flex-shrink:0\">");
  html += F("<div><h1 style=\"margin:0 0 4px\">"); html += tr(TK_APP_TITLE); html += F("</h1>");
  html += F("<p class=\"sub\" style=\"margin:0\">"); html += tr(TK_APP_SUBTITLE); html += F("</p></div>");
  html += F("</div>");
  html += F("<nav>");
  html += F("<a href=\"/temperature\" title=\""); html += tr(TK_NAV_HOME_TIP); html += F("\""); html += pageMode == PAGE_HOME ? F(" class=\"active\">") : F(">"); html += tr(TK_NAV_HOME); html += F("</a>");
  html += F("<a href=\"/tags\" title=\""); html += tr(TK_NAV_TAG_TIP); html += F("\""); html += pageMode == PAGE_TAGS ? F(" class=\"active\">") : F(">"); html += tr(TK_NAV_TAG); html += F("</a>");
  html += F("<a href=\"/slots\" title=\"Gestione slot bobine\""); html += pageMode == PAGE_SLOTS ? F(" class=\"active\">") : F(">"); html += F("Slot Bobine</a>");
  html += F("<a href=\"/setpoint\" title=\""); html += tr(TK_NAV_SETPOINT_TIP); html += F("\""); html += pageMode == PAGE_SETPOINT ? F(" class=\"active\">") : F(">"); html += tr(TK_NAV_SETPOINT); html += F("</a>");
  html += F("<a href=\"/settings\" title=\""); html += tr(TK_NAV_SETTINGS_TIP); html += F("\""); html += pageMode == PAGE_SETTINGS ? F(" class=\"active\">") : F(">"); html += tr(TK_NAV_SETTINGS); html += F("</a>");
  html += F("<span style=\"margin-left:auto;display:flex;gap:8px;align-items:center;flex-wrap:wrap\">");
  html += F("<span style=\"font-size:13px;color:#1e40af;background:#dbeafe;padding:3px 10px;border-radius:20px;font-weight:600\">&#x1F512; Tailscale</span>");
  if (wifiClientConnected()) {
    html += F("<span style=\"font-size:12px;color:#166534;background:#dcfce7;padding:3px 10px;border-radius:20px;font-weight:600\">&#x1F4F6; ");
    html += WiFi.localIP().toString();
    html += F("</span>");
  } else {
    html += F("<span style=\"font-size:12px;color:#92400e;background:#fef3c7;padding:3px 10px;border-radius:20px;font-weight:600\">&#x1F4E1; AP 192.168.4.1</span>");
  }
  if (wifiClientConnected() && pi5CheckDone && !pi5Reachable) {
    html += F("<span style=\"font-size:13px;color:#b91c1c;background:#fee2e2;padding:3px 10px;border-radius:20px;font-weight:700\">&#x26A0; Accendi il Raspberry!</span>");
  }
  html += F("</span></nav>");

  // ── Home panel
  html += F("<section class=\"grid home-panel\">");
  html += F("<article class=\"card\"><div class=\"label\">"); html += tr(TK_CARD_TEMPERATURE); html += F("</div><div class=\"value\" id=\"temperature\">"); html += valueOrDash(sensorData.temperature, " C"); html += F("</div></article>");
  html += F("<article class=\"card\"><div class=\"label\">"); html += tr(TK_CARD_HUMIDITY); html += F("</div><div class=\"value\" id=\"humidity\">"); html += valueOrDash(sensorData.humidity, " %"); html += F("</div><div class=\"hum-bar-wrap\"><div class=\"hum-bar\" id=\"humidity-bar\"></div></div></article>");
  html += F("<article class=\"card\"><div class=\"label\">"); html += tr(TK_CARD_BED); html += F("</div><div class=\"value\" id=\"bed-temp\">"); html += valueOrDash(bedData.temperature, " C"); html += F("</div></article>");
  html += F("<article class=\"card\"><div class=\"label\">"); html += tr(TK_CYCLE_COUNTDOWN); html += F("</div><div class=\"value\" style=\"font-size:26px;font-variant-numeric:tabular-nums\" id=\"home-cycle-countdown\">--:--:--</div></article>");
  // card riepilogativa riscaldatore
  html += F("<article class=\"card\"><div class=\"label\">"); html += tr(TK_HEATER); html += F("</div>");
  html += F("<div class=\"hcard-row\">");
  html += F("<div><div class=\"stat\">"); html += tr(TK_STATE); html += F("</div><span class=\"badge\" id=\"hcard-state\">"); html += heaterEnabled ? "ON" : "OFF"; html += F("</span></div>");
  html += F("<div><div class=\"stat\">"); html += tr(TK_MODE); html += F("</div><span class=\"badge\" id=\"hcard-mode\">"); html += heaterControl.automatic ? "AUTO" : "MAN"; html += F("</span></div>");
  html += F("<div><div class=\"stat\">"); html += tr(TK_SETPOINT); html += F("</div><div class=\"stat-val\" id=\"hcard-sp\">"); html += String(heaterControl.setpoint, 1); html += F(" C</div></div>");
  html += F("<div><div class=\"stat\">"); html += tr(TK_OUTPUT); html += F("</div><div class=\"stat-val\" id=\"hcard-out\">"); html += String(heaterControl.outputPercent, 1); html += F(" %</div></div>");
  html += F("</div></article>");
  html += F("</section>");
  html += F("<div class=\"help home-panel\"><strong>&#x2139; Questa pagina</strong>");
  html += F("<ul>");
  html += F("<li>"); html += tr(TK_HOME_HELP1); html += F("</li>");
  html += F("<li>"); html += tr(TK_HOME_HELP2); html += F("</li>");
  html += F("<li>"); html += tr(TK_HOME_HELP3); html += F("</li>");
  html += F("<li>"); html += tr(TK_HOME_HELP4); html += F("</li>");
  html += F("</ul>");
  html += F("Le altre sezioni: <b>Lettura TAG</b> — leggi e scrivi bobine NFC &nbsp;|&nbsp; <b>SetPoint</b> — imposta profilo e avvia ciclo &nbsp;|&nbsp; <b>Settings</b> — WiFi, lingua, OTA.</div>");
  // ── Grafico storico (Home) ─────────────────────────────────────────────
  html += F("<div class=\"card wide home-panel\" style=\"margin-top:14px\"><div class=\"label\">Storico (ultime 4 ore)</div>");
  html += F("<canvas id=\"log-chart\" width=\"700\" height=\"180\" style=\"width:100%;height:180px\"></canvas>");
  html += F("<script>");
  html += F("(async()=>{try{const r=await fetch('/api/log');const d=await r.json();");
  html += F("if(!d.entries||!d.entries.length){document.getElementById('log-chart').style.display='none';return;}");
  html += F("const c=document.getElementById('log-chart');const ctx=c.getContext('2d');");
  html += F("const W=c.offsetWidth||700,H=180;c.width=W;");
  html += F("const now=d.nowMs;const entries=d.entries;");
  html += F("const pad={l:38,r:10,t:10,b:28};const cw=W-pad.l-pad.r,ch=H-pad.t-pad.b;");
  html += F("const minT=Math.min(...entries.map(e=>e.t));const maxT=now;");
  html += F("const tx=t=>pad.l+cw*(t-minT)/(maxT-minT||1);");
  html += F("const vals=(k)=>entries.filter(e=>e[k]!==null).map(e=>e[k]);");
  html += F("const allV=[...vals('temp'),...vals('hum'),...vals('bed')];");
  html += F("if(!allV.length)return;");
  html += F("const minV=Math.min(...allV)-2,maxV=Math.max(...allV)+2;");
  html += F("const ty=v=>pad.t+ch*(1-(v-minV)/(maxV-minV||1));");
  html += F("ctx.clearRect(0,0,W,H);");
  html += F("ctx.strokeStyle='#d9e2ec';ctx.lineWidth=1;");
  html += F("for(let v=Math.ceil(minV/10)*10;v<=maxV;v+=10){const y=ty(v);ctx.beginPath();ctx.moveTo(pad.l,y);ctx.lineTo(W-pad.r,y);ctx.stroke();ctx.fillStyle='#52606d';ctx.font='10px system-ui';ctx.textAlign='right';ctx.fillText(v+'',pad.l-3,y+3);}");
  html += F("const drawLine=(key,color)=>{const pts=entries.filter(e=>e[key]!==null);if(pts.length<2)return;ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();pts.forEach((e,i)=>i===0?ctx.moveTo(tx(e.t),ty(e[key])):ctx.lineTo(tx(e.t),ty(e[key])));ctx.stroke();};");
  html += F("drawLine('temp','#0f766e');drawLine('hum','#3b82f6');drawLine('bed','#f97316');");
  html += F("ctx.fillStyle='#0f766e';ctx.font='11px system-ui';ctx.textAlign='left';ctx.fillText('Temp',pad.l+4,pad.t+13);");
  html += F("ctx.fillStyle='#3b82f6';ctx.fillText('Umid',pad.l+52,pad.t+13);");
  html += F("ctx.fillStyle='#f97316';ctx.fillText('Piatto',pad.l+100,pad.t+13);");
  html += F("}catch(e){}})();");
  html += F("</script></div>");

  // ── Tag panel
  html += F("<section class=\"card wide tag-panel\"><div class=\"label\">OpenPrintTag</div><div class=\"small\">");
  html += F("<div class=\"tag-summary\"><span><strong>"); html += tr(TK_TAG_STATUS); html += F(":</strong> <span id=\"pn5180-status\">");
  html += nfcData.readerPresent ? (nfcData.rfReady ? tr(TK_NFC_READY) : tr(TK_NFC_RF_NOT_READY)) : tr(TK_NFC_NOT_DETECTED);
  html += F("</span></span><span><strong>"); html += tr(TK_TAG_UID); html += F(":</strong> <span id=\"pn5180-uid\">");
  html += nfcData.lastUidLength > 0 ? nfcData.lastUid : String(tr(TK_TAG_NO_UID));
  html += F("</span></span><span><strong>"); html += tr(TK_TAG_LAST_READ); html += F(":</strong> <span id=\"pn5180-scan-status\">"); html += nfcData.lastStatus; html += F("</span></span></div>");
  if (appSettings.debugEnabled) {
    html += F("<div class=\"debug-box\"><div class=\"label\">"); html += tr(TK_TAG_DEBUG); html += F("</div>");
    html += F("<span id=\"pn5180-version\">Product "); html += nfcData.productVersion; html += F(" / Firmware "); html += nfcData.firmwareVersion; html += F(" / EEPROM "); html += nfcData.eepromVersion; html += F("</span><br>");
    html += tr(TK_TAG_TYPE); html += F(": <span id=\"pn5180-type\">"); html += nfcData.tagType; html += F("</span><br>");
    html += tr(TK_TAG_TECH); html += F(": <span id=\"pn5180-tech\">"); html += nfcData.technologies; html += F("</span><br>");
    html += F("ATQA: <span id=\"pn5180-atqa\">"); html += nfcData.atqa; html += F("</span><br>");
    html += F("SAK: <span id=\"pn5180-sak\">"); html += nfcData.sak; html += F("</span><br>");
    html += tr(TK_TAG_MEMORY); html += F(": <span id=\"pn5180-memory\">"); html += nfcData.memoryInfo; html += F("</span><br>");
    html += tr(TK_TAG_FORMAT); html += F(": <span id=\"pn5180-format\">"); html += nfcData.dataFormat; html += F("</span></div>");
  }
  html += F("<div class=\"help\"><strong>"); html += tr(TK_HELP_HOW_TO_USE); html += F("</strong><ul>");
  html += F("<li>"); html += tr(TK_TAG_HELP1); html += F("</li>");
  html += F("<li>"); html += tr(TK_TAG_HELP2); html += F("</li>");
  html += F("<li>"); html += tr(TK_TAG_HELP3); html += F("</li>");
  html += F("<li>"); html += tr(TK_TAG_HELP4); html += F("</li>");
  html += F("</ul></div>");
  html += F("<form id=\"tag-form\" onsubmit=\"event.preventDefault();writeOpenPrintTag();\">");
  // Gruppo 1: Identità bobina
  html += F("<details open><summary>"); html += tr(TK_TAG_MATERIAL); html += F(" &amp; "); html += tr(TK_TAG_COLOR); html += F("</summary><div class=\"formgrid\">");
  html += F("<label>"); html += tr(TK_TAG_BRAND);    html += F("<input name=\"brand\" maxlength=\"31\" value=\"ODIN\"></label>");
  html += F("<label>"); html += tr(TK_TAG_MATERIAL);  html += F("<input name=\"name\" maxlength=\"31\" value=\"Test PLA\"></label>");
  html += F("<label>"); html += tr(TK_TAG_TYPE);      html += F("<select id=\"type-select\" name=\"type\"><option value=\"0\">PLA</option><option value=\"1\">PETG</option><option value=\"2\">TPU</option><option value=\"3\">ABS</option><option value=\"4\">ASA</option><option value=\"5\">PC</option></select></label>");
  html += F("<label>"); html += tr(TK_TAG_COLOR);     html += F("<div class=\"colorline\"><input id=\"tag-color\" name=\"color\" type=\"color\" value=\"#ffffff\" oninput=\"syncColor()\"><span class=\"hex\" id=\"tag-color-hex\">#FFFFFF</span></div></label>");
  html += F("<label>"); html += tr(TK_TAG_DENSITY);    html += F("<input name=\"density\" type=\"number\" min=\"0\" max=\"5\" step=\"0.01\" value=\"1.24\"></label>");
  html += F("<label>"); html += tr(TK_TAG_DIAMETER);   html += F("<input name=\"diameter\" type=\"number\" min=\"0\" max=\"5\" step=\"0.01\" value=\"1.75\"></label>");
  html += F("</div></details>");
  // Gruppo 2: Pesi e lunghezze
  html += F("<details><summary>"); html += tr(TK_TAG_NOMINAL_WEIGHT); html += F(" / "); html += tr(TK_TAG_NOMINAL_LENGTH); html += F("</summary><div class=\"formgrid\">");
  html += F("<label>"); html += tr(TK_TAG_NOMINAL_WEIGHT); html += F("<input name=\"nomw\" type=\"number\" min=\"0\" max=\"9999\" value=\"1000\"></label>");
  html += F("<label>"); html += tr(TK_TAG_ACTUAL_WEIGHT);  html += F("<input name=\"actw\" type=\"number\" min=\"0\" max=\"9999\" value=\"1000\"></label>");
  html += F("<label>"); html += tr(TK_TAG_EMPTY_WEIGHT);   html += F("<input name=\"emptyw\" type=\"number\" min=\"0\" max=\"9999\" value=\"250\"></label>");
  html += F("<label>"); html += tr(TK_TAG_NOMINAL_LENGTH); html += F("<input name=\"nomlen\" type=\"number\" min=\"0\" max=\"999999\" value=\"330000\"></label>");
  html += F("<label>"); html += tr(TK_TAG_ACTUAL_LENGTH);  html += F("<input name=\"actlen\" type=\"number\" min=\"0\" max=\"999999\" value=\"330000\"></label>");
  html += F("<label>"); html += tr(TK_TAG_MANUFACTURE_DATE); html += F("<input name=\"mdate\" type=\"date\"></label>");
  html += F("<label>"); html += tr(TK_TAG_COUNTRY);    html += F("<input name=\"country\" maxlength=\"2\" value=\"IT\"></label>");
  html += F("</div></details>");
  // Gruppo 3: Parametri stampa ed essiccazione
  html += F("<details><summary>"); html += tr(TK_TAG_NOZZLE_MIN); html += F(" / "); html += tr(TK_TAG_DRY_TEMP); html += F("</summary><div class=\"formgrid\">");
  html += F("<label>"); html += tr(TK_TAG_NOZZLE_MIN); html += F("<input name=\"nmin\" type=\"number\" min=\"0\" max=\"350\" value=\"205\"></label>");
  html += F("<label>"); html += tr(TK_TAG_NOZZLE_MAX); html += F("<input name=\"nmax\" type=\"number\" min=\"0\" max=\"350\" value=\"215\"></label>");
  html += F("<label>"); html += tr(TK_TAG_BED_MIN);    html += F("<input name=\"bmin\" type=\"number\" min=\"0\" max=\"140\" value=\"55\"></label>");
  html += F("<label>"); html += tr(TK_TAG_BED_MAX);    html += F("<input name=\"bmax\" type=\"number\" min=\"0\" max=\"140\" value=\"60\"></label>");
  html += F("<label>"); html += tr(TK_TAG_DRY_TEMP);   html += F("<input name=\"drytemp\" type=\"number\" min=\"0\" max=\"120\" value=\"45\"></label>");
  html += F("<label>"); html += tr(TK_TAG_DRY_TIME);   html += F("<input name=\"drytime\" type=\"number\" min=\"0\" max=\"9999\" value=\"240\"></label>");
  html += F("</div></details>");
  // Gruppo 4: Dimensioni bobina
  html += F("<details><summary>"); html += tr(TK_TAG_SPOOL_WIDTH); html += F("</summary><div class=\"formgrid\">");
  html += F("<label>"); html += tr(TK_TAG_SPOOL_WIDTH); html += F("<input name=\"spoolw\" type=\"number\" min=\"0\" max=\"999\" value=\"68\"></label>");
  html += F("<label>"); html += tr(TK_TAG_SPOOL_OD);   html += F("<input name=\"spoolod\" type=\"number\" min=\"0\" max=\"999\" value=\"200\"></label>");
  html += F("<label>"); html += tr(TK_TAG_SPOOL_ID);   html += F("<input name=\"spoolid\" type=\"number\" min=\"0\" max=\"999\" value=\"100\"></label>");
  html += F("<label>"); html += tr(TK_TAG_SPOOL_HOLE); html += F("<input name=\"spoolhole\" type=\"number\" min=\"0\" max=\"999\" value=\"52\"></label>");
  html += F("</div></details>");
  html += F("</form><div class=\"tag-actions\">");
  html += F("<button id=\"read-tag\" data-label=\""); html += tr(TK_TAG_READ); html += F("\" onclick=\"readTag()\">"); html += tr(TK_TAG_READ); html += F("</button>");
  html += F("<button id=\"write-tag\" data-label=\""); html += tr(TK_TAG_WRITE); html += F("\" onclick=\"writeOpenPrintTag()\">"); html += tr(TK_TAG_WRITE); html += F("</button>");
  html += F("</div></div></section>");

  // ── Slots panel
  html += F("<section class=\"card wide slots-panel\"><div class=\"label\">Slot Bobine (Essiccazione Differenziata)</div><div class=\"small\">");
  html += F("<div id=\"slots-container\" style=\"display:grid;grid-template-columns:1fr;gap:14px;margin-top:14px\"></div>");
  html += F("<div style=\"margin-top:14px;text-align:center\">");
  html += F("<button type=\"button\" onclick=\"clearAllSlots()\">Cancella tutti slot</button>");
  html += F("</div>");
  html += F("</div></section>");

  html += F("<script>");
  html += F("async function refreshSlots(){try{const r=await fetch('/api/slots');const d=await r.json();const container=document.getElementById('slots-container');if(!container)return;");
  html += F("container.innerHTML='';for(let i=0;i<d.slots.length;i++){const s=d.slots[i];const card=document.createElement('div');card.className='card';");
  html += F("let html='<div class=\"label\">Slot '+s.index+'</div>';");
  html += F("if(!s.occupied){html+='<div style=\"color:#52606d;padding:20px;text-align:center\">Vuoto</div>';}");
  html += F("else{html+='<div style=\"margin-bottom:12px\"><strong>'+s.brand+' / '+s.material+'</strong><br><small>UID: '+s.uid+'</small></div>';");
  html += F("html+='<div style=\"display:grid;gap:6px;margin-bottom:12px\"><div><small>Temperatura:</small> <strong>'+s.dryingTemp+' °C</strong></div><div><small>Durata:</small> <strong>'+s.dryingMinutes+' min</strong></div>';");
  html += F("if(s.cycleRunning){html+='<div><small>Ciclo in corso:</small> <span style=\"color:#0f766e;font-weight:700\">'+formatDuration(s.cycleRemainingSeconds)+'</span></div>';}");
  html += F("html+='</div>';");
  html += F("html+='<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:8px\"><input type=\"number\" min=\"20\" max=\"90\" value=\"'+s.dryingTemp+'\" id=\"temp-slot-'+i+'\" placeholder=\"Temp\"><input type=\"number\" min=\"1\" max=\"9999\" value=\"'+s.dryingMinutes+'\" id=\"min-slot-'+i+'\" placeholder=\"Min\">';");
  html += F("html+='</div>';");
  html += F("html+='<button style=\"width:100%;margin-top:8px\" onclick=\"updateSlotDrying('+i+')\">Aggiorna tempi</button>';}");
  html += F("card.innerHTML=html;container.appendChild(card);}");
  html += F("}catch(e){console.log('Error refreshing slots:',e);}");
  html += F("}");
  html += F("async function updateSlotDrying(slot){const temp=document.getElementById('temp-slot-'+slot).value;const min=document.getElementById('min-slot-'+slot).value;");
  html += F("try{const r=await fetch('/api/slot/drying-time?slot='+slot+'&temperature='+temp+'&minutes='+min);if(r.ok){refreshSlots();}}catch(e){}}");
  html += F("async function clearAllSlots(){if(!confirm('Cancellare tutti gli slot?'))return;try{const r=await fetch('/api/slots/clear-all',{method:'POST'});if(r.ok){refreshSlots();}}catch(e){}}");
  html += F("setInterval(refreshSlots,2000);window.addEventListener('load',refreshSlots);");
  html += F("</script>");

  // ── Heater panel
  html += F("<section class=\"card wide heater-panel\"><div class=\"label\">"); html += tr(TK_HEATER); html += F("</div><div class=\"small\">");
  html += F("<div class=\"heater-summary\">");
  html += F("<div class=\"metric\"><span>"); html += tr(TK_STATE);   html += F("</span><strong id=\"heater-state\">");   html += heaterEnabled ? "ON" : "OFF"; html += F("</strong></div>");
  html += F("<div class=\"metric\"><span>"); html += tr(TK_MODE);    html += F("</span><strong id=\"heater-mode\">");    html += heaterControl.automatic ? F("AUTO") : tr(TK_MANUAL); html += F("</strong></div>");
  html += F("<div class=\"metric\"><span>"); html += tr(TK_SETPOINT); html += F("</span><strong id=\"heater-setpoint\">"); html += String(heaterControl.setpoint, 1); html += F(" C</strong></div>");
  html += F("</div><div class=\"profile-control\"><label>"); html += tr(TK_PROFILE_MATERIAL);
  html += F("<select id=\"home-profile\" onchange=\"fillHomeProfile()\"></select></label><label>"); html += tr(TK_CYCLE_SETPOINT);
  html += F("<input id=\"home-profile-temp\" type=\"number\" min=\"20\" max=\"120\" step=\"0.5\" readonly value=\""); html += String(heaterControl.setpoint, 1); html += F("\"></label><label>"); html += tr(TK_CYCLE_DURATION_MIN);
  html += F("<input id=\"home-cycle-minutes\" type=\"number\" min=\"1\" max=\"9999\" step=\"1\" readonly value=\""); html += String(heaterControl.cycleDurationSeconds / 60UL); html += F("\"></label>");
  html += F("<button type=\"button\" onclick=\"applyHomeProfile()\">"); html += tr(TK_APPLY_CYCLE); html += F("</button>");
  html += F("<button type=\"button\" class=\"secondary\" id=\"home-manual-edit\" onclick=\"toggleManualHomeEdit()\">"); html += tr(TK_MANUAL_EDIT); html += F("</button></div>");
  html += F("<p class=\"status\">"); html += tr(TK_RECOMMENDED); html += F(": <span id=\"home-profile-recommended\">--</span></p>");
  html += F("<p class=\"status\" id=\"home-profile-note\">--</p>");
  html += F("<div class=\"cycle-caption\">"); html += tr(TK_CYCLE_COUNTDOWN); html += F("</div><div class=\"cycle-timer\" id=\"cycle-countdown\">--:--:--</div>");
  html += F("<div class=\"button-row\">");
  html += F("<button onclick=\"setHeaterAuto(true)\">AUTO</button>");
  html += F("<button class=\"secondary\" onclick=\"setHeaterAuto(false)\">STOP AUTO</button>");
  html += F("<button class=\"danger\" onclick=\"setHeaterState(true)\">"); html += tr(TK_MANUAL_ON); html += F("</button>");
  html += F("<button class=\"secondary\" onclick=\"setHeaterState(false)\">OFF</button>");
  html += F("<span class=\"output-inline\">"); html += tr(TK_OUTPUT); html += F(" <strong id=\"heater-output\">"); html += String(heaterControl.outputPercent, 1); html += F(" %</strong></span>");
  html += F("</div></div></section>");

  // ── Setpoint panel
  html += F("<section class=\"card wide setpoint-panel\"><div class=\"label\">"); html += tr(TK_SETPOINT_SETTINGS); html += F("</div><div class=\"small\">");
  html += F("<div class=\"help\"><strong>"); html += tr(TK_HELP_HOW_TO_USE); html += F("</strong><ul>");
  html += F("<li>"); html += tr(TK_SETPOINT_HELP1); html += F("</li>");
  html += F("<li>"); html += tr(TK_SETPOINT_HELP2); html += F("</li>");
  html += F("<li>"); html += tr(TK_SETPOINT_HELP3); html += F("</li>");
  html += F("<li>"); html += tr(TK_SETPOINT_HELP4); html += F("</li>");
  html += F("<li>"); html += tr(TK_SETPOINT_HELP5); html += F("</li>");
  html += F("</ul></div>");
  html += F("<div class=\"formgrid\"><label>"); html += tr(TK_TABLE_MATERIAL);
  html += F("<select id=\"material-profile\" onchange=\"loadRecommendedProfile()\"></select></label><label>"); html += tr(TK_DRYER_SETPOINT_C);
  html += F("<input id=\"profile-temp\" type=\"number\" min=\"20\" max=\"120\" step=\"0.5\" value=\""); html += String(heaterControl.setpoint, 1); html += F("\" oninput=\"syncSetpointWarning()\"></label>");
  html += F("<label>"); html += tr(TK_CYCLE_DURATION_MIN);
  html += F("<input id=\"profile-minutes\" type=\"number\" min=\"1\" max=\"9999\" step=\"1\" value=\""); html += String(heaterControl.cycleDurationSeconds / 60UL); html += F("\"></label>");
  html += F("<label>"); html += tr(TK_RECOMMENDED_PROFILE); html += F("<div class=\"hex\" id=\"profile-recommended\">--</div></label></div>");
  html += F("<p class=\"status\" id=\"profile-note\">--</p>");
  html += F("<button type=\"button\" onclick=\"applySetpoint()\">"); html += tr(TK_APPLY_SETPOINT); html += F("</button>");
  html += F("<p class=\"status\" id=\"settings-status\">"); html += tr(TK_SETPOINT_HELP); html += F("</p>");
  html += F("<div class=\"label\" style=\"margin-top:24px\">"); html += tr(TK_CUSTOM_PROFILE); html += F("</div>");
  html += F("<div class=\"formgrid\"><label>"); html += tr(TK_PROFILE_NAME); html += F("<input id=\"custom-profile-name\" maxlength=\"24\" placeholder=\"PLA 0.5KG\"></label>");
  html += F("<label>"); html += tr(TK_DRYER_SETPOINT_C); html += F("<input id=\"custom-profile-temp\" type=\"number\" min=\"20\" max=\"120\" step=\"0.5\" value=\"45\"></label>");
  html += F("<label>"); html += tr(TK_DURATION_MIN); html += F("<input id=\"custom-profile-minutes\" type=\"number\" min=\"1\" max=\"9999\" step=\"1\" value=\"180\"></label>");
  html += F("<label>&nbsp;<button type=\"button\" onclick=\"addCustomProfile(event)\">"); html += tr(TK_ADD_PROFILE); html += F("</button></label></div>");
  html += F("<table><thead><tr><th>"); html += tr(TK_TABLE_MATERIAL); html += F("</th><th>"); html += tr(TK_TABLE_TEMP); html += F("</th><th>"); html += tr(TK_TABLE_TIME); html += F("</th><th>"); html += tr(TK_TABLE_STATUS); html += F("</th><th></th></tr></thead><tbody id=\"profiles-tbody\"></tbody></table></div></section>");

  // ── Settings panel
  html += F("<section class=\"card wide settings-panel\"><div class=\"label\">"); html += tr(TK_SETTINGS_NETWORK); html += F("</div><div class=\"small\">");
  html += F("<div class=\"help\"><strong>"); html += tr(TK_HELP_HOW_TO_USE); html += F("</strong><ul>");
  html += F("<li>"); html += tr(TK_SETTINGS_HELP1); html += F("</li>");
  html += F("<li>"); html += tr(TK_SETTINGS_HELP2); html += F("</li>");
  html += F("<li>"); html += tr(TK_SETTINGS_HELP3); html += F("</li>");
  html += F("<li>"); html += tr(TK_SETTINGS_HELP4); html += F("</li>");
  html += F("</ul></div>");
  html += F("<div class=\"formgrid\"><label>"); html += tr(TK_SETTINGS_LANGUAGE);
  html += F("<select id=\"ui-language\"><option value=\""); html += UI_LOCALE_CODE; html += appSettings.language == UI_LOCALE_CODE ? F("\" selected>") : F("\">"); html += UI_LOCALE_NAME;
  html += F("</option><option value=\"en\""); html += appSettings.language == "en" ? F(" selected") : F(""); html += F(">English</option></select></label>");
  html += F("<label>"); html += tr(TK_SETTINGS_WIFI_SSID); html += F("<input id=\"wifi-ssid\" maxlength=\"32\" value=\""); html += escapeHtml(appSettings.wifiSsid); html += F("\"></label>");
  html += F("<label>"); html += tr(TK_SETTINGS_WIFI_NETWORKS); html += F("<select id=\"wifi-networks\" onchange=\"chooseWifiNetwork()\" disabled><option value=\"\">--</option></select></label>");
  html += F("<label>"); html += tr(TK_SETTINGS_WIFI_PASSWORD); html += F("<input id=\"wifi-password\" type=\"password\" maxlength=\"64\" value=\""); html += escapeHtml(appSettings.wifiPassword); html += F("\"></label>");
  html += F("<label>"); html += tr(TK_SETTINGS_WIFI_MODE);
  html += F("<select id=\"wifi-client\"><option value=\"0\""); html += appSettings.wifiClientEnabled ? F("") : F(" selected"); html += F(">"); html += tr(TK_SETTINGS_WIFI_DISABLED);
  html += F("</option><option value=\"1\""); html += appSettings.wifiClientEnabled ? F(" selected") : F(""); html += F(">"); html += tr(TK_SETTINGS_WIFI_SAVED); html += F("</option></select></label>");
  html += F("<label>"); html += tr(TK_SETTINGS_DEBUG);
  html += F("<select id=\"debug-enabled\"><option value=\"0\""); html += appSettings.debugEnabled ? F("") : F(" selected"); html += F(">"); html += tr(TK_SETTINGS_DEBUG_OFF);
  html += F("</option><option value=\"1\""); html += appSettings.debugEnabled ? F(" selected") : F(""); html += F(">"); html += tr(TK_SETTINGS_DEBUG_ON); html += F("</option></select></label></div>");
  html += F("<div class=\"status\"><strong>"); html += tr(TK_FIRMWARE_LOCALE); html += F(":</strong> "); html += UI_LOCALE_NAME;
  html += F(" / <strong>"); html += tr(TK_FALLBACK_LOCALE); html += F(":</strong> English</div>");
  html += F("<div class=\"status\"><strong>"); html += tr(TK_SETTINGS_WIFI_STATUS); html += F(":</strong> <span id=\"wifi-status-value\">");
  html += wifiClientConnected() ? tr(TK_SETTINGS_WIFI_CONNECTED) : tr(TK_SETTINGS_WIFI_NOT_CONNECTED); html += F(" "); html += wifiClientIp(); html += F("</span></div>");
  html += F("<button type=\"button\" class=\"secondary\" onclick=\"scanWifiNetworks()\">"); html += tr(TK_SETTINGS_WIFI_SCAN); html += F("</button>");
  html += F("<button type=\"button\" onclick=\"saveGeneralSettings()\">"); html += tr(TK_SETTINGS_SAVE); html += F("</button>");
  html += F("<button type=\"button\" class=\"danger\" onclick=\"restartDevice()\">&#x21BA; "); html += tr(TK_RESTART); html += F("</button>");
  html += F("<p class=\"status\" id=\"general-settings-status\">"); html += tr(TK_SETTINGS_NOTE); html += F("</p>");
  // OTA section
  html += F("<div class=\"label\" style=\"margin-top:24px\">Firmware</div>");
  html += F("<div class=\"status\" style=\"margin-bottom:12px\">Versione: <strong id=\"fw-version\">"); html += FIRMWARE_VERSION; html += F("</strong> <span id=\"fw-status\" style=\"margin-left:8px\"></span></div>");
  html += F("<p class=\"status\">"); html += tr(TK_SETTINGS_OTA_BTN); html += F(": <a href=\"/update\" target=\"_blank\" style=\"color:#0f766e;font-weight:700\">/update</a></p>");
  html += F("<div class=\"label\" style=\"margin-top:24px\">"); html += tr(TK_SETTINGS_OTA); html += F("</div>");
  html += F("<p class=\"status\">Aggiornamento OTA disponibile su: <a href=\"/update\" target=\"_blank\" style=\"color:#0f766e;font-weight:700\">/update</a></p>");
  html += F("<script>");
  html += F("async function checkFirmwareUpdate(){try{const r=await fetch('/api/version');const d=await r.json();");
  html += F("const gh=await fetch('https://api.github.com/repos/Ricky1966/Odin/releases/latest');const gr=await gh.json();");
  html += F("const latest=gr.tag_name?.replace('v','');const current=d.version;");
  html += F("const status=document.getElementById('fw-status');if(latest&&latest!==current){");
  html += F("status.innerHTML='<span style=\"color:#b91c1c;font-weight:700\">Aggiornamento: '+latest+'</span>';}");
  html += F("else{status.innerHTML='<span style=\"color:#0f766e\">✓ Aggiornato</span>';}");
  html += F("}catch(e){console.log('Update check failed',e);}");
  html += F("document.addEventListener('DOMContentLoaded',checkFirmwareUpdate);");
  html += F("</script>");
  html += F("</div></section>");
  html += F("</main></body></html>");
  return html;
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────

static void handleRoot()            { Serial.println("HTTP GET /");           server.sendHeader("Cache-Control","no-store"); server.send(200,"text/html",dashboardHtml(PAGE_HOME)); }
static void handleTemperaturePage() { Serial.println("HTTP GET /temperature"); server.sendHeader("Cache-Control","no-store"); server.send(200,"text/html",dashboardHtml(PAGE_HOME)); }
static void handleTagsPage()        { Serial.println("HTTP GET /tags");        server.sendHeader("Cache-Control","no-store"); server.send(200,"text/html",dashboardHtml(PAGE_TAGS)); }
static void handleSlotsPage()       { Serial.println("HTTP GET /slots");       server.sendHeader("Cache-Control","no-store"); server.send(200,"text/html",dashboardHtml(PAGE_SLOTS)); }
static void handleSetpointPage()    { Serial.println("HTTP GET /setpoint");    server.sendHeader("Cache-Control","no-store"); server.send(200,"text/html",dashboardHtml(PAGE_SETPOINT)); }
static void handleSettingsPage()    { Serial.println("HTTP GET /settings");    server.sendHeader("Cache-Control","no-store"); server.send(200,"text/html",dashboardHtml(PAGE_SETTINGS)); }

static void handleApiBme280() {
  Serial.println("HTTP GET /api/bme280");
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",sensorJson());
}
static void handleApiBed() {
  Serial.println("HTTP GET /api/bed");
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",bedJson());
}
static void handleApiNfc() {
  Serial.println("HTTP GET /api/nfc");
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",nfcJson());
}
static void handleApiNfcRead() {
  Serial.println("HTTP /api/nfc/read");
  readNfcTag();
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",nfcJson());
}
static void handleApiNfcWriteTest() {
  Serial.println("HTTP /api/nfc/write");
  const uint8_t materialType = constrain(server.arg("type").toInt(), 0, 255);
  String brandName    = server.arg("brand"); if (brandName.length() == 0)    brandName = "ODIN";
  String materialName = server.arg("name");  if (materialName.length() == 0) materialName = "Test PLA";
  writeOpenPrintTag(materialType, brandName, materialName,
    constrain(server.arg("nmin").toInt(),0,350), constrain(server.arg("nmax").toInt(),0,350),
    constrain(server.arg("bmin").toInt(),0,140), constrain(server.arg("bmax").toInt(),0,140),
    constrain(server.arg("drytemp").toInt(),0,120), constrain(server.arg("drytime").toInt(),0,9999),
    server.arg("color"),
    constrain(server.arg("nomw").toInt(),0,9999), constrain(server.arg("actw").toInt(),0,9999),
    constrain(server.arg("emptyw").toInt(),0,9999), constrain(server.arg("density").toFloat(),0.0F,5.0F),
    constrain(server.arg("diameter").toFloat(),0.0F,5.0F),
    constrain((uint32_t)server.arg("nomlen").toInt(),(uint32_t)0,(uint32_t)999999),
    constrain((uint32_t)server.arg("actlen").toInt(),(uint32_t)0,(uint32_t)999999),
    server.arg("mdate").toInt()>0?(uint32_t)server.arg("mdate").toInt():0,
    server.arg("country"),
    constrain(server.arg("spoolw").toInt(),0,999), constrain(server.arg("spoolod").toInt(),0,999),
    constrain(server.arg("spoolid").toInt(),0,999), constrain(server.arg("spoolhole").toInt(),0,999));
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",nfcJson());
}
static void handleApiHeater() {
  Serial.println("HTTP GET /api/heater");
  updateHeaterControl(); server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",heaterJson());
}
static void handleApiHeaterOn() {
  Serial.println("HTTP GET /api/heater/on");
  heaterControl.automatic = false; heaterControl.outputPercent = 0.0F; heaterControl.status = "Manuale"; setHeater(true);
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",heaterJson());
}
static void handleApiHeaterOff() {
  Serial.println("HTTP GET /api/heater/off");
  stopHeaterAutomation("Manuale"); server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",heaterJson());
}
static void handleApiHeaterAutoOn() {
  Serial.println("HTTP GET /api/heater/auto/on");
  readBedTemperature(true);
  if (!bedData.valid)                             stopHeaterAutomation("STOP: NTC non valida");
  else if (bedData.temperature >= BED_SAFETY_LIMIT_C) stopHeaterAutomation("STOP: limite sicurezza");
  else { startHeaterAutomation(); updateHeaterControl(); }
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",heaterJson());
}
static void handleApiHeaterAutoOff() {
  Serial.println("HTTP GET /api/heater/auto/off");
  stopHeaterAutomation("Manuale"); server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",heaterJson());
}
static void handleApiSettingsMaterial() {
  Serial.println("HTTP GET /api/settings/material");
  if (server.hasArg("id")) {
    const int id = server.arg("id").toInt();
    if (id >= 0 && id < totalMaterialProfileCount()) {
      selectedMaterialProfile = (uint8_t)id;
      heaterControl.cycleDurationSeconds = (uint32_t)materialProfileMinutes(selectedMaterialProfile) * 60UL;
      if (!heaterControl.cycleRunning) heaterControl.cycleRemainingSeconds = heaterControl.cycleDurationSeconds;
    }
  }
  if (server.hasArg("setpoint")) {
    heaterControl.setpoint = constrain(server.arg("setpoint").toFloat(), 20.0F, BED_SAFETY_LIMIT_C);
    heaterControl.integral = 0.0F;
  }
  if (server.hasArg("minutes")) {
    heaterControl.cycleDurationSeconds = constrain((uint32_t)server.arg("minutes").toInt(), (uint32_t)1, (uint32_t)9999) * 60UL;
    if (!heaterControl.cycleRunning) heaterControl.cycleRemainingSeconds = heaterControl.cycleDurationSeconds;
  }
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",settingsJson());
}
static void handleApiSettingsProfileAdd() {
  Serial.println("HTTP GET /api/settings/profile/add");
  if (!server.hasArg("name") || !server.hasArg("setpoint") || !server.hasArg("minutes")) { server.send(400,"application/json","{\"error\":\"missing_args\"}"); return; }
  const float    requestedSetpoint = server.arg("setpoint").toFloat();
  const uint16_t requestedMinutes  = constrain((uint16_t)server.arg("minutes").toInt(),(uint16_t)1,(uint16_t)9999);
  if (isnan(requestedSetpoint) || requestedSetpoint < 20.0F || requestedSetpoint > BED_SAFETY_LIMIT_C || requestedMinutes < 1) { server.send(400,"application/json","{\"error\":\"invalid_args\"}"); return; }
  uint8_t newId = 0;
  if (!addCustomMaterialProfile(server.arg("name"), requestedSetpoint, requestedMinutes, newId)) { server.send(400,"application/json","{\"error\":\"profile_not_added\"}"); return; }
  saveCustomProfiles();
  selectedMaterialProfile = newId;
  heaterControl.setpoint  = materialProfileTemp(selectedMaterialProfile);
  heaterControl.integral  = 0.0F;
  heaterControl.cycleDurationSeconds = (uint32_t)materialProfileMinutes(selectedMaterialProfile) * 60UL;
  if (!heaterControl.cycleRunning) heaterControl.cycleRemainingSeconds = heaterControl.cycleDurationSeconds;
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",settingsJson());
}
static void handleApiWifiScan() {
  Serial.println("HTTP GET /api/wifi/scan");
  WiFi.mode(WIFI_AP_STA); WiFi.setSleep(false);
  const int networkCount = WiFi.scanNetworks(false, true);
  String json = "{\"networks\":[";
  for (int i = 0; i < networkCount; i++) {
    if (WiFi.SSID(i).length() == 0) continue;
    if (json.endsWith("}")) json += ",";
    json += "{\"ssid\":\""; json += escapeJson(WiFi.SSID(i));
    json += "\",\"rssi\":"; json += String(WiFi.RSSI(i));
    json += ",\"secure\":"; json += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true";
    json += "}";
  }
  json += "]}"; WiFi.scanDelete();
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",json);
}
static void handleApiSettingsGeneral() {
  Serial.println("HTTP GET /api/settings/general");
  if (server.hasArg("language")) { const String l=server.arg("language"); appSettings.language=(l=="en")?"en":UI_LOCALE_CODE; }
  if (server.hasArg("wifiSsid")) { appSettings.wifiSsid=server.arg("wifiSsid"); appSettings.wifiSsid.trim(); if(appSettings.wifiSsid.length()>32)appSettings.wifiSsid=appSettings.wifiSsid.substring(0,32); }
  if (server.hasArg("wifiPassword")) { appSettings.wifiPassword=server.arg("wifiPassword"); if(appSettings.wifiPassword.length()>64)appSettings.wifiPassword=appSettings.wifiPassword.substring(0,64); }
  if (server.hasArg("wifiClient")) appSettings.wifiClientEnabled = server.arg("wifiClient") == "1";
  if (server.hasArg("debug"))      appSettings.debugEnabled      = server.arg("debug") == "1";
  saveAppSettings();
  server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",settingsJson());
  scheduleWifiReconnect();
}
static void handleApiLog() {
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",logJson());
}

static void handleApiProfilesGet() {
  server.sendHeader("Cache-Control","no-store");
  String result = pi5ProxyRequest("GET", "/api/profiles");
  server.send(200,"application/json", result.isEmpty() ? profilesFallbackJson() : result);
}

static void handleApiProfilesPost() {
  server.sendHeader("Cache-Control","no-store");
  if (!appSettings.wifiClientEnabled) {
    server.send(503,"application/json","{\"error\":\"WiFi client non abilitato\"}"); return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503,"application/json","{\"error\":\"ESP32 non connesso al router\"}"); return;
  }
  String path = "/api/profiles/add?name=" + urlEncode(server.arg("name"));
  path += "&temperature=" + server.arg("temperature");
  path += "&minutes=" + server.arg("minutes");
  Serial.print("Profiles add GET: "); Serial.println(path);
  String result = pi5ProxyRequest("GET", path);
  Serial.print("Result: "); Serial.println(result.isEmpty() ? "EMPTY" : result.substring(0,80));
  server.send(result.isEmpty() ? 503 : 200, "application/json",
    result.isEmpty() ? "{\"error\":\"Pi5 non raggiungibile\"}" : result);
}

static void handleApiProfilesDelete() {
  server.sendHeader("Cache-Control","no-store");
  if (!appSettings.wifiClientEnabled) {
    server.send(503,"application/json","{\"error\":\"WiFi client non abilitato\"}"); return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503,"application/json","{\"error\":\"ESP32 non connesso al router\"}"); return;
  }
  String id = server.arg("id");
  if (id.isEmpty()) { server.send(400,"application/json","{\"error\":\"id mancante\"}"); return; }
  String result = pi5ProxyRequest("GET", "/api/profiles/remove?id=" + id);
  server.send(result.isEmpty() ? 503 : 200, "application/json",
    result.isEmpty() ? "{\"error\":\"Pi5 non raggiungibile\"}" : result);
}

static void handleApiRestart() {
  Serial.println("HTTP GET /api/restart — riavvio ESP32");
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json","{\"restarting\":true}");
  delay(300);
  ESP.restart();
}

static void handleApiPi5Test() {
  Serial.println("Pi5 test GET /health");
  String result = pi5ProxyRequest("GET", "/health");
  Serial.print("Pi5 test result: "); Serial.println(result.isEmpty() ? "EMPTY" : result);
  server.send(200, "application/json",
    result.isEmpty() ? "{\"pi5\":false,\"wifi\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}"
                     : "{\"pi5\":true,\"health\":" + result + "}");
}

static void handleNotFound() {
  Serial.print("HTTP 404 "); Serial.println(server.uri());
  server.send(404,"text/plain","Not found");
}

static void handleApiVersion() {
  String json = "{\"version\":\""; json += FIRMWARE_VERSION;
  json += "\",\"repo\":\"https://github.com/Ricky1966/Odin\"}";
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",json);
}

static void handleApiSlots() {
  Serial.println("HTTP GET /api/slots");
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",slotJson());
}

static void handleApiSlotDryingTime() {
  String slotStr = server.arg("slot");
  if (slotStr.isEmpty()) { server.send(400,"application/json","{\"error\":\"slot mancante\"}"); return; }
  uint8_t slot = slotStr.toInt();
  if (slot >= DryerSlots::NUM_SLOTS) { server.send(400,"application/json","{\"error\":\"slot non valido\"}"); return; }

  if (server.method() == HTTP_GET) {
    Serial.print("HTTP GET /api/slot/drying-time?slot="); Serial.println(slot);
    String json = "{\"slot\":"; json += String(slot);
    json += ",\"dryingTemp\":"; json += String(dryerSlots.dryingTemp[slot]);
    json += ",\"dryingMinutes\":"; json += String(dryerSlots.dryingMinutes[slot]);
    json += "}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",json);
  } else if (server.method() == HTTP_POST || server.method() == HTTP_GET) {
    Serial.print("HTTP /api/slot/drying-time?slot="); Serial.println(slot);
    uint16_t temp = constrain(server.arg("temperature").toInt(), 20, BED_SAFETY_LIMIT_C);
    uint16_t minutes = constrain(server.arg("minutes").toInt(), 1, 9999);
    dryerSlots.dryingTemp[slot] = temp;
    dryerSlots.dryingMinutes[slot] = minutes;
    String json = "{\"slot\":"; json += String(slot);
    json += ",\"dryingTemp\":"; json += String(dryerSlots.dryingTemp[slot]);
    json += ",\"dryingMinutes\":"; json += String(dryerSlots.dryingMinutes[slot]);
    json += "}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",json);
  }
}

static void handleApiSlotClearAll() {
  Serial.println("HTTP POST /api/slots/clear-all");
  for (uint8_t i = 0; i < DryerSlots::NUM_SLOTS; i++) {
    dryerSlots.slotOccupied[i] = false;
    dryerSlots.cycleRunning[i] = false;
    dryerSlots.cycleEndTime[i] = 0;
    dryerSlots.dryingTemp[i] = 45;
    dryerSlots.dryingMinutes[i] = 360;
  }
  dryerSlots.activeSlotCount = 0;
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",slotJson());
}

// ── Server setup ──────────────────────────────────────────────────────────────

void setupWebServer() {
  server.on("/",            handleRoot);
  server.on("/temperature", handleTemperaturePage);
  server.on("/tags",        handleTagsPage);
  server.on("/slots",       handleSlotsPage);
  server.on("/setpoint",    handleSetpointPage);
  server.on("/settings",    handleSettingsPage);
  server.on("/api/bme280",                  handleApiBme280);
  server.on("/api/bed",           HTTP_GET, handleApiBed);
  server.on("/api/settings/material",  HTTP_GET, handleApiSettingsMaterial);
  server.on("/api/settings/profile/add", HTTP_GET, handleApiSettingsProfileAdd);
  server.on("/api/settings/general", HTTP_GET, handleApiSettingsGeneral);
  server.on("/api/wifi/scan",       HTTP_GET, handleApiWifiScan);
  server.on("/api/nfc",             handleApiNfc);
  server.on("/api/nfc/read",   HTTP_GET, handleApiNfcRead);
  server.on("/api/nfc/write",  HTTP_GET, handleApiNfcWriteTest);
  server.on("/api/heater",          HTTP_GET, handleApiHeater);
  server.on("/api/heater/on",       HTTP_GET, handleApiHeaterOn);
  server.on("/api/heater/off",      HTTP_GET, handleApiHeaterOff);
  server.on("/api/heater/auto/on",  HTTP_GET, handleApiHeaterAutoOn);
  server.on("/api/heater/auto/off", HTTP_GET, handleApiHeaterAutoOff);
  server.on("/api/log",             HTTP_GET, handleApiLog);
  server.on("/api/profiles",        HTTP_GET,    handleApiProfilesGet);
  server.on("/api/profiles",        HTTP_POST,   handleApiProfilesPost);
  server.on("/api/profiles",        HTTP_DELETE, handleApiProfilesDelete);
  server.on("/api/slots",           HTTP_GET,    handleApiSlots);
  server.on("/api/slot/drying-time",HTTP_GET,    handleApiSlotDryingTime);
  server.on("/api/slot/drying-time",HTTP_POST,   handleApiSlotDryingTime);
  server.on("/api/slots/clear-all", HTTP_POST,   handleApiSlotClearAll);
  server.on("/api/pi5test",         HTTP_GET,    handleApiPi5Test);
  server.on("/api/restart",         HTTP_GET,    handleApiRestart);
  server.on("/api/version",         HTTP_GET,    handleApiVersion);
  server.onNotFound(handleNotFound);

  ElegantOTA.begin(&server);
  server.begin();
  Serial.println("Server web avviato. OTA su /update");
}

void webLoop() {
  server.handleClient();
  ElegantOTA.loop();
  checkPi5Reachable();
}
