#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
class __FlashStringHelper;
#define F(s) reinterpret_cast<const __FlashStringHelper *>(s)
inline unsigned long millis() { static unsigned long ms=0; return ++ms; }
inline void delay(unsigned long) {}
