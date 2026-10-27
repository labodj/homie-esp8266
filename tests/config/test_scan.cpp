#include <cassert>
#include <cstdint>
#include <sstream>
#include <iostream>
#define F(value) value
using std::endl;
constexpr int WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2;
struct {
  int16_t result = 0;
  unsigned starts = 0;
  int16_t scanComplete() { return result; }
  void scanNetworks(bool asynchronous) { assert(asynchronous); ++starts; }
} WiFi;
struct Interface {
  static Interface& get() { static Interface value; return value; }
  std::ostringstream logger;
  std::ostringstream& getLogger() { return logger; }
};
struct BootConfig {
  uint8_t _ssidCount = 0;
  bool _wifiScanAvailable = false, _lastWifiScanEnded = false;
  struct {
    bool due = false;
    unsigned resets = 0, ticks = 0;
    bool check() { return due; }
    void reset() { due = false; ++resets; }
    void tick() { due = false; ++ticks; }
  } _wifiScanTimer;
  unsigned generated = 0;
  void _generateNetworksJson() { ++generated; }
  void _loopWifiScan();
};
#include "../../src/Homie/Boot/BootConfigScan.ipp"

int main() {
  for (int16_t count : {0, 1, 127, 128, 253, 254, 255, 256, 32767}) {
    BootConfig config;
    WiFi.result = count;
    config._loopWifiScan();
    assert(config._ssidCount == (count > UINT8_MAX ? UINT8_MAX : count));
    assert(config._lastWifiScanEnded && config._wifiScanAvailable && config.generated == 1);
  }
  {
    BootConfig config;
    WiFi.result = WIFI_SCAN_RUNNING;
    WiFi.starts = 0; config._wifiScanTimer.due = true;
    config._loopWifiScan();
    assert(!config._lastWifiScanEnded && !config.generated && !WiFi.starts);
  }
  for (int16_t error : {-2, -3}) {
    BootConfig config;
    WiFi.result = error;
    config._loopWifiScan();
    assert(config._lastWifiScanEnded && config._ssidCount == 0 && config._wifiScanTimer.resets == 1);
    assert(!config._wifiScanAvailable && !config.generated);
    config._wifiScanTimer.due = true;
    WiFi.starts = 0;
    config._loopWifiScan();
    assert(!config._lastWifiScanEnded && WiFi.starts == 1 && config._wifiScanTimer.ticks == 1);
  }
}
