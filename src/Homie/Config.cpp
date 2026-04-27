#include "Config.hpp"

using namespace HomieInternals;

Config::Config()
  : _configStruct()
  , _filesystemBegan(false)
  , _valid(false) {
}

bool Config::_filesystemBegin() {
  if (!_filesystemBegan) {
    // Always try a non-formatting mount first. In a LittleFS migration build a
    // mount failure is the signal that a provisioned SPIFFS image may still
    // exist on the same flash area and must be read before anything is erased.
    if (!_mountSelectedFilesystem(false)) {
      if (_migrateSpiffsToLittleFs()) {
        return true;
      }

      const bool formatOnFail =
#ifdef ESP32
        true;
#elif defined(ESP8266)
        HOMIE_USE_LITTLEFS;
#else
        false;
#endif
      if (formatOnFail) {
        Interface::get().getLogger() << F("! Formatting ") << HOMIE_FS_NAME << F(" after mount failure") << endl;
        _mountSelectedFilesystem(true);
      }
    }

    if (!_filesystemBegan) Interface::get().getLogger() << F("✖ Cannot mount ") << HOMIE_FS_NAME << endl;
  }

  return _filesystemBegan;
}

bool Config::_mountSelectedFilesystem(bool formatOnFail) {
#ifdef ESP32
  _filesystemBegan = HOMIE_FS.begin(formatOnFail);
#elif defined(ESP8266)
  _filesystemBegan = HOMIE_FS.begin();
  if (!_filesystemBegan && formatOnFail) {
    if (HOMIE_FS.format()) {
      _filesystemBegan = HOMIE_FS.begin();
    }
  }
#endif

  return _filesystemBegan;
}

bool Config::_ensureFilesystemDirectories() {
#if HOMIE_USE_LITTLEFS
  // LittleFS has real directories. SPIFFS accepted nested-looking paths without
  // creating parent directories, so create /homie explicitly when LittleFS is used.
  if (HOMIE_FS.exists(CONFIG_DIRECTORY_PATH)) return true;
  if (HOMIE_FS.mkdir(CONFIG_DIRECTORY_PATH)) return true;

  Interface::get().getLogger() << F("✖ Cannot create filesystem directory ") << CONFIG_DIRECTORY_PATH << endl;
  return false;
#else
  return true;
#endif
}

