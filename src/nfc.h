#pragma once
#include "tag_parser.h"
extern SpoolData spool;
void nfcSetup();
void nfcLoop();

// Serial writer protocol; the daemon owns this serial port during normal use.
bool nfcWriterCommand(const char *line);
bool nfcWriterHolding();
