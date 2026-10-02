#pragma once
void transportSetup();
void transportLoop();
// Blank (all-zero) NFC-V tag seen on the reader. uid with or without colons.
void transportBlank(const char *uid);
