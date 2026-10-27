#pragma once

#include <stdint.h>

namespace HomieInternals {

inline bool parseRangeIndex(const char* text, uint16_t& index) {
  if (!text || !*text) return false;

  uint16_t parsed = 0;
  for (const char* digit = text; *digit; ++digit) {
    if (*digit < '0' || *digit > '9') return false;
    const uint32_t next = static_cast<uint32_t>(parsed) * 10 + (*digit - '0');
    if (next > UINT16_MAX) return false;
    parsed = static_cast<uint16_t>(next);
  }

  index = parsed;
  return true;
}

}  // namespace HomieInternals
