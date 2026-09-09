// Compile the real async transport and callbacks against a host TCP boundary.
// Load native helpers first so MqttClient keeps its real Linux mutex backend.
#include <MqttClient.h>
#define ARDUINO_ARCH_ESP8266
#include <espMqttClientAsync.h>
#include <espMqttClientAsync.cpp>
#include <Transport/ClientAsync.cpp>
#undef ARDUINO_ARCH_ESP8266
#include "../../src/Homie/Utils/HomieMqttClient.hpp"

#include <cassert>
#include <condition_variable>
#include <future>
#include <type_traits>

#ifdef TEST_UNGUARDED
using Subject = espMqttClientAsync;
#else
using Subject = HomieInternals::HomieMqttClient;
#endif

struct Client : Subject {
  uint8_t empty = 0;
  Client() {
    _state = State::connected;
    setKeepAlive(0);
    // Upstream calls memcpy even for an empty read; give that unrelated native
    // nonnull precondition a valid source without disabling sanitizer checks.
    _clientAsync.bufData = &empty;
  }
  AsyncClient& tcp() { return _clientAsync.client; }
  void pending(std::vector<uint8_t>& bytes) {
    _clientAsync.bufData = bytes.data();
    _clientAsync.availableData = bytes.size();
  }
  void offline() { _state = State::disconnected; tcp().online = false; }
};

struct Gate {
  std::mutex mutex;
  std::condition_variable cv;
  bool open = false;
  void signal() { std::lock_guard<std::mutex> lock(mutex); open = true; cv.notify_all(); }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    assert(cv.wait_for(lock, std::chrono::seconds(3), [&] { return open; }));
  }
};

std::vector<uint8_t> firstChunk() {
  // One QoS1 PUBLISH, topic "a", id 1, with a 32-byte payload in two fragments.
  std::vector<uint8_t> bytes{0x32, 37, 0, 1, 'a', 0, 1};
  bytes.insert(bytes.end(), 16, 'A');
  return bytes;
}

void reentrantPump() {
  Client c;
  auto first = firstChunk();
  std::vector<uint8_t> second(16, 'B');
  unsigned callbacks = 0;
  c.onMessage([&](const espMqttClientTypes::MessageProperties&, const char*, const uint8_t* data,
                  size_t len, size_t index, size_t total) {
    ++callbacks;
    assert(total == 32 && len == 16);
    if (index == 0) {
      // Model bytes becoming available while a user callback tries to pump.
      c.pending(second);
      c.loop();
      assert(callbacks == 1);  // Red without the production adapter.
      assert(!c.connect());
    }
    for (size_t i = 0; i < len; ++i) assert(data[i] == (index == 0 ? 'A' : 'B'));
  });
  c.tcp().receive(first);
  c.loop();
  assert(callbacks == 2);
}

void concurrentReceive(const std::string& event) {
  Client c;
  HomieInternals::MqttPublishAck tracked;
  c.onPublish([&](uint16_t id) { tracked.acknowledge(id); });
  auto first = firstChunk();
  std::vector<uint8_t> second(16, 'B');
  Gate entered, resume, attempting;
  std::vector<size_t> indices;
  c.onMessage([&](const espMqttClientTypes::MessageProperties&, const char*, const uint8_t* data,
                  size_t len, size_t index, size_t) {
    if (index == 0) { entered.signal(); resume.wait(); }
    indices.push_back(index);
    for (size_t i = 0; i < len; ++i) assert(data[i] == (index == 0 ? 'A' : 'B'));
  });
  c.pending(first);
  auto foreground = std::async(std::launch::async, [&] { c.loop(); });
  entered.wait();
  auto receiver = std::async(std::launch::async, [&] {
    attempting.signal();
    if (event == "data") c.tcp().receive(second);
    else if (event == "poll") c.tcp().poll();
    else if (event == "disconnect") c.tcp().disconnectedEvent();
    else if (event == "connected") c.tcp().connectedEvent();
    else if (event == "connect") c.connect();
#if defined(ESP32) && !defined(TEST_UNGUARDED)
    else if (event == "tracked-publish") c.publishTracked("a", 1, true, "sleeping", tracked);
#endif
  });
  attempting.wait();
  assert(receiver.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
  // A busy foreground pump returns immediately, not after the callback finishes.
  auto skippedPump = std::async(std::launch::async, [&] { c.loop(); });
  assert(skippedPump.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  skippedPump.get();
  resume.signal();
  foreground.get(); receiver.get();
  assert(indices == (event == "data" ? std::vector<size_t>{0, 16} : std::vector<size_t>{0}));
  if (event == "data") {
    assert(c.publish("a", 0, false, "fast") != 0);
    const auto before = c.tcp().sent.size();
    c.loop();
    assert(c.tcp().sent.size() > before);  // No need to wait for TCP onPoll.
  }
  if (event == "tracked-publish") {
    assert(tracked.id() != 0);
    c.tcp().receive(second);  // Finish the incoming PUBLISH before sending PUBACK.
    c.loop();
    std::vector<uint8_t> ack{0x40, 2, static_cast<uint8_t>(tracked.id() >> 8), static_cast<uint8_t>(tracked.id())};
    c.tcp().receive(ack);
    assert(tracked.takeAcknowledged());
    assert(!tracked.takeAcknowledged());
  }
}

void connectionCallbacks() {
  Client c;
  unsigned connected = 0, disconnected = 0;
  c.onConnect([&](bool) { ++connected; c.loop(); assert(!c.connect()); });
  c.onDisconnect([&](espMqttClientTypes::DisconnectReason) { ++disconnected; c.loop(); assert(!c.connect()); });
  for (unsigned attempt = 0; attempt != 2; ++attempt) {
    c.offline();
    c.setServer("unused.test", 1883);
    assert(c.connect());
    c.tcp().connectedEvent();
    c.tcp().poll();
    std::vector<uint8_t> connack{0x20, 2, 0, 0};
    c.tcp().receive(connack);
    assert(c.connected() && connected == attempt + 1);
    c.tcp().disconnectedEvent();
    assert(c.disconnected() && disconnected == attempt + 1);
  }
}

int main() {
#ifdef ESP32
  reentrantPump();
  for (const auto* event : {"data", "poll", "disconnect", "connected", "connect"}) concurrentReceive(event);
#ifndef TEST_UNGUARDED
  concurrentReceive("tracked-publish");
#endif
  connectionCallbacks();
  std::cout << "MQTT adapter: reentrancy, concurrency, fast pump and reconnect passed\n";
#else
  static_assert(std::is_same<HomieInternals::HomieMqttClient, espMqttClientAsync>::value,
                "ESP8266 must retain the cooperative upstream client");
  std::cout << "MQTT adapter: ESP8266 compatibility passed\n";
#endif
}
