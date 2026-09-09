// Exercise the actual BootNormal helpers without Arduino or a broker.
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>
#include "../../src/Homie/Utils/MqttPublishAck.hpp"

namespace HomieInternals {
struct BootNormal {
  static constexpr size_t PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH = HOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH;
  MqttPublishAck _mqttOfflineAck;
  uint16_t _pendingMqttMessagesDropped = 0, _pendingMqttAcksDropped = 0;
  uint32_t _pendingMqttMessagesDroppedTotal = 0, _pendingMqttAcksDroppedTotal = 0;
  std::vector<uint16_t> events;
  bool _enqueuePendingMqttAck(uint16_t id) {
    if (events.size() == 3) return false;
    events.push_back(id);
    return true;
  }
  void _onMqttPublish(uint16_t);
  bool __fillPayloadBuffer(std::unique_ptr<char[]>&, size_t&, const uint8_t*, size_t, size_t, size_t);
#if HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED
  std::array<char, PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH + 1> _mqttPreallocatedPayloadBuffer{};
  bool __fillPreallocatedPayloadBuffer(const uint8_t*, size_t, size_t, size_t);
#endif
};
}
using HomieInternals::BootNormal;
void incrementDropCounters(uint16_t& current, uint32_t& total) { ++current; ++total; }
#include "../../src/Homie/Boot/BootNormalMqtt.ipp"

void sleepAckSurvivesFullQueue() {
  BootNormal b;
  b._mqttOfflineAck.track(100);
  for (uint16_t id = 1; id <= 100; ++id) b._onMqttPublish(id);
  assert(b.events.size() == 3 && b._pendingMqttAcksDroppedTotal == 97);
  assert(b._mqttOfflineAck.takeAcknowledged());
  assert(!b._mqttOfflineAck.takeAcknowledged());
  b._onMqttPublish(100);  // A duplicate must not trigger a second disconnect.
  assert(!b._mqttOfflineAck.takeAcknowledged());
  b._mqttOfflineAck.track(0);  // Disconnect or failed publish.
  b._onMqttPublish(100);
  b._onMqttPublish(0);
  assert(!b._mqttOfflineAck.takeAcknowledged());
  b._mqttOfflineAck.track(65535);
  b._onMqttPublish(100);
  assert(!b._mqttOfflineAck.takeAcknowledged());
  b._onMqttPublish(65535);
  assert(b._mqttOfflineAck.takeAcknowledged());
  b._mqttOfflineAck.track(1);  // Packet-ID wrap/new request.
  b._onMqttPublish(65535);
  assert(!b._mqttOfflineAck.takeAcknowledged());
  b._onMqttPublish(1);
  assert(b._mqttOfflineAck.takeAcknowledged());
}

void boundedPayload() {
  BootNormal b;
  const size_t limit = b.PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH;
  std::vector<uint8_t> bytes(limit, 'x');
  std::unique_ptr<char[]> buffer;
  size_t capacity = 0;
  // Oversize declarations must be rejected before allocation, including overflow.
  for (size_t total : {limit + 1, std::numeric_limits<size_t>::max()}) {
    assert(b.__fillPayloadBuffer(buffer, capacity, bytes.data(), 1, 0, total));
    assert(!buffer && capacity == 0);
#if HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED
    assert(b.__fillPreallocatedPayloadBuffer(bytes.data(), 1, 0, total));
#endif
  }
  for (size_t i = 0; i < limit; ++i) {
    assert(b.__fillPayloadBuffer(buffer, capacity, bytes.data() + i, 1, i, limit) == (i + 1 != limit));
#if HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED
    assert(b.__fillPreallocatedPayloadBuffer(bytes.data() + i, 1, i, limit) == (i + 1 != limit));
#endif
  }
  assert(capacity == limit + 1 && buffer[limit] == '\0');
  assert(memcmp(buffer.get(), bytes.data(), limit) == 0);
  char* allocated = buffer.get();
  assert(b.__fillPayloadBuffer(buffer, capacity, bytes.data(), 1, 0, limit + 1));
  assert(b.__fillPayloadBuffer(buffer, capacity, bytes.data(), 1, limit + 1, limit));
  assert(b.__fillPayloadBuffer(buffer, capacity, bytes.data(), 2, limit - 1, limit));
  assert(buffer.get() == allocated && capacity == limit + 1);
  assert(!b.__fillPayloadBuffer(buffer, capacity, bytes.data(), 1, 0, 1));
  assert(buffer[1] == '\0');  // A valid message still works after rejection.
}

int main() { sleepAckSurvivesFullQueue(); boundedPayload(); }
