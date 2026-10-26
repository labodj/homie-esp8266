// Foreground-only connection attempts; transport callbacks publish atomic flags.
bool BootConfig::_claimConfigRequest(ConfigTestPhase expected) {
#ifdef ESP8266
  // Cooperative callbacks never yield here; the toolchain has no atomic CAS.
  if (_configTestPhase.load() != expected) return false;
  _configTestPhase.store(ConfigTestPhase::PREPARING);
  return true;
#else
  return _configTestPhase.compare_exchange_strong(expected, ConfigTestPhase::PREPARING);
#endif
}

void BootConfig::_loopConfigTest() {
  auto phase = _configTestPhase.load();
  auto& mqtt = Interface::get().getMqttClient();
  if (phase == ConfigTestPhase::IDLE || phase == ConfigTestPhase::PREPARING) return;
  mqtt.loop();
  if (phase == ConfigTestPhase::QUEUED) {
    if (!mqtt.disconnected()) return;
    _testMqttConnected.store(false);
    _testMqttError.store(0);
    _testFailure.store(0);
    JsonObject wifi = (*_testConfig)["wifi"];
    WiFi.disconnect(false);
    IPAddress ip, gateway, mask, dns1, dns2;
    if (!wifi["ip"].isNull()) {
      ip.fromString(wifi["ip"].as<const char*>());
      gateway.fromString(wifi["gw"].as<const char*>());
      mask.fromString(wifi["mask"].as<const char*>());
    }
    if (!wifi["dns1"].isNull()) dns1.fromString(wifi["dns1"].as<const char*>());
    if (!wifi["dns2"].isNull()) dns2.fromString(wifi["dns2"].as<const char*>());
    if (!WiFi.config(ip, gateway, mask, dns1, dns2)) {
      _testFailure.store(1);
      _configTestPhase.store(ConfigTestPhase::FAILED);
      return;
    }
    if (!wifi["bssid"].isNull()) {
      uint8_t bssid[6];
      Helpers::stringToBytes(wifi["bssid"].as<const char*>(), ':', bssid, 6, 16);
      WiFi.begin(wifi["ssid"].as<const char*>(), wifi["password"].as<const char*>(), wifi["channel"].as<uint16_t>(), bssid);
    } else {
      WiFi.begin(wifi["ssid"].as<const char*>(), wifi["password"].as<const char*>());
    }
    WiFi.setAutoReconnect(false);
    _testStartedAt = millis();
    _configTestPhase.store(ConfigTestPhase::WIFI);
  } else if (phase == ConfigTestPhase::WIFI) {
    JsonObject wifi = (*_testConfig)["wifi"];
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == wifi["ssid"].as<const char*>()) {
      JsonObject config = (*_testConfig)["mqtt"];
      mqtt.setServer(config["host"].as<const char*>(), config["port"] | DEFAULT_MQTT_PORT);
      mqtt.setClientId(_testClientId.c_str());
      mqtt.setCleanSession(true);
      mqtt.setCredentials(config["auth"].as<bool>() ? config["username"].as<const char*>() : nullptr,
                          config["auth"].as<bool>() ? config["password"].as<const char*>() : nullptr);
      _testStartedAt = millis();
      _configTestPhase.store(ConfigTestPhase::MQTT);
      if (!mqtt.connect()) _testMqttError.store(1);
    } else if (millis() - _testStartedAt >= 30000UL) {
      _testFailure.store(1);
      _configTestPhase.store(ConfigTestPhase::FAILED);
    }
  } else if (phase == ConfigTestPhase::MQTT) {
    if (_testMqttConnected.load() && mqtt.connected() && WiFi.status() == WL_CONNECTED) {
      mqtt.disconnect(true);
      _configTestPhase.store(ConfigTestPhase::CLOSING);
    } else if (_testMqttError.load() || millis() - _testStartedAt >= 15000UL || WiFi.status() != WL_CONNECTED) {
      const uint8_t reason = _testMqttError.load();
      _testFailure.store(reason == 5 || reason == 6 ? 2 : 3);
      mqtt.disconnect(true);
      _configTestPhase.store(ConfigTestPhase::FAILED);
    }
  } else if (phase == ConfigTestPhase::CLOSING) {
    if (mqtt.disconnected()) _configTestPhase.store(ConfigTestPhase::READY);
    else if (millis() - _testStartedAt >= 20000UL) {
      _testFailure.store(3);
      _configTestPhase.store(ConfigTestPhase::FAILED);
    }
  }
}

void BootConfig::_onConfigRequest(AsyncWebServerRequest *request) {
  Interface::get().getLogger() << F("Received config request") << endl;
  if (_flaggedForReboot) {
    __SendJSONError(request, F("✖ Device already configured"), 403);
    return;
  }
  auto testPhase = _configTestPhase.load();
  if (testPhase != ConfigTestPhase::IDLE && testPhase != ConfigTestPhase::READY) {
    __SendJSONError(request, F("Finish a successful connection test before saving"), 409);
    return;
  }
  if (!_claimConfigRequest(testPhase)) {
    __SendJSONError(request, F("Another configuration request is running"), 409);
    return;
  }
  DynamicJsonDocument parseJsonDoc(MAX_JSON_CONFIG_OWNED_BUFFER_SIZE);
  if (!_parseConfig(request, parseJsonDoc)) { _configTestPhase.store(testPhase); return; }
  JsonObject parsedJson = parseJsonDoc.as<JsonObject>();
  if (testPhase == ConfigTestPhase::READY) {
    String submitted, tested;
    serializeJson(parsedJson, submitted);
    serializeJson(*_testConfig, tested);
    if (submitted != tested) {
      _configTestPhase.store(testPhase);
      __SendJSONError(request, F("Configuration changed. Test the new values before saving."), 409);
      return;
    }
  }
  if (!Interface::get().getConfig().write(parsedJson)) {
    _configTestPhase.store(testPhase);
    __SendJSONError(request, F("Cannot save configuration. Previous configuration preserved; try again."), 500);
    return;
  }

  Interface::get().getLogger() << F("✔ Configured") << endl;
  request->send(200, FPSTR(PROGMEM_CONFIG_APPLICATION_JSON), FPSTR(PROGMEM_CONFIG_JSON_SUCCESS));
  Interface::get().disable = true;
  _flaggedForRebootAt = millis();
  _configTestPhase.store(ConfigTestPhase::SAVED);
  _flaggedForReboot.store(true);
}

