// Run the production HTTP handlers and foreground loop with deterministic transports.
// GCC 13 warns on ArduinoJson's zero-copy storage before startString initializes it.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#include <ArduinoJson.h>
#pragma GCC diagnostic pop
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#define F(s) s
#define FPSTR(s) s
using String = std::string;
using std::endl;
uint32_t clockMs = 1;
uint32_t millis() { return clockMs; }
#include "../../src/Homie/Limits.hpp"
using namespace HomieInternals;
constexpr int DEFAULT_MQTT_PORT = 1883;
const char* PROGMEM_CONFIG_APPLICATION_JSON = "application/json";
const char* PROGMEM_CONFIG_JSON_SUCCESS = "{\"success\":true}";
const int WL_CONNECTED = 3;
struct IPAddress { String address; void fromString(const char* p) { address = p; } };
namespace Helpers { void stringToBytes(const char*, char, uint8_t*, size_t, int) {} }
struct { bool connected = false; String ssid; int attempts = 0;
  String appliedIp, appliedDns1, appliedDns2;
  bool usedBssid = false; uint16_t appliedChannel = 0;
  void disconnect(bool) { connected = false; }
  void begin(const char* name, const char*) { ssid = name; usedBssid = false; ++attempts; }
  void begin(const char* name, const char* password, uint16_t channel, uint8_t*) {
    begin(name, password); usedBssid = true; appliedChannel = channel;
  }
  bool config(IPAddress ip, IPAddress, IPAddress, IPAddress dns1, IPAddress dns2) {
    appliedIp = ip.address; appliedDns1 = dns1.address; appliedDns2 = dns2.address; return true;
  }
  void setAutoReconnect(bool) {}
  int status() { return connected ? WL_CONNECTED : 0; }
  String SSID() { return ssid; }
} WiFi;
struct Mqtt {
  bool idle = true, online = false, canStart = true;
  int attempts = 0;
  const char* host = nullptr;
  const char* username = nullptr;
  const char* clientId = nullptr;
  void loop() {}
  bool disconnected() { return idle; }
  bool connected() { return online; }
  bool connect() { ++attempts; idle = !canStart; return canStart; }
  void disconnect(bool) { online = false; idle = true; }
  void setServer(const char* h, int) { host = h; }
  void setClientId(const char* id) { clientId = id; }
  void setCleanSession(bool) {}
  void setCredentials(const char* user, const char*) { username = user; }
};
struct Config { bool writable = true; int writes = 0;
  bool storageReady = true;
  bool _valid = false;
  bool _recoveryFailed = false;
  bool prepare(JsonDocument&, const JsonObject);
  void _patchJsonObject(JsonObject, JsonObjectConst);
  bool isValid() const { return _valid; }
  bool isStorageReady() const { return storageReady && !_recoveryFailed; }
  char* getSafeConfigFile() const;
  bool write(JsonObject) { ++writes; return writable; }
};
const char* CONFIG_FILE_PATH = "config";
struct File {
  String contents; size_t pos = 0, maxRead = SIZE_MAX; bool valid = true;
  explicit operator bool() const { return valid; }
  size_t size() { return contents.size(); }
  int read() { return pos < contents.size() && pos < maxRead ? static_cast<uint8_t>(contents[pos++]) : -1; }
  size_t readBytes(char* data, size_t size) {
    size_t n = 0; while (n < size && pos < contents.size() && pos < maxRead) data[n++] = contents[pos++]; return n;
  }
  void close() {}
};
struct { String stored; bool readable = true; size_t maxRead = SIZE_MAX;
  File open(const char*, const char*) { File f; f.contents = stored; f.valid = readable; f.maxRead = maxRead; return f; }
} HOMIE_FS;
#include "../../src/Homie/ConfigJson.ipp"
struct Interface {
  Mqtt mqtt; Config config; std::ostringstream logger; bool disable = false;
  static Interface& get() { static Interface i; return i; }
  Mqtt& getMqttClient() { return mqtt; }
  Config& getConfig() { return config; }
  std::ostringstream& getLogger() { return logger; }
};
struct ConfigValidationResult { bool valid = true; String reason; };
namespace Validation { ConfigValidationResult validateConfig(JsonObject) { return {}; } }
struct Response { String body; void addHeader(const char*, const char*) {} };
struct AsyncWebServerRequest {
  String body, response; void* _tempObject; int code = 0;
  explicit AsyncWebServerRequest(String text = "") : body(text), _tempObject(body.empty() ? nullptr : const_cast<char*>(body.c_str())) {}
  void send(int c, const char*, const String& value) { code = c; response = value; }
  Response* beginResponse(int c, const char*, const String& value) { code = c; auto* r = new Response; r->body = value; return r; }
  void send(Response* r) { response = r->body; delete r; }
};
class BootConfig {
 public:
  enum class ConfigTestPhase : uint8_t { IDLE, PREPARING, QUEUED, WIFI, MQTT, CLOSING, READY, FAILED, SAVED };
  std::atomic<bool> _flaggedForReboot{false}; uint32_t _flaggedForRebootAt = 0;
  std::atomic<ConfigTestPhase> _configTestPhase{ConfigTestPhase::IDLE};
  std::unique_ptr<DynamicJsonDocument> _testConfig;
  std::atomic<bool> _testMqttConnected{false};
  std::atomic<uint8_t> _testMqttError{0}, _testFailure{0};
  uint32_t _testStartedAt = 0;
  String _testClientId = "Homie-setup-hardware-mac";
  void _loopConfigTest();
  void _onConfigTestRequest(AsyncWebServerRequest*);
  void _onConfigStatusRequest(AsyncWebServerRequest*);
  void _onConfigRequest(AsyncWebServerRequest*);
  bool _parseConfig(AsyncWebServerRequest*, JsonDocument&);
  bool _claimConfigRequest(ConfigTestPhase);
  void _onStoredConfigRequest(AsyncWebServerRequest*);
  static const int MAX_POST_SIZE = 1500;
  static void __parsePost(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t);
  static void __SendJSONError(AsyncWebServerRequest*, String, int16_t code = 400);
};
#include "../../src/Homie/Boot/BootConfigTest.ipp"
namespace HomieInternals { using ::BootConfig; }
#include "../../src/Homie/Boot/BootConfigBody.ipp"
const char* candidate = R"({"name":"Test","device_id":"j4","wifi":{"ssid":"Home","password":"secret"},"mqtt":{"host":"broker.invalid","auth":true,"username":"user","password":"secret"}})";
using Phase = BootConfig::ConfigTestPhase;
void reset() { Interface::get().mqtt = Mqtt{}; Interface::get().config = Config{}; WiFi.connected = false; clockMs = 1; }
void queue(BootConfig& b) {
  // The HTTP buffer is destroyed here: later connection steps must own its strings.
  AsyncWebServerRequest r(candidate); b._onConfigTestRequest(&r); assert(r.code == 202);
  assert(!Interface::get().config.writes);
}
void joinWifi(BootConfig& b) {
  b._loopConfigTest(); assert(b._configTestPhase == Phase::WIFI);
  WiFi.connected = true; b._loopConfigTest(); assert(b._configTestPhase == Phase::MQTT);
  assert(std::string(Interface::get().mqtt.host) == "broker.invalid");
  assert(std::string(Interface::get().mqtt.username) == "user");
  assert(std::string(Interface::get().mqtt.clientId) != "Homie-j4");
}
void ready(BootConfig& b) {
  joinWifi(b); b._testMqttConnected = true; Interface::get().mqtt.online = true;
  b._loopConfigTest(); assert(b._configTestPhase == Phase::CLOSING);
  b._loopConfigTest(); assert(b._configTestPhase == Phase::READY);
}
int main() {
  {
    // All optional groups together must still fit the next boot's bounded parser.
    reset(); DynamicJsonDocument full(4096); deserializeJson(full, candidate);
    full["device_stats_interval"] = 300;
    full["ota"]["enabled"] = true;
    full["wifi"]["ip"] = "192.0.2.10"; full["wifi"]["mask"] = "255.255.255.0";
    full["wifi"]["gw"] = "192.0.2.1"; full["wifi"]["dns1"] = "192.0.2.2"; full["wifi"]["dns2"] = "192.0.2.3";
    full["wifi"]["bssid"] = "aa:bb:cc:dd:ee:ff"; full["wifi"]["channel"] = 6;
    full["mqtt"]["port"] = 1883; full["mqtt"]["base_topic"] = "homie/";
    full["mqtt"]["ssl"] = false; full["mqtt"]["ssl_fingerprint"] = "";
    full["mqtt"]["effective_base_topic"] = "homie/5/";
    for (int i = 0; i < 10; ++i) full["settings"]["s" + std::to_string(i)] = i;
    String text; serializeJson(full, text); assert(text.size() < MAX_JSON_CONFIG_FILE_SIZE);
    char buffer[MAX_JSON_CONFIG_FILE_SIZE]; memcpy(buffer, text.c_str(), text.size() + 1);
    StaticJsonDocument<MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE> boot;
    assert(deserializeJson(boot, buffer) == DeserializationError::Ok);
    assert(boot["settings"].size() == 10 && boot["device_stats_interval"] == 300);

    BootConfig b; AsyncWebServerRequest r(text); b._onConfigTestRequest(&r); assert(r.code == 202);
    b._loopConfigTest(); assert(WiFi.appliedIp == "192.0.2.10");
    assert(WiFi.appliedDns1 == "192.0.2.2" && WiFi.appliedDns2 == "192.0.2.3");
    assert(WiFi.usedBssid && WiFi.appliedChannel == 6);

    reset(); Interface::get().config._valid = true; HOMIE_FS.stored = text; BootConfig dhcp;
    const char* automatic = R"({"device_stats_interval":null,"wifi":{"ip":null,"mask":null,"gw":null,"dns1":null,"dns2":null,"bssid":null,"channel":null}})";
    AsyncWebServerRequest clear(automatic); dhcp._onConfigTestRequest(&clear); assert(clear.code == 202);
    dhcp._loopConfigTest(); assert(WiFi.appliedIp.empty() && WiFi.appliedDns1.empty() && WiFi.appliedDns2.empty());
    assert(!WiFi.usedBssid && (*dhcp._testConfig)["device_id"] == "j4");
    assert(!dhcp._testConfig->containsKey("device_stats_interval"));
  }
  {
    reset(); BootConfig b; auto& config = Interface::get().config;
    config._recoveryFailed = true; // Mounted filesystem, valid backup, failed promotion.
    HOMIE_FS.stored = R"({"device_id":"j4"})";
    AsyncWebServerRequest read; b._onStoredConfigRequest(&read); assert(read.code == 503);
    AsyncWebServerRequest save(candidate); b._onConfigRequest(&save); assert(save.code == 503);
    b._onConfigTestRequest(&save); assert(save.code == 503);
    DynamicJsonDocument changes(1024), prepared(1024); deserializeJson(changes, candidate);
    assert(!config.prepare(prepared, changes.as<JsonObject>()));
    assert(config.writes == 0 && !b._flaggedForReboot && HOMIE_FS.stored.find("j4") != String::npos);
  }
  {
    reset(); BootConfig b; auto& config = Interface::get().config;
    config.storageReady = false;
    AsyncWebServerRequest r; b._onStoredConfigRequest(&r);
    assert(r.code == 503 && r.response.find("preserved") != String::npos);
    assert(r.response.find("\"configured\":false") == String::npos);
    assert(!b._flaggedForReboot && config.writes == 0);
    AsyncWebServerRequest rejected(candidate);
    b._onConfigRequest(&rejected); assert(rejected.code == 503);
    b._onConfigTestRequest(&rejected); assert(rejected.code == 503);
    assert(config.writes == 0 && Interface::get().mqtt.attempts == 0 && !b._flaggedForReboot);
    config.storageReady = true; b._onStoredConfigRequest(&r);
    assert(r.code == 200 && r.response.find("\"configured\":false") != String::npos);
  }
  {
    AsyncWebServerRequest r;
    uint8_t part[] = {'{','}', 'x'};
    BootConfig::__parsePost(&r, part, 1, 0, 2);
    BootConfig::__parsePost(&r, part + 1, 1, 1, 2);
    assert(std::string(static_cast<char*>(r._tempObject)) == "{}");
    BootConfig::__parsePost(&r, part, 3, 1, 2); assert(r._tempObject == nullptr);
    BootConfig::__parsePost(&r, part, 1, SIZE_MAX, 2); assert(r._tempObject == nullptr);
    BootConfig::__parsePost(&r, part, 1, 0, 1501); assert(r._tempObject == nullptr);
    BootConfig::__parsePost(&r, part, 1, 0, 2);
    BootConfig::__parsePost(&r, part, 1, 0, 2); assert(r._tempObject == nullptr);
    BootConfig::__SendJSONError(&r, "bad \"setting\"\nnext line");
    DynamicJsonDocument doc(1024); assert(!deserializeJson(doc, r.response));
    assert(doc["error"] == "bad \"setting\"\nnext line");
  }
  {
    reset(); auto& config = Interface::get().config; config._valid = true;
    HOMIE_FS.stored = R"({"name":"Old","device_id":"j4","wifi":{"ssid":"Home","password":"keep-wifi","ip":"192.0.2.3"},"mqtt":{"host":"old-broker","username":"keep-user","password":"keep-mqtt"},"settings":{"custom":42}})";
    DynamicJsonDocument patch(1024), merged(2048);
    deserializeJson(patch, R"({"mqtt":{"host":"new-broker"},"name":"New"})");
    assert(config.prepare(merged, patch.as<JsonObject>()));
    assert(merged["device_id"] == "j4"); assert(merged["wifi"]["password"] == "keep-wifi");
    assert(merged["wifi"]["ip"] == "192.0.2.3"); assert(merged["settings"]["custom"] == 42);
    assert(merged["mqtt"]["host"] == "new-broker"); assert(merged["mqtt"]["password"] == "keep-mqtt");
    std::unique_ptr<char[]> safe(config.getSafeConfigFile());
    assert(std::string(safe.get()).find("keep-wifi") == String::npos);
    assert(std::string(safe.get()).find("keep-mqtt") == String::npos);
    assert(std::string(safe.get()).find("keep-user") == String::npos);
    assert(std::string(safe.get()).find("j4") != String::npos);
    deserializeJson(patch, R"({"wifi":{"password":""},"mqtt":{"username":null,"password":null,"auth":false}})");
    assert(config.prepare(merged, patch.as<JsonObject>()));
    assert(merged["wifi"]["password"] == ""); assert(!merged["mqtt"].containsKey("password"));
    HOMIE_FS.readable = false; assert(!config.prepare(merged, patch.as<JsonObject>())); HOMIE_FS.readable = true;
    HOMIE_FS.maxRead = 3;
    safe.reset(config.getSafeConfigFile()); assert(std::string(safe.get()) == "{}");
    HOMIE_FS.maxRead = SIZE_MAX; HOMIE_FS.stored = std::string(1200, 'x');
    safe.reset(config.getSafeConfigFile()); assert(std::string(safe.get()) == "{}");
  }
  {
    reset(); BootConfig b; DynamicJsonDocument large(4096); deserializeJson(large, candidate);
    large["settings"]["text"] = std::string(650, 'x');
    String text; serializeJson(large, text); assert(text.size() < MAX_JSON_CONFIG_FILE_SIZE);
    AsyncWebServerRequest r(text); b._onConfigTestRequest(&r); assert(r.code == 202);
  }
  {
    reset(); BootConfig b; queue(b);
    AsyncWebServerRequest duplicate(candidate); b._onConfigTestRequest(&duplicate); assert(duplicate.code == 409);
    b._onConfigRequest(&duplicate); assert(duplicate.code == 409); assert(!Interface::get().config.writes);
    ready(b); AsyncWebServerRequest status; b._onConfigStatusRequest(&status); assert(status.response.find("ready") != String::npos);
    assert(status.response.find("secret") == String::npos);
    AsyncWebServerRequest save(candidate); b._onConfigRequest(&save); assert(save.code == 200);
    assert(b._flaggedForReboot); assert(Interface::get().config.writes == 1);
  }
  {
    reset(); BootConfig b; queue(b); clockMs = UINT32_MAX - 50; b._loopConfigTest();
    clockMs += 30001; b._loopConfigTest(); assert(b._configTestPhase == Phase::FAILED); assert(b._testFailure == 1);
    assert(!Interface::get().config.writes); assert(!b._flaggedForReboot);
    queue(b); ready(b);  // failed credentials can be corrected without a reboot
  }
  for (uint8_t reason : {uint8_t(5), uint8_t(6), uint8_t(8)}) {
    reset(); BootConfig b; queue(b); joinWifi(b); b._testMqttError = reason;
    b._loopConfigTest(); assert(b._configTestPhase == Phase::FAILED);
    assert(b._testFailure == (reason == 8 ? 3 : 2)); assert(!Interface::get().config.writes);
  }
  {
    reset(); BootConfig b; queue(b); joinWifi(b); clockMs += 15001; b._loopConfigTest();
    assert(b._configTestPhase == Phase::FAILED); assert(!b._flaggedForReboot);
  }
  {
    reset(); BootConfig b; queue(b); ready(b); Interface::get().config.writable = false;
    AsyncWebServerRequest save(candidate); b._onConfigRequest(&save); assert(save.code == 500);
    assert(!b._flaggedForReboot); assert(b._configTestPhase == Phase::READY);
  }
  {
    reset(); BootConfig b; queue(b); ready(b);
    AsyncWebServerRequest changed(R"({"name":"different"})"); b._onConfigRequest(&changed);
    assert(changed.code == 409); assert(!Interface::get().config.writes);
  }
  {
    reset(); BootConfig b; AsyncWebServerRequest invalid("{"); b._onConfigTestRequest(&invalid);
    assert(invalid.code == 400); assert(b._configTestPhase == Phase::IDLE);
  }
  std::cout << "Config connection tests: lifetime, timeouts, authentication, retry, concurrent submissions and save errors passed\n";
}
