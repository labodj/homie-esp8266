#pragma once

#include <espMqttClientAsync.h>
#include "MqttPublishAck.hpp"
#ifdef ESP32
#include <mutex>
#endif

namespace HomieInternals {
#ifdef ESP32
// espMqttClient releases its mutex during application callbacks, while their
// payload still points into its shared receive buffer. Serialize whole AsyncTCP
// callbacks (including transport buffer assignment) with Homie's foreground pump.
// Forward to upstream; do not duplicate its parser or connection state machine.
class HomieMqttClient : public espMqttClientAsync {
 public:
  HomieMqttClient() {
    _clientAsync.client.onConnect([](void* arg, AsyncClient* client) {
      static_cast<HomieMqttClient*>(arg)->_dispatch(onConnectCb, client);
    }, this);
    _clientAsync.client.onDisconnect([](void* arg, AsyncClient* client) {
      static_cast<HomieMqttClient*>(arg)->_dispatch(onDisconnectCb, client);
    }, this);
    _clientAsync.client.onData([](void* arg, AsyncClient* client, void* data, size_t len) {
      static_cast<HomieMqttClient*>(arg)->_dispatch(onDataCb, client, data, len);
    }, this);
    _clientAsync.client.onPoll([](void* arg, AsyncClient* client) {
      static_cast<HomieMqttClient*>(arg)->_dispatch(onPollCb, client);
    }, this);
  }

  void loop() {
    std::unique_lock<std::recursive_mutex> lock(_mutex, std::try_to_lock);
    if (!lock.owns_lock() || _active) return;
    ActiveCall active(_active);
    espMqttClientAsync::loop();
  }

  bool connect() {
    // Upstream connect() also calls loop(). Unlike the optional pump, connection
    // setup must wait its turn rather than silently skip Homie's connect attempt.
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_active) return false;
    ActiveCall active(_active);
    return espMqttClientAsync::connect();
  }

  void publishTracked(const char* topic, uint8_t qos, bool retain, const char* payload, MqttPublishAck& ack) {
    // Register the packet ID before an AsyncTCP task can deliver its PUBACK.
    // Merely making the ID atomic would still leave that early-ACK window open.
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    ack.track(publish(topic, qos, retain, payload));
  }

 private:
  // Recursive locking lets loop()/connect() detect and reject calls from inside
  // an application callback instead of deadlocking or reentering the parser.
  std::recursive_mutex _mutex;
  bool _active = false;
  struct ActiveCall {
    bool& flag;
    bool previous;
    explicit ActiveCall(bool& value) : flag(value), previous(value) { flag = true; }
    ~ActiveCall() { flag = previous; }
  };

  template <typename Callback, typename... Args>
  void _dispatch(Callback callback, Args... args) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    ActiveCall active(_active);
    callback(static_cast<espMqttClientAsync*>(this), args...);
  }
};
#else
// ESP8266 uses cooperative callbacks, not a second AsyncTCP task.
using HomieMqttClient = espMqttClientAsync;
#endif
}  // namespace HomieInternals