bool Config::_migrateSpiffsToLittleFs() {
#if HOMIE_USE_LITTLEFS && HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS
  Interface::get().getLogger() << F("! ") << HOMIE_FS_NAME
                               << F(" mount failed; checking for a SPIFFS configuration to migrate") << endl;

  // The migration path is deliberately gated by config.json. Without it there is
  // no provisioned Homie state to preserve, so the normal mount/format fallback
  // should handle the selected filesystem instead.
  if (!SPIFFS.begin()) {
    Interface::get().getLogger() << F("! No mountable SPIFFS filesystem found for migration") << endl;
    return false;
  }

  if (!SPIFFS.exists(CONFIG_FILE_PATH)) {
    SPIFFS.end();
    Interface::get().getLogger() << F("! SPIFFS migration skipped: ") << CONFIG_FILE_PATH << F(" not found") << endl;
    return false;
  }

  File configFile = SPIFFS.open(CONFIG_FILE_PATH, "r");
  if (!configFile) {
    SPIFFS.end();
    Interface::get().getLogger() << F("✖ SPIFFS migration failed: cannot open config file") << endl;
    return false;
  }

  const size_t configSize = configFile.size();
  if (configSize == 0 || configSize >= MAX_JSON_CONFIG_FILE_SIZE) {
    configFile.close();
    SPIFFS.end();
    Interface::get().getLogger() << F("✖ SPIFFS migration failed: config file size is not supported") << endl;
    return false;
  }

  char configBuffer[MAX_JSON_CONFIG_FILE_SIZE];
  configFile.readBytes(configBuffer, configSize);
  configFile.close();

  // NEXTMODE is optional operational state. Copy it only when it fits in this
  // bounded buffer; a malformed oversized file must not block config migration.
  bool nextModePresent = false;
  char nextModeBuffer[8];
  size_t nextModeSize = 0;
  File nextModeFile = SPIFFS.open(CONFIG_NEXT_BOOT_MODE_FILE_PATH, "r");
  if (nextModeFile) {
    nextModeSize = nextModeFile.size();
    if (nextModeSize > 0 && nextModeSize < sizeof(nextModeBuffer)) {
      nextModeFile.readBytes(nextModeBuffer, nextModeSize);
      nextModePresent = true;
    }
    nextModeFile.close();
  }

  const bool uiBundlePresent = SPIFFS.exists(CONFIG_UI_BUNDLE_PATH);
  SPIFFS.end();

  // SPIFFS and LittleFS use the same flash partition in the migration image.
  // Once LittleFS is formatted the old SPIFFS contents are gone, so only small,
  // bounded files are copied through RAM.
  if (!HOMIE_FS.format() || !_mountSelectedFilesystem(false)) {
    Interface::get().getLogger() << F("✖ SPIFFS migration failed: cannot format or mount ") << HOMIE_FS_NAME << endl;
    return false;
  }
  if (!_ensureFilesystemDirectories()) {
    Interface::get().getLogger() << F("✖ SPIFFS migration failed: cannot prepare ") << HOMIE_FS_NAME << endl;
    return false;
  }

  File migratedConfig = HOMIE_FS.open(CONFIG_FILE_PATH, "w");
  if (!migratedConfig) {
    Interface::get().getLogger() << F("✖ SPIFFS migration failed: cannot write config file") << endl;
    return false;
  }
  const size_t configWritten = migratedConfig.write(reinterpret_cast<const uint8_t*>(configBuffer), configSize);
  migratedConfig.close();
  if (configWritten != configSize) {
    Interface::get().getLogger() << F("✖ SPIFFS migration failed: config file was not fully written") << endl;
    return false;
  }

  if (nextModePresent) {
    File migratedNextMode = HOMIE_FS.open(CONFIG_NEXT_BOOT_MODE_FILE_PATH, "w");
    if (migratedNextMode) {
      const size_t nextModeWritten = migratedNextMode.write(reinterpret_cast<const uint8_t*>(nextModeBuffer), nextModeSize);
      migratedNextMode.close();
      if (nextModeWritten != nextModeSize) {
        Interface::get().getLogger() << F("! SPIFFS migration skipped NEXTMODE: file was not fully written") << endl;
      }
    } else {
      Interface::get().getLogger() << F("! SPIFFS migration skipped NEXTMODE: cannot write file") << endl;
    }
  }

  Interface::get().getLogger() << F("✔ Migrated Homie configuration from SPIFFS to ") << HOMIE_FS_NAME << endl;
  if (uiBundlePresent) {
    // This warning is informational. The UI bundle is intentionally excluded
    // from migration because it can be much larger than the bounded config copy.
    Interface::get().getLogger() << F("! UI bundle was not migrated; upload it again to ") << HOMIE_FS_NAME << endl;
  }
  return true;
#else
  return false;
#endif
}

