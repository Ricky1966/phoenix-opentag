#pragma once
#include "config.h"

extern SensorData sensorData;
extern BedData    bedData;

void   readSensor(bool force = false);
void   readBedTemperature(bool force = false);
String sensorJson();
String bedJson();
void   sensorSetup();
