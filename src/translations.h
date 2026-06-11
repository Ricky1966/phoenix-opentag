#pragma once
#include "i18n.h"

// Scarica e memorizza le traduzioni da Pi5
// Fallback alle stringhe hardcoded se download fallisce
bool downloadTranslationsFromPi5(const char *language);

// Ottieni la traduzione per una chiave
// Cerca prima nel dizionario scaricato, fallback a hardcoded
const char *getTranslationStr(TextKey key);
