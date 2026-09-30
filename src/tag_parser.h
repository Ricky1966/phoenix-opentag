#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>

struct SpoolData {
  bool present = false;
  bool valid = false;
  char uid[25] = "";
  char instanceUuid[37] = "";
  char material[64] = "";  // material_name, not the material_type enum
  char brand[32] = "";
  char country[3] = "";
  int materialClass = -1;
  int materialType = -1;
  float diameter = NAN;
  float density = NAN;
  int nozzleMin = -1, nozzleMax = -1;
  int bedMin = -1, bedMax = -1;
  float weight = NAN;      // nominal full net weight, never remaining weight
  float actualWeight = NAN;
  float emptyWeight = NAN;
  uint8_t color[4] = {0, 0, 0, 255};
  bool hasColor = false;
  unsigned long lastReadMs = 0;
};

enum class TagParseResult { Incomplete, Invalid, NotOpenPrintTag, Valid };
// Pure host-testable parser. Failure leaves 'out' unchanged. Full NDEF message
// required; validates only fields we consume, skips bounded unknown CBOR values.
TagParseResult parseType5Tag(const uint8_t *data, size_t size, SpoolData &out);
