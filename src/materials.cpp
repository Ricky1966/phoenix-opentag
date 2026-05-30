#include "materials.h"
#include <Preferences.h>

AppSettings           appSettings;
CustomMaterialProfile customProfiles[MAX_CUSTOM_PROFILES];
uint8_t               customProfileCount     = 0;
uint8_t               selectedMaterialProfile = 0;

static Preferences preferences;

uint8_t totalMaterialProfileCount() {
  return MATERIAL_PROFILE_COUNT + customProfileCount;
}

bool materialProfileExists(uint8_t id) {
  return id < totalMaterialProfileCount();
}

String materialProfileName(uint8_t id) {
  if (id < MATERIAL_PROFILE_COUNT)        return MATERIAL_PROFILES[id].name;
  if (id < totalMaterialProfileCount())   return customProfiles[id - MATERIAL_PROFILE_COUNT].name;
  return "--";
}

float materialProfileTemp(uint8_t id) {
  if (id < MATERIAL_PROFILE_COUNT)       return MATERIAL_PROFILES[id].recommendedTemp;
  if (id < totalMaterialProfileCount())  return customProfiles[id - MATERIAL_PROFILE_COUNT].recommendedTemp;
  return DEFAULT_BED_SETPOINT_C;
}

uint16_t materialProfileMinutes(uint8_t id) {
  if (id < MATERIAL_PROFILE_COUNT)       return MATERIAL_PROFILES[id].recommendedMinutes;
  if (id < totalMaterialProfileCount())  return customProfiles[id - MATERIAL_PROFILE_COUNT].recommendedMinutes;
  return 60;
}

bool addCustomMaterialProfile(String name, float temperature, uint16_t minutes, uint8_t &newId) {
  name.trim();
  if (name.length() == 0 || customProfileCount >= MAX_CUSTOM_PROFILES) return false;
  if (name.length() > 24) name = name.substring(0, 24);

  for (uint8_t i = 0; i < name.length(); i++) {
    const char c = name[i];
    if (!isAlphaNumeric(c) && c != ' ' && c != '.' && c != '_' && c != '-')
      name.setCharAt(i, ' ');
  }
  name.trim();
  if (name.length() == 0) return false;

  for (uint8_t i = 0; i < totalMaterialProfileCount(); i++) {
    if (materialProfileName(i).equalsIgnoreCase(name)) return false;
  }

  customProfiles[customProfileCount].name               = name;
  customProfiles[customProfileCount].recommendedTemp    = constrain(temperature, 20.0F, BED_SAFETY_LIMIT_C);
  customProfiles[customProfileCount].recommendedMinutes = constrain(minutes, (uint16_t)1, (uint16_t)9999);
  newId = MATERIAL_PROFILE_COUNT + customProfileCount;
  customProfileCount++;
  return true;
}

void saveCustomProfiles() {
  preferences.begin("essicc", false);
  preferences.putUChar("profileCount", customProfileCount);
  for (uint8_t i = 0; i < MAX_CUSTOM_PROFILES; i++) {
    const String prefix = "p" + String(i);
    if (i < customProfileCount) {
      preferences.putString((prefix + "Name").c_str(), customProfiles[i].name);
      preferences.putFloat((prefix + "Temp").c_str(), customProfiles[i].recommendedTemp);
      preferences.putUShort((prefix + "Min").c_str(), customProfiles[i].recommendedMinutes);
    } else {
      preferences.remove((prefix + "Name").c_str());
      preferences.remove((prefix + "Temp").c_str());
      preferences.remove((prefix + "Min").c_str());
    }
  }
  preferences.end();
}

void loadCustomProfiles() {
  preferences.begin("essicc", true);
  customProfileCount = min<uint8_t>(preferences.getUChar("profileCount", 0), MAX_CUSTOM_PROFILES);
  for (uint8_t i = 0; i < customProfileCount; i++) {
    const String prefix = "p" + String(i);
    customProfiles[i].name               = preferences.getString((prefix + "Name").c_str(), "");
    customProfiles[i].recommendedTemp    = preferences.getFloat((prefix + "Temp").c_str(), DEFAULT_BED_SETPOINT_C);
    customProfiles[i].recommendedMinutes = preferences.getUShort((prefix + "Min").c_str(), 360);
    if (customProfiles[i].name.length() == 0) { customProfileCount = i; break; }
  }
  preferences.end();
}

void saveAppSettings() {
  preferences.begin("essicc", false);
  preferences.putString("wifiSsid",  appSettings.wifiSsid);
  preferences.putString("wifiPass",  appSettings.wifiPassword);
  preferences.putString("lang",      appSettings.language);
  preferences.putBool("wifiClient",  appSettings.wifiClientEnabled);
  preferences.putBool("debug",       appSettings.debugEnabled);
  preferences.end();
}

void loadAppSettings() {
  preferences.begin("essicc", true);
  appSettings.wifiSsid          = preferences.getString("wifiSsid", "");
  appSettings.wifiPassword      = preferences.getString("wifiPass", "");
  appSettings.language          = preferences.getString("lang", UI_LOCALE_CODE);
  appSettings.wifiClientEnabled = preferences.getBool("wifiClient", false);
  appSettings.debugEnabled      = preferences.getBool("debug", false);
  preferences.end();

  if (appSettings.language != UI_LOCALE_CODE && appSettings.language != "en")
    appSettings.language = UI_LOCALE_CODE;
}