bool Config::load() {
  if (!_filesystemBegin()) { return false; }

  _valid = false;

  if (!HOMIE_FS.exists(CONFIG_FILE_PATH)) {
    Interface::get().getLogger() << F("✖ ") << CONFIG_FILE_PATH << F(" doesn't exist") << endl;
    return false;
  }

  File configFile = HOMIE_FS.open(CONFIG_FILE_PATH, "r");
  if (!configFile) {
    Interface::get().getLogger() << F("✖ Cannot open config file") << endl;
    return false;
  }

  size_t configSize = configFile.size();

  if (configSize >= MAX_JSON_CONFIG_FILE_SIZE) {
    Interface::get().getLogger() << F("✖ Config file too big") << endl;
    return false;
  }

  char buf[MAX_JSON_CONFIG_FILE_SIZE];
  configFile.readBytes(buf, configSize);
  configFile.close();
  buf[configSize] = '\0';

  StaticJsonDocument<MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE> jsonDoc;
  if (deserializeJson(jsonDoc, buf) != DeserializationError::Ok || !jsonDoc.is<JsonObject>()) {
    Interface::get().getLogger() << F("✖ Invalid JSON in the config file") << endl;
    return false;
  }

  JsonObject parsedJson = jsonDoc.as<JsonObject>();
  ConfigValidationResult configValidationResult = Validation::validateConfig(parsedJson);
  if (!configValidationResult.valid) {
    Interface::get().getLogger() << F("✖ Config file is not valid, reason: ") << configValidationResult.reason << endl;
    return false;
  }

  /* Mandatory config items */
  JsonObject reqWifi = parsedJson["wifi"];
  JsonObject reqMqtt = parsedJson["mqtt"];

  const char* reqName = parsedJson["name"];
  const char* reqWifiSsid = reqWifi["ssid"];
  const char* reqMqttHost = reqMqtt["host"];

  /* Optional config items */
  const char* reqDeviceId = parsedJson["device_id"] | DeviceId::get();
  uint16_t regDeviceStatsInterval = parsedJson["device_stats_interval"] | STATS_SEND_INTERVAL_SEC;
  bool reqOtaEnabled = parsedJson["ota"]["enabled"] | false;

  uint16_t reqWifiChannel = reqWifi["channel"] | 0;
  const char* reqWifiBssid = reqWifi["bssid"] | "";
  const char* reqWifiPassword = reqWifi["password"]; // implicit | nullptr;
  const char* reqWifiIp = reqWifi["ip"] | "";
  const char* reqWifiMask = reqWifi["mask"] | "";
  const char* reqWifiGw = reqWifi["gw"] | "";
  const char* reqWifiDns1 = reqWifi["dns1"] | "";
  const char* reqWifiDns2 = reqWifi["dns2"] | "";

  uint16_t reqMqttPort = reqMqtt["port"] | DEFAULT_MQTT_PORT;
  bool reqMqttSsl = reqMqtt["ssl"] | false;
  bool reqMqttAuth = reqMqtt["auth"] | false;
  const char* reqMqttUsername = reqMqtt["username"] | "";
  const char* reqMqttPassword = reqMqtt["password"] | "";
  const char* reqMqttFingerprint = reqMqtt["ssl_fingerprint"] | "";
  const char* reqMqttBaseTopic = reqMqtt["base_topic"] | DEFAULT_MQTT_BASE_TOPIC;

  strlcpy(_configStruct.name, reqName, MAX_FRIENDLY_NAME_LENGTH);
  strlcpy(_configStruct.deviceId, reqDeviceId, MAX_DEVICE_ID_LENGTH);
  _configStruct.deviceStatsInterval = regDeviceStatsInterval;
  strlcpy(_configStruct.wifi.ssid, reqWifiSsid, MAX_WIFI_SSID_LENGTH);
  if (reqWifiPassword) strlcpy(_configStruct.wifi.password, reqWifiPassword, MAX_WIFI_PASSWORD_LENGTH);
  strlcpy(_configStruct.wifi.bssid, reqWifiBssid, MAX_MAC_STRING_LENGTH + 6);
  _configStruct.wifi.channel = reqWifiChannel;
  strlcpy(_configStruct.wifi.ip, reqWifiIp, MAX_IP_STRING_LENGTH);
  strlcpy(_configStruct.wifi.gw, reqWifiGw, MAX_IP_STRING_LENGTH);
  strlcpy(_configStruct.wifi.mask, reqWifiMask, MAX_IP_STRING_LENGTH);
  strlcpy(_configStruct.wifi.dns1, reqWifiDns1, MAX_IP_STRING_LENGTH);
  strlcpy(_configStruct.wifi.dns2, reqWifiDns2, MAX_IP_STRING_LENGTH);
  strlcpy(_configStruct.mqtt.server.host, reqMqttHost, MAX_HOSTNAME_LENGTH);
#if ASYNC_TCP_SSL_ENABLED
  _configStruct.mqtt.server.ssl.enabled = reqMqttSsl;
  if (strcmp_P(reqMqttFingerprint, PSTR("")) != 0) {
    _configStruct.mqtt.server.ssl.hasFingerprint = true;
    Helpers::hexStringToByteArray(reqMqttFingerprint, _configStruct.mqtt.server.ssl.fingerprint, MAX_FINGERPRINT_SIZE);
  }
#endif
  _configStruct.mqtt.server.port = reqMqttPort;
  strlcpy(_configStruct.mqtt.baseTopic, reqMqttBaseTopic, MAX_MQTT_BASE_TOPIC_LENGTH);
  _configStruct.mqtt.auth = reqMqttAuth;
  strlcpy(_configStruct.mqtt.username, reqMqttUsername, MAX_MQTT_CREDS_LENGTH);
  strlcpy(_configStruct.mqtt.password, reqMqttPassword, MAX_MQTT_CREDS_LENGTH);
  _configStruct.ota.enabled = reqOtaEnabled;

  /* Parse the settings */

  JsonObject settingsObject = parsedJson["settings"].as<JsonObject>();

  for (IHomieSetting* iSetting : IHomieSetting::settings) {
    JsonVariant reqSetting = settingsObject[iSetting->getName()];

    if (!reqSetting.isNull()) {
      if (iSetting->isBool()) {
        HomieSetting<bool>* setting = static_cast<HomieSetting<bool>*>(iSetting);
        setting->set(reqSetting.as<bool>());
      } else if (iSetting->isLong()) {
        HomieSetting<long>* setting = static_cast<HomieSetting<long>*>(iSetting);
        setting->set(reqSetting.as<long>());
      } else if (iSetting->isDouble()) {
        HomieSetting<double>* setting = static_cast<HomieSetting<double>*>(iSetting);
        setting->set(reqSetting.as<double>());
      } else if (iSetting->isConstChar()) {
        HomieSetting<const char*>* setting = static_cast<HomieSetting<const char*>*>(iSetting);
        setting->set(reqSetting.as<const char*>());
      }
    }
  }

  _valid = true;
  return true;
}

