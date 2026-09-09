#pragma once

// Host-only TCP boundary. The MQTT transport/callback/parser code stays upstream.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class AsyncClient {
 public:
  using Event = std::function<void(void*, AsyncClient*)>;
  using Data = std::function<void(void*, AsyncClient*, void*, size_t)>;
  Event connectedCallback, disconnectedCallback, pollCallback;
  Data dataCallback;
  void *connectedArg = nullptr, *disconnectedArg = nullptr, *pollArg = nullptr, *dataArg = nullptr;
  bool online = true;
  std::vector<uint8_t> sent;
  void onConnect(Event fn, void* arg) { connectedCallback = fn; connectedArg = arg; }
  void onDisconnect(Event fn, void* arg) { disconnectedCallback = fn; disconnectedArg = arg; }
  void onPoll(Event fn, void* arg) { pollCallback = fn; pollArg = arg; }
  void onData(Data fn, void* arg) { dataCallback = fn; dataArg = arg; }
  bool connect(const char*, uint16_t) { online = true; return true; }
  bool connect(IPAddress, uint16_t) { online = true; return true; }
  bool connected() { return online; }
  bool disconnected() { return !online; }
  void setNoDelay(bool) {}
  size_t write(const char* bytes, size_t len) {
    sent.insert(sent.end(), bytes, bytes + len);
    return len;
  }
  void receive(std::vector<uint8_t>& bytes) { dataCallback(dataArg, this, bytes.data(), bytes.size()); }
  void poll() { pollCallback(pollArg, this); }
  void connectedEvent() { connectedCallback(connectedArg, this); }
  void disconnectedEvent() { online = false; disconnectedCallback(disconnectedArg, this); }
};
