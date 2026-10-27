#include <cassert>
#include "Arduino.h"
#include "../../src/StreamingOperator.hpp"

struct Counted {
  static int copies;
  Counted() = default;
  Counted(const Counted&) { ++copies; }
};
int Counted::copies = 0;
std::ostream& operator<<(std::ostream& output, const Counted&) { return output << "counted"; }

// No out-of-class definition: logging must not ODR-use this constant.
struct Settings {
  static const int value = 42;
};

int main() {
  Print printer;
  Counted value;
  String text = " string ";
  printer << value << text << Settings::value << ' ' << 3.5 << endl;
  assert(Counted::copies == 1);
  assert(printer.lastString == &text);
  assert(printer.output.str() == "counted string 42 3.5\n");
}