char* Config::getSafeConfigFile() const {
  File configFile = HOMIE_FS.open(CONFIG_FILE_PATH, "r");
  size_t configSize = configFile.size();

  char buf[MAX_JSON_CONFIG_FILE_SIZE];
  configFile.readBytes(buf, configSize);
  configFile.close();
  buf[configSize] = '\0';

  StaticJsonDocument<MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE> jsonDoc;
  deserializeJson(jsonDoc, buf);
  JsonObject parsedJson = jsonDoc.as<JsonObject>();
  parsedJson["wifi"].as<JsonObject>().remove("password");
  JsonObject mqtt = parsedJson["mqtt"].as<JsonObject>();
  mqtt.remove("username");
  mqtt.remove("password");
  // Runtime-only diagnostics are injected when advertising over MQTT. Do not
  // leak a stale value if a previous tool copied the advertised payload back
  // into config.json.
  mqtt.remove("effective_base_topic");

  size_t jsonBufferLength = measureJson(jsonDoc) + 1;
  char* jsonString = new char[jsonBufferLength];
  serializeJson(jsonDoc, jsonString, jsonBufferLength);
  return jsonString;
}

void Config::erase() {
  if (!_filesystemBegin()) { return; }

  HOMIE_FS.remove(CONFIG_FILE_PATH);
  HOMIE_FS.remove(CONFIG_NEXT_BOOT_MODE_FILE_PATH);
}

void Config::setHomieBootModeOnNextBoot(HomieBootMode bootMode) {
  if (!_filesystemBegin()) { return; }
  if (!_ensureFilesystemDirectories()) { return; }

  if (bootMode == HomieBootMode::UNDEFINED) {
    HOMIE_FS.remove(CONFIG_NEXT_BOOT_MODE_FILE_PATH);
  } else {
    File bootModeFile = HOMIE_FS.open(CONFIG_NEXT_BOOT_MODE_FILE_PATH, "w");
    if (!bootModeFile) {
      Interface::get().getLogger() << F("✖ Cannot open NEXTMODE file") << endl;
      return;
    }

    bootModeFile.printf("#%d", static_cast<int>(bootMode));
    bootModeFile.close();
    Interface::get().getLogger().printf("Setting next boot mode to %d\n", static_cast<int>(bootMode));
  }
}

