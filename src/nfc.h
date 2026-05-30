#pragma once
#include "config.h"

extern NfcData nfcData;

void   setupNfcReader();
void   readNfcTag();
void   clearNfcReadState();
bool   writeOpenPrintTag(uint8_t materialType, const String &brandName, const String &materialName,
                         uint16_t minPrintTemp, uint16_t maxPrintTemp, uint16_t minBedTemp,
                         uint16_t maxBedTemp, uint16_t dryingTemp, uint16_t dryingTime,
                         const String &primaryColor, uint16_t nominalWeight, uint16_t actualWeight,
                         uint16_t emptyWeight, float density, float filamentDiameter,
                         uint32_t nominalLength, uint32_t actualLength, uint32_t manufacturedDate,
                         const String &country, uint16_t spoolWidth, uint16_t spoolOuterDiameter,
                         uint16_t spoolInnerDiameter, uint16_t spoolHoleDiameter);
String nfcJson();
