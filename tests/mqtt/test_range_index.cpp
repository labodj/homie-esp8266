#include <cassert>
#include <cstdint>
#include <string>

#include "../../src/Homie/Utils/RangeIndex.hpp"

int main() {
  uint16_t index = 42;
  assert(HomieInternals::parseRangeIndex("0", index) && index == 0);
  assert(HomieInternals::parseRangeIndex("00001", index) && index == 1);
  assert(HomieInternals::parseRangeIndex("65535", index) && index == UINT16_MAX);

  for (const char* invalid : {"", "-1", "+1", "1x", "65536", "65537"}) {
    assert(!HomieInternals::parseRangeIndex(invalid, index) && index == UINT16_MAX);
  }
  const std::string longIndex(256, '1');
  assert(!HomieInternals::parseRangeIndex(longIndex.c_str(), index) && index == UINT16_MAX);
  const std::string leadingZeroes(255, '0');
  assert(HomieInternals::parseRangeIndex((leadingZeroes + "1").c_str(), index) && index == 1);
}