HomieBootMode Config::getHomieBootModeOnNextBoot() {
  if (!_filesystemBegin()) { return HomieBootMode::UNDEFINED; }

  File bootModeFile = HOMIE_FS.open(CONFIG_NEXT_BOOT_MODE_FILE_PATH, "r");
  if (bootModeFile) {
    int v = bootModeFile.parseInt();
    bootModeFile.close();
    return static_cast<HomieBootMode>(v);
  } else {
    return HomieBootMode::UNDEFINED;
  }
}

void Config::write(const JsonObject config) {
  if (!_filesystemBegin()) { return; }
  if (!_ensureFilesystemDirectories()) { return; }

  HOMIE_FS.remove(CONFIG_FILE_PATH);

  File configFile = HOMIE_FS.open(CONFIG_FILE_PATH, "w");
  if (!configFile) {
    Interface::get().getLogger() << F("✖ Cannot open config file") << endl;
    return;
  }
  serializeJson(config, configFile);
  configFile.close();
}

bool Config::patch(const char* patch) {
  if (!_filesystemBegin()) { return false; }

  StaticJsonDocument<MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE> patchJsonDoc;

  if (deserializeJson(patchJsonDoc, patch) != DeserializationError::Ok || !patchJsonDoc.is<JsonObject>()) {
    Interface::get().getLogger() << F("✖ Invalid or too big JSON") << endl;
    return false;
  }

  JsonObject patchObject = patchJsonDoc.as<JsonObject>();
  File configFile = HOMIE_FS.open(CONFIG_FILE_PATH, "r");
  if (!configFile) {
    Interface::get().getLogger() << F("✖ Cannot open config file") << endl;
    return false;
  }

  size_t configSize = configFile.size();

  char configJson[MAX_JSON_CONFIG_FILE_SIZE];
  configFile.readBytes(configJson, configSize);
  configFile.close();
  configJson[configSize] = '\0';

  StaticJsonDocument<MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE> configJsonDoc;
  deserializeJson(configJsonDoc, configJson);
  JsonObject configObject = configJsonDoc.as<JsonObject>();

  _patchJsonObject(configObject, patchObject);
  // mqtt.effective_base_topic is published for diagnostics only. If a consumer
  // echoes $implementation/config into /config/set, keep the persisted config
  // clean and let the advertiser regenerate the value from mqtt.base_topic.
  configObject["mqtt"].as<JsonObject>().remove("effective_base_topic");

  ConfigValidationResult configValidationResult = Validation::validateConfig(configObject);
  if (!configValidationResult.valid) {
    Interface::get().getLogger() << F("✖ Config file is not valid, reason: ") << configValidationResult.reason << endl;
    return false;
  }

  write(configObject);

  return true;
}

void Config::_patchJsonObject(JsonObject object, JsonObject patch) {
  for (JsonPair patchItem : patch) {
    JsonVariant patchElement = patchItem.value();
    JsonVariant objectElement = object[patchItem.key()];

    // If both object element and patch element are objects, then recursively call this method again
    if (objectElement.is<JsonObject>() && patchElement.is<JsonObject>()) {
      _patchJsonObject(objectElement.as<JsonObject>(), patchElement.as<JsonObject>());
    } else {
      // Delete the object element if the patch element is null
      if (patchElement.isNull()) {
        object.remove(patchItem.key());
      } else {
        // Otherwise replace the object element value with the patch element value
        object[patchItem.key()] = patchElement;
      }
    }
  }
}

bool Config::isValid() const {
  return this->_valid;
}

