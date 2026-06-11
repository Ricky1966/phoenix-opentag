#include "translations.h"
#include "i18n.h"
#include "config.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <map>

// Mappa per memorizzare le traduzioni scaricate
static std::map<TextKey, String> translationCache;
static bool translationsLoaded = false;
static String currentLanguage = "";

// Mapping tra TextKey enum e path JSON (section.key)
static const char *getJsonPath(TextKey key) {
  switch (key) {
    // nav
    case TK_NAV_HOME:     return "nav/home";
    case TK_NAV_TAG:      return "nav/nfc";
    case TK_NAV_SETPOINT: return "nav/setpoint";
    case TK_NAV_SETTINGS: return "nav/settings";
    case TK_CARD_TEMPERATURE: return "home/temperature";
    case TK_CARD_HUMIDITY: return "home/humidity";
    case TK_CARD_BED: return "home/bed_ntc";
    case TK_HEATER: return "esp32/status_idle";
    case TK_SETPOINT: return "setpoint/title";
    case TK_PROFILE_MATERIAL: return "setpoint/profile_section";
    case TK_APPLY_SETPOINT: return "setpoint/apply";
    case TK_SETTINGS_LANGUAGE: return "settings/language";
    case TK_SETTINGS_SAVING: return "settings/testing";
    case TK_SETTINGS_SAVED: return "settings/saved";
    case TK_SETTINGS_SAVE: return "settings/save";
    // ... aggiungi altri mapping secondo necessità
    default: return nullptr;
  }
}

// Parsing di un JSON path "section/key" da un oggetto JsonObject
static String getJsonValue(JsonObject obj, const char *path) {
  if (!path) return "";

  String pathStr(path);
  int slashPos = pathStr.indexOf('/');

  if (slashPos == -1) {
    // Top-level key
    if (obj[path].is<const char*>()) {
      return String(obj[path].as<const char*>());
    }
    return "";
  }

  String section = pathStr.substring(0, slashPos);
  String key = pathStr.substring(slashPos + 1);

  if (!obj[section].is<JsonObject>()) return "";

  JsonObject secObj = obj[section];
  if (secObj[key].is<const char*>()) {
    return String(secObj[key].as<const char*>());
  }

  return "";
}

bool downloadTranslationsFromPi5(const char *language) {
  if (!language) return false;

  Serial.print("[Translations] Scarico traduzioni per lingua: ");
  Serial.println(language);

  // Prova prima Tailscale, poi fallback LAN
  const char *hosts[] = {PI5_TAILSCALE_IP, PI5_LOCAL_IP};
  const char *hostNames[] = {"Tailscale", "LAN"};

  for (int i = 0; i < 2; i++) {
    String url = String("http://") + hosts[i] + ":" + PI5_PORT +
                 "/api/translations/" + language;

    Serial.print("[Translations] Tentativo ");
    Serial.print(i + 1);
    Serial.print(" (");
    Serial.print(hostNames[i]);
    Serial.print("): ");
    Serial.println(url);

    HTTPClient http;
    http.setTimeout(3000);

    if (!http.begin(url)) {
      Serial.println("[Translations] Errore begin HTTP");
      continue;
    }

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
      Serial.print("[Translations] HTTP error: ");
      Serial.println(httpCode);
      http.end();
      continue;
    }

    String payload = http.getString();
    http.end();

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("[Translations] JSON parse error: ");
      Serial.println(error.c_str());
      continue;
    }

    // Carica nel cache
    translationCache.clear();
    JsonObject root = doc.as<JsonObject>();

    // Per semplicità, load tutti i valori dal JSON
    for (int j = 0; j < 100; j++) {
      TextKey key = (TextKey)j;
      const char *path = getJsonPath(key);
      if (!path) continue;

      String value = getJsonValue(root, path);
      if (!value.isEmpty()) {
        translationCache[key] = value;
      }
    }

    translationsLoaded = true;
    currentLanguage = language;

    Serial.print("[Translations] ✓ Caricate ");
    Serial.print(translationCache.size());
    Serial.print(" traduzioni da ");
    Serial.println(hostNames[i]);

    return true; // Successo, esci dal loop
  }

  // Nessun host ha funzionato
  Serial.println("[Translations] ✗ Nessun host raggiungibile, fallback stringhe hardcoded");
  return false;
}

const char *getTranslationStr(TextKey key) {
  // Cerca nel cache prima
  if (translationsLoaded && translationCache.count(key) > 0) {
    static String result;
    result = translationCache[key];
    return result.c_str();
  }

  // Fallback: ritorna hardcoded
  return (const char *)tr(key);
}

// Wrapper per usare con F() se necessario
const __FlashStringHelper *getTranslation(TextKey key) {
  // Per semplificare, ritorna sempre tr() per ora
  // In futuro, potrebbe ritornare un wrapper F()
  return tr(key);
}
