#pragma once
#include "tag_parser.h"
#include <string>

std::string spoolJson(const SpoolData &s, const char *reader, const char *firmware);

// Single latest observation, bounded retries, no offline backlog of old spools.
struct DeliveryState {
  std::string body, delivered;
  bool available=false, pending=false, attempted=false;
  uint32_t lastAttempt=0, retryMs=5000;
  void observe(const SpoolData &s, const std::string &json) {
    available=s.present && s.valid;
    if (!s.present) { body.clear(); delivered.clear(); pending=false; attempted=false; retryMs=5000; }
    if (!available) return;
    if (json!=body) { body=json; delivered.clear(); attempted=false; retryMs=5000; }
    pending=body!=delivered;
  }
  bool ready(uint32_t now) const {
    return available && pending && (!attempted || uint32_t(now-lastAttempt)>=retryMs);
  }
  void finish(uint32_t now, bool success) {
    lastAttempt=now;
    if (success) { delivered=body; pending=false; attempted=false; retryMs=5000; }
    else { if(attempted && retryMs<60000) retryMs=retryMs>30000?60000:retryMs*2; attempted=true; }
  }
};
