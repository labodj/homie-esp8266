#include "ResetHandler.hpp"

#if HOMIE_CONFIG
using namespace HomieInternals;

namespace {
HomieEvent makeEvent(HomieEventType type) {
  HomieEvent event{};
  event.type = type;
  return event;
}

void dispatchEvent(const HomieEvent& event) {
  Interface::get().eventHandler(event);
}
}  // namespace

Ticker ResetHandler::_resetBTNTicker;
Bounce ResetHandler::_resetBTNDebouncer;
bool ResetHandler::_sentReset = false;

void ResetHandler::Attach() {
  if (Interface::get().reset.enabled) {
    pinMode(Interface::get().reset.triggerPin, INPUT_PULLUP);
    _resetBTNDebouncer.attach(Interface::get().reset.triggerPin);
    _resetBTNDebouncer.interval(Interface::get().reset.triggerTime);

    _resetBTNTicker.attach_ms(10, _tick);
  }
}

void ResetHandler::Loop() {
  _handleReset();
}

void ResetHandler::_tick() {
  if (!Interface::get().reset.resetFlag && Interface::get().reset.enabled) {
    _resetBTNDebouncer.update();
    if (_resetBTNDebouncer.read() == Interface::get().reset.triggerState) {
      Interface::get().disable = true;
      Interface::get().reset.resetFlag = true;
    }
  }
}

void ResetHandler::_handleReset() {
  if (Interface::get().reset.resetFlag && !_sentReset && Interface::get().reset.idle) {
    Interface::get().getLogger() << F("Flagged for reset") << endl;
    Interface::get().getLogger() << F("Device is idle") << endl;

    Interface::get().getConfig().erase();
    Interface::get().getLogger() << F("Configuration erased") << endl;

    // Set boot mode
    Interface::get().getConfig().setHomieBootModeOnNextBoot(HomieBootMode::CONFIGURATION);

    Interface::get().getLogger() << F("Triggering ABOUT_TO_RESET event...") << endl;
    const HomieEvent event = makeEvent(HomieEventType::ABOUT_TO_RESET);
    dispatchEvent(event);

    Interface::get().getLogger() << F("↻ Rebooting into config mode...") << endl;
    Serial.flush();
    ESP.restart();
    _sentReset = true;
  }
}
#endif
