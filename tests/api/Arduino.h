#pragma once

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include "IPAddress.h"

using String = std::string;
using byte = uint8_t;
#define F(value) value

class Print {
 public:
  std::ostringstream output;
  const String* lastString = nullptr;
  void print(const String& value) { lastString = &value; output << value; }
  template<class T> void print(const T& value) { output << value; }
  void println() { output << '\n'; }
};
