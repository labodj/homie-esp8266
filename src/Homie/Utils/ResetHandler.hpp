#pragma once

#include "Arduino.h"

#include <Bounce2.h>
#include "../../StreamingOperator.hpp"
#include "../Datatypes/Interface.hpp"

#if HOMIE_CONFIG
namespace HomieInternals {
class ResetHandler {
 public:
  static void Attach();
  static void Loop();

 private:
  // Disallow creating an instance of this object
  ResetHandler() {}
  static Bounce _resetBTNDebouncer;
  static uint32_t _pressedAt;
  static bool _pressed;
  static void _tick();
  static bool _sentReset;
  static void _handleReset();
};
}  // namespace HomieInternals
#endif
