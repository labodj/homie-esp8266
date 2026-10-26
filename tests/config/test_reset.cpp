#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#define HOMIE_CONFIG 1
#define F(value) value
#define INPUT_PULLUP 2
#include "../../src/HomieBootMode.hpp"

uint32_t now = 0;
uint32_t millis() { return now; }
void pinMode(int, int) {}
struct Bounce {
  int state = 1;
  void attach(int) {}
  void interval(int) {}
  void update() {}
  int read() { return state; }
};
struct { void flush() {} } Serial;
struct { int restarts = 0; void restart() { ++restarts; } } ESP;
enum class HomieEventType { ABOUT_TO_RESET };
struct HomieEvent { HomieEventType type; };
using std::endl;
namespace HomieInternals {
struct Config {
  bool writable = true;
  int erased = 0, modes = 0;
  std::string id = "j4";
  bool erase() { if (!writable) return false; ++erased; id.clear(); return true; }
  bool setHomieBootModeOnNextBoot(HomieBootMode) { if (!writable) return false; ++modes; return true; }
};
struct Data {
  struct Reset {
    bool enabled = true, resetFlag = false, idle = true, configurationOnly = false;
    int triggerPin = 0, triggerState = 0;
    uint16_t triggerTime = 5000;
  } reset;
  bool disable = false;
  Config config;
  std::ostringstream logger;
  int events = 0;
  Config& getConfig() { return config; }
  std::ostringstream& getLogger() { return logger; }
  void eventHandler(const HomieEvent&) { ++events; }
};
struct Interface { static Data& get() { static Data data; return data; } };
struct ResetHandler {
  static Bounce _resetBTNDebouncer;
  static uint32_t _pressedAt;
  static bool _pressed, _sentReset;
  static void Attach();
  static void Loop();
  static void _tick();
  static void _handleReset();
};
}
#include "../../src/Homie/Utils/ResetHandler.ipp"

void fresh() {
  auto& d = Interface::get();
  d.reset = Data::Reset{}; d.config = Config{}; d.disable = false; d.events = 0;
  ResetHandler::_pressed = false; ResetHandler::_sentReset = false;
  ResetHandler::_resetBTNDebouncer.state = 1; ESP.restarts = 0;
}
void press(uint32_t duration) {
  ResetHandler::_resetBTNDebouncer.state = 0; ResetHandler::Loop();
  now += duration;
  ResetHandler::Loop(); assert(ESP.restarts == 0);
  ResetHandler::_resetBTNDebouncer.state = 1; ResetHandler::Loop();
}
int main() {
  auto& d = Interface::get();
  fresh(); press(4999); assert(ESP.restarts == 0 && d.config.id == "j4");
  fresh(); press(5000); assert(ESP.restarts == 1 && d.config.id == "j4" && d.config.erased == 0 && d.config.modes == 1);
  fresh(); press(14999); assert(ESP.restarts == 1 && d.config.id == "j4");
  fresh(); press(15000); assert(ESP.restarts == 1 && d.config.erased == 1 && d.config.id.empty());
  fresh(); now = UINT32_MAX - 1000; press(5000); assert(ESP.restarts == 1 && d.config.id == "j4");
  fresh(); d.reset.idle = false; press(5000); assert(ESP.restarts == 0);
  d.reset.idle = true; ResetHandler::Loop(); assert(ESP.restarts == 1 && d.config.id == "j4");
  for (bool configurationOnly : {false, true}) {
    fresh(); d.config.writable = false; d.reset.configurationOnly = configurationOnly;
    d.reset.resetFlag = true; d.disable = true; ResetHandler::Loop();
    assert(ESP.restarts == 0 && d.events == 0 && !d.disable && !d.reset.resetFlag && d.config.id == "j4");
    for (int i = 0; i < 10; ++i) ResetHandler::Loop();
    assert(ESP.restarts == 0);
    d.config.writable = true; d.reset.resetFlag = true; ResetHandler::Loop();
    assert(ESP.restarts == 1);
  }
  fresh(); d.reset.resetFlag = true; ResetHandler::Loop();
  assert(ESP.restarts == 1 && d.config.erased == 1 && d.events == 1);
  std::cout << "Reset: preserved ID, explicit factory reset, release thresholds, timer wrap, idle and failures passed\n";
}