bool BootConfig::_parseConfig(AsyncWebServerRequest *request, JsonDocument& parseJsonDoc) {
  if (request->_tempObject == nullptr) {
    __SendJSONError(request, F("✖ Empty or too large JSON"));
    return false;
  }

  const char* body = reinterpret_cast<const char*>(request->_tempObject);
  DynamicJsonDocument changes(MAX_JSON_CONFIG_OWNED_BUFFER_SIZE);
  if (deserializeJson(changes, body) != DeserializationError::Ok || !changes.is<JsonObject>()) {
    __SendJSONError(request, F("✖ Invalid or too big JSON"));
    return false;
  }
  if (!Interface::get().getConfig().prepare(parseJsonDoc, changes.as<JsonObject>())) {
    __SendJSONError(request, F("Cannot prepare configuration; stored settings have not been changed"), 503);
    return false;
  }

  JsonObject parsedJson = parseJsonDoc.as<JsonObject>();

  ConfigValidationResult configValidationResult = Validation::validateConfig(parsedJson);
  if (!configValidationResult.valid) {
    __SendJSONError(request, String(F("✖ Config file is not valid, reason: ")) + configValidationResult.reason);
    return false;
  }

  // The saved file must also fit the buffer used on the next boot.
  if (measureJson(parsedJson) >= MAX_JSON_CONFIG_FILE_SIZE) {
    __SendJSONError(request, F("Config file is too large"));
    return false;
  }
  return true;
}

void BootConfig::_onStoredConfigRequest(AsyncWebServerRequest *request) {
  const bool configured = Interface::get().getConfig().isValid();
  std::unique_ptr<char[]> safe(configured ? Interface::get().getConfig().getSafeConfigFile() : nullptr);
  String output = configured ? "{\"configured\":true,\"config\":" : "{\"configured\":false,\"config\":";
  output += safe ? safe.get() : "{}";
  output += "}";
  auto* response = request->beginResponse(200, FPSTR(PROGMEM_CONFIG_APPLICATION_JSON), output);
  response->addHeader(F("Cache-Control"), F("no-store"));
  request->send(response);
}

void BootConfig::_onConfigTestRequest(AsyncWebServerRequest *request) {
  auto phase = _configTestPhase.load();
  if (_flaggedForReboot || !Interface::get().getMqttClient().disconnected()
      || (phase != ConfigTestPhase::IDLE && phase != ConfigTestPhase::READY && phase != ConfigTestPhase::FAILED)
      || !_claimConfigRequest(phase)) {
    __SendJSONError(request, F("A connection test is already running; wait and retry"), 409);
    return;
  }
  std::unique_ptr<DynamicJsonDocument> candidate(new (std::nothrow) DynamicJsonDocument(MAX_JSON_CONFIG_OWNED_BUFFER_SIZE));
  if (!candidate || candidate->capacity() == 0) {
    _configTestPhase.store(phase);
    __SendJSONError(request, F("Not enough memory to test configuration"), 503);
    return;
  }
  if (!_parseConfig(request, *candidate)) { _configTestPhase.store(phase); return; }
  _testConfig = std::move(candidate);
  _configTestPhase.store(ConfigTestPhase::QUEUED);
  request->send(202, FPSTR(PROGMEM_CONFIG_APPLICATION_JSON), FPSTR(PROGMEM_CONFIG_JSON_SUCCESS));
}

void BootConfig::_onConfigStatusRequest(AsyncWebServerRequest *request) {
  const auto phase = _configTestPhase.load();
  const char* state = "idle";
  const char* error = "";
  switch (phase) {
    case ConfigTestPhase::PREPARING:
    case ConfigTestPhase::QUEUED:
    case ConfigTestPhase::WIFI: state = "wifi"; break;
    case ConfigTestPhase::MQTT:
    case ConfigTestPhase::CLOSING: state = "mqtt"; break;
    case ConfigTestPhase::READY: state = "ready"; break;
    case ConfigTestPhase::SAVED: state = "saved"; break;
    case ConfigTestPhase::FAILED:
      state = "failed";
      switch (_testFailure.load()) {
        case 1: error = "Cannot join Wi-Fi. Check the network name and password (2.4 GHz required)."; break;
        case 2: error = "MQTT authentication rejected. Check the username and password."; break;
        default: error = "Cannot connect to MQTT. Check the broker address, port and availability."; break;
      }
      break;
    default: break;
  }
  StaticJsonDocument<JSON_OBJECT_SIZE(2)> doc;
  doc["status"] = state;
  doc["error"] = error;
  String output;
  serializeJson(doc, output);
  auto* response = request->beginResponse(200, FPSTR(PROGMEM_CONFIG_APPLICATION_JSON), output);
  response->addHeader(F("Cache-Control"), F("no-store"));
  request->send(response);
}