void Config::log() const {
  Interface::get().getLogger() << F("{} Stored configuration") << endl;
  Interface::get().getLogger() << F("  • Hardware device ID: ") << DeviceId::get() << endl;
  Interface::get().getLogger() << F("  • Device ID: ") << _configStruct.deviceId << endl;
  Interface::get().getLogger() << F("  • Name: ") << _configStruct.name << endl;
  Interface::get().getLogger() << F("  • Device Stats Interval: ") << _configStruct.deviceStatsInterval << F(" sec") << endl;

  Interface::get().getLogger() << F("  • Wi-Fi: ") << endl;
  Interface::get().getLogger() << F("    ◦ SSID: ") << _configStruct.wifi.ssid << endl;
  Interface::get().getLogger() << F("    ◦ Password not shown") << endl;
  if (strcmp_P(_configStruct.wifi.ip, PSTR("")) != 0) {
    Interface::get().getLogger() << F("    ◦ IP: ") << _configStruct.wifi.ip << endl;
    Interface::get().getLogger() << F("    ◦ Mask: ") << _configStruct.wifi.mask << endl;
    Interface::get().getLogger() << F("    ◦ Gateway: ") << _configStruct.wifi.gw << endl;
  }
  Interface::get().getLogger() << F("  • MQTT: ") << endl;
  Interface::get().getLogger() << F("    ◦ Host: ") << _configStruct.mqtt.server.host << endl;
  Interface::get().getLogger() << F("    ◦ Port: ") << _configStruct.mqtt.server.port << endl;
#if ASYNC_TCP_SSL_ENABLED
  Interface::get().getLogger() << F("    ◦ SSL enabled: ") << (_configStruct.mqtt.server.ssl.enabled ? "true" : "false") << endl;
  if (_configStruct.mqtt.server.ssl.enabled && _configStruct.mqtt.server.ssl.hasFingerprint) {
    char hexBuf[MAX_FINGERPRINT_STRING_LENGTH];
    Helpers::byteArrayToHexString(Interface::get().getConfig().get().mqtt.server.ssl.fingerprint, hexBuf, MAX_FINGERPRINT_SIZE);
    Interface::get().getLogger() << F("    ◦ Fingerprint: ") << hexBuf << endl;
  }
#endif
  Interface::get().getLogger() << F("    ◦ Base topic: ") << _configStruct.mqtt.baseTopic << endl;
  Interface::get().getLogger() << F("    ◦ Auth? ") << (_configStruct.mqtt.auth ? F("yes") : F("no")) << endl;
  if (_configStruct.mqtt.auth) {
    Interface::get().getLogger() << F("    ◦ Username: ") << _configStruct.mqtt.username << endl;
    Interface::get().getLogger() << F("    ◦ Password not shown") << endl;
  }

  Interface::get().getLogger() << F("  • OTA: ") << endl;
  Interface::get().getLogger() << F("    ◦ Enabled? ") << (_configStruct.ota.enabled ? F("yes") : F("no")) << endl;

  if (IHomieSetting::settings.size() > 0) {
    Interface::get().getLogger() << F("  • Custom settings: ") << endl;
    for (IHomieSetting* iSetting : IHomieSetting::settings) {
      Interface::get().getLogger() << F("    ◦ ");

      if (iSetting->isBool()) {
        HomieSetting<bool>* setting = static_cast<HomieSetting<bool>*>(iSetting);
        Interface::get().getLogger() << setting->getName() << F(": ") << setting->get() << F(" (") << (setting->wasProvided() ? F("set") : F("default")) << F(")");
      } else if (iSetting->isLong()) {
        HomieSetting<long>* setting = static_cast<HomieSetting<long>*>(iSetting);
        Interface::get().getLogger() << setting->getName() << F(": ") << setting->get() << F(" (") << (setting->wasProvided() ? F("set") : F("default")) << F(")");
      } else if (iSetting->isDouble()) {
        HomieSetting<double>* setting = static_cast<HomieSetting<double>*>(iSetting);
        Interface::get().getLogger() << setting->getName() << F(": ") << setting->get() << F(" (") << (setting->wasProvided() ? F("set") : F("default")) << F(")");
      } else if (iSetting->isConstChar()) {
        HomieSetting<const char*>* setting = static_cast<HomieSetting<const char*>*>(iSetting);
        Interface::get().getLogger() << setting->getName() << F(": ") << setting->get() << F(" (") << (setting->wasProvided() ? F("set") : F("default")) << F(")");
      }

      Interface::get().getLogger() << endl;
    }
  }
}
