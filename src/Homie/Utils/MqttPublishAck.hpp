#pragma once

#include <atomic>
#include <cstdint>

namespace HomieInternals {
// A control ACK must survive notification-queue overflow. Keep its ID and state
// in one atomic word so a late callback cannot acknowledge a reset/new request.
class MqttPublishAck {
 public:
  void track(uint16_t id) { _state.store(id); }
  uint16_t id() const { return static_cast<uint16_t>(_state.load()); }
  void acknowledge(uint16_t id) {
    if (id == 0) return;
    uint32_t expected = id;
#ifdef ESP8266
    // ESP8266 is cooperative and these operations never yield. Its toolchain
    // does not provide 32-bit compare_exchange; load/store are sufficient here.
    if (_state.load() == expected) _state.store(id | ACKNOWLEDGED);
#else
    _state.compare_exchange_strong(expected, id | ACKNOWLEDGED);
#endif
  }
  bool takeAcknowledged() {
    uint32_t state = _state.load();
    if (!(state & ACKNOWLEDGED)) return false;
#ifdef ESP8266
    _state.store((state & 0xffffU) | CONSUMED);
    return true;
#else
    return _state.compare_exchange_strong(state, (state & 0xffffU) | CONSUMED);
#endif
  }

 private:
  static constexpr uint32_t ACKNOWLEDGED = 1UL << 16;
  static constexpr uint32_t CONSUMED = 1UL << 17;
  std::atomic<uint32_t> _state{0};
};
}  // namespace HomieInternals
