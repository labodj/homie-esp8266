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

Bounce ResetHandler::_resetBTNDebouncer;
uint32_t ResetHandler::_pressedAt = 0;
bool ResetHandler::_pressed = false;
bool ResetHandler::_sentReset = false;

void ResetHandler::Attach() {
  if (Interface::get().reset.enabled) {
    pinMode(Interface::get().reset.triggerPin, INPUT_PULLUP);
    _resetBTNDebouncer.attach(Interface::get().reset.triggerPin);
    _resetBTNDebouncer.interval(20);
  }
}

void ResetHandler::Loop() {
  _tick();
  _handleReset();
}

void ResetHandler::_tick() {
  if (!Interface::get().reset.resetFlag && Interface::get().reset.enabled) {
    _resetBTNDebouncer.update();
    const bool pressed = _resetBTNDebouncer.read() == Interface::get().reset.triggerState;
    if (pressed && !_pressed) {
      _pressedAt = millis();
    } else if (!pressed && _pressed) {
      const uint32_t held = millis() - _pressedAt;
      if (held >= Interface::get().reset.triggerTime) {
        Interface::get().reset.configurationOnly = held < 3UL * Interface::get().reset.triggerTime;
        Interface::get().disable = true;
        Interface::get().reset.resetFlag = true;
      }
    }
    _pressed = pressed;
  }
}

void ResetHandler::_handleReset() {
  if (Interface::get().reset.resetFlag && !_sentReset && Interface::get().reset.idle) {
    Interface::get().getLogger() << F("Flagged for reset") << endl;
    Interface::get().getLogger() << F("Device is idle") << endl;

    const bool configurationOnly = Interface::get().reset.configurationOnly;
    const bool prepared = configurationOnly
      ? Interface::get().getConfig().setHomieBootModeOnNextBoot(HomieBootMode::CONFIGURATION)
      : Interface::get().getConfig().erase();
    if (!prepared) {
      Interface::get().getLogger() << F("Cannot prepare config mode. Reset cancelled") << endl;
      Interface::get().reset.resetFlag = false;
      Interface::get().disable = false;
      return;
    }
    Interface::get().getLogger() << (configurationOnly ? F("Configuration preserved") : F("Configuration erased")) << endl;

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
