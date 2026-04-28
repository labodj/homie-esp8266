#include "ExponentialBackoffTimer.hpp"

using namespace HomieInternals;

ExponentialBackoffTimer::ExponentialBackoffTimer(uint16_t initialInterval, uint8_t maxBackoff)
: _timer(Timer())
, _initialInterval(initialInterval)
, _maxBackoff(maxBackoff)
, _retryCount(0) {
  _timer.deactivate();
}

bool ExponentialBackoffTimer::check() {
  if (_timer.check()) {
    if (_retryCount != _maxBackoff) _retryCount++;

    const uint32_t fixedDelay = static_cast<uint32_t>(_retryCount)
                              * static_cast<uint32_t>(_retryCount)
                              * static_cast<uint32_t>(_initialInterval);
    uint32_t randomDifference = random(0, (fixedDelay / 10) + 1);
    uint32_t nextInterval = fixedDelay - randomDifference;

    _timer.setInterval(nextInterval, false);
    return true;
  } else {
    return false;
  }
}

void ExponentialBackoffTimer::activate() {
  if (_timer.isActive()) return;

  _timer.setInterval(_initialInterval, false);
  _timer.activate();
  _retryCount = 1;
}

void ExponentialBackoffTimer::deactivate() {
  _timer.deactivate();
}

bool ExponentialBackoffTimer::isActive() const {
  return _timer.isActive();
}
