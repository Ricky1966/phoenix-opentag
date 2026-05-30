#pragma once
#include "config.h"

constexpr MaterialDryProfile MATERIAL_PROFILES[] = {
  {"PLA",  45.0F, 360},
  {"PETG", 55.0F, 360},
  {"TPU",  60.0F, 240},
  {"ASA",  80.0F, 240},
  {"ABS",  80.0F, 240},
  {"PC",   85.0F, 300},
};
constexpr uint8_t MATERIAL_PROFILE_COUNT = sizeof(MATERIAL_PROFILES) / sizeof(MATERIAL_PROFILES[0]);
constexpr uint8_t MAX_CUSTOM_PROFILES    = 8;

extern AppSettings           appSettings;
extern CustomMaterialProfile customProfiles[MAX_CUSTOM_PROFILES];
extern uint8_t               customProfileCount;
extern uint8_t               selectedMaterialProfile;

uint8_t totalMaterialProfileCount();
bool    materialProfileExists(uint8_t id);
String  materialProfileName(uint8_t id);
float   materialProfileTemp(uint8_t id);
uint16_t materialProfileMinutes(uint8_t id);

bool addCustomMaterialProfile(String name, float temperature, uint16_t minutes, uint8_t &newId);
void saveCustomProfiles();
void loadCustomProfiles();
void saveAppSettings();
void loadAppSettings();
