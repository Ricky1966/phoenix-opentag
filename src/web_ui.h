#pragma once
#include "config.h"
#include <WebServer.h>

enum PageMode : uint8_t { PAGE_HOME, PAGE_TAGS, PAGE_SETPOINT, PAGE_SETTINGS };

extern WebServer server;

void   setupWebServer();
void   webLoop();
void   checkWifiReconnect();
void   connectWifiClient(uint16_t timeoutMs = 8000);
bool   wifiClientConnected();
String wifiClientIp();
String dashboardHtml(PageMode pageMode = PAGE_HOME);
String settingsJson();
