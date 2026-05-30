#pragma once
#include "config.h"

extern HeaterControlData heaterControl;
extern bool              heaterEnabled;

void     setHeater(bool enabled);
void     stopHeaterAutomation(const String &status);
void     startHeaterAutomation();
void     updateHeaterControl();
uint32_t heaterCycleRemainingSeconds();
uint32_t heaterCycleElapsedSeconds();
String   heaterJson();
