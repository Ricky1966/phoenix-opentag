#pragma once
#include <Arduino.h>
#include "nfc.h"

// Invio delle letture al demone su Phoenix (host/phoenix_opentag_daemon.py).
//
// Modalita' (scelta a runtime da seriale, salvata in flash):
//   usb   riga "PHOENIX-TAG {json}" sulla seriale USB, letta dal demone
//   wifi  HTTP POST {json} a <host>/tag
//   auto  wifi se connesso e il POST riesce, altrimenti usb
//
// Comandi seriali (115200): help, show, mode <usb|wifi|auto>, host <url>,
// reader <id>, test, reboot.

void transportSetup();
void transportLoop();                    // legge i comandi dalla seriale
bool transportWantsWifi();
void transportSendTag(const SpoolData &s);
