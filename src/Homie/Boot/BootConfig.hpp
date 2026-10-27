#pragma once

#include "Arduino.h"

#include "../Constants.hpp"
#if HOMIE_CONFIG

#include <functional>
#include <atomic>
#include <memory>
#ifdef ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESPAsyncTCP.h>
#endif // ESP32
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include "Boot.hpp"
#include "../Limits.hpp"
#include "../Datatypes/Interface.hpp"
#include "../Timer.hpp"
#include "../Utils/DeviceId.hpp"
#include "../Utils/Filesystem.hpp"
#include "../Utils/Validation.hpp"
#include "../Utils/Helpers.hpp"
#include "../Logger.hpp"
#include "../Strings.hpp"
#include "../../HomieSetting.hpp"
#include "../../StreamingOperator.hpp"

namespace HomieInternals {
class BootConfig : public Boot {
 public:
  BootConfig();
  ~BootConfig();
  void setup();
  void loop();

 private:
  AsyncWebServer _http;
  HTTPClient _httpClient;
  WiFiClient _wifiClient;
  DNSServer _dns;
  uint8_t _ssidCount;
  bool _wifiScanAvailable;
  Timer _wifiScanTimer;
  bool _lastWifiScanEnded;
  String _jsonWifiNetworks;
  std::atomic<bool> _flaggedForReboot;
  uint32_t _flaggedForRebootAt;
  bool _proxyEnabled;
  char _apIpStr[MAX_IP_STRING_LENGTH];
  enum class ConfigTestPhase : uint8_t { IDLE, PREPARING, QUEUED, WIFI, MQTT, CLOSING, READY, FAILED, SAVED };
  std::atomic<ConfigTestPhase> _configTestPhase{ConfigTestPhase::IDLE};
  std::unique_ptr<DynamicJsonDocument> _testConfig;
  std::atomic<bool> _testMqttConnected{false};
  std::atomic<uint8_t> _testMqttError{0};
  std::atomic<uint8_t> _testFailure{0};
  uint32_t _testStartedAt = 0;
  String _testClientId;

  void _onCaptivePortal(AsyncWebServerRequest *request);
  void _onDeviceInfoRequest(AsyncWebServerRequest *request);
  void _onNetworksRequest(AsyncWebServerRequest *request);
  void _onConfigRequest(AsyncWebServerRequest *request);
  void _onStoredConfigRequest(AsyncWebServerRequest *request);
  bool _parseConfig(AsyncWebServerRequest *request, JsonDocument& document);
  bool _claimConfigRequest(ConfigTestPhase expected);
  void _onConfigTestRequest(AsyncWebServerRequest *request);
  void _onConfigStatusRequest(AsyncWebServerRequest *request);
  void _loopConfigTest();
  void _loopWifiScan();
  void _generateNetworksJson();
  void _onWifiConnectRequest(AsyncWebServerRequest *request);
  void _onProxyControlRequest(AsyncWebServerRequest *request);
  void _proxyHttpRequest(AsyncWebServerRequest *request);
  void _onWifiStatusRequest(AsyncWebServerRequest *request);

  // Helpers
  static void __setCORS();
  static const int MAX_POST_SIZE = 1500;
  static void __parsePost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
  static void __SendJSONError(AsyncWebServerRequest *request, String msg, int16_t code = 400);
};
}  // namespace HomieInternals

#endif
