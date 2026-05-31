#pragma once
#include "config.h"

struct LogEntry {
  unsigned long timestampMs;
  float temperature;
  float humidity;
  float bedTemp;
  float outputPercent;
};

void logCheck();
String logJson();
