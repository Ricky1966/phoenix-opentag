#pragma once
#include "config.h"

extern NfcData nfcData;

void setupNfcReader();
void readNfcTag();
void clearNfcReadState();
void printNfcData();
