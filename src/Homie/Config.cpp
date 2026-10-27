#include "Config.hpp"
#include "Utils/ConfigFile.hpp"
#ifdef ESP32
#include <esp_partition.h>
#elif defined(ESP8266)
#include <flash_hal.h>
#endif

using namespace HomieInternals;

#include "ConfigJson.ipp"

Config::Config()
  : _configStruct()
  , _filesystemBegan(false)
  , _valid(false)
  , _recovered(false)
  , _recoveryFailed(false) {
}

#include "ConfigFilesystem.ipp"

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

  // Migration is opt-in; probing SPIFFS must never format an unreadable partition.
#ifdef ESP8266
  FSInfo spiffsInfo;
  if (!SPIFFS.setConfig(SPIFFSConfig(false)) && !SPIFFS.info(spiffsInfo)) return false;
#endif
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
  _valid = false;
  _recovered = false;
  _recoveryFailed = false;
  if (!_filesystemBegin()) { return false; }
  const auto result = ConfigFile::load(HOMIE_FS, CONFIG_FILE_PATH, CONFIG_BACKUP_FILE_PATH,
                                       [this](const char* path) { return _loadFile(path); });
  _valid = result == ConfigFile::LoadResult::PRIMARY || result == ConfigFile::LoadResult::RECOVERED;
  _recovered = result == ConfigFile::LoadResult::RECOVERED;
  _recoveryFailed = result == ConfigFile::LoadResult::RECOVERY_FAILED;
  return _valid;
}

bool Config::_loadFile(const char* path) {
  if (!HOMIE_FS.exists(path)) {
    Interface::get().getLogger() << F("✖ ") << path << F(" doesn't exist") << endl;
    return false;
  }

  File configFile = HOMIE_FS.open(path, "r");
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
  const size_t configRead = configFile.readBytes(buf, configSize);
  configFile.close();
  if (configRead != configSize) return false;
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
  bool reqMqttAuth = reqMqtt["auth"] | false;
  const char* reqMqttUsername = reqMqtt["username"] | "";
  const char* reqMqttPassword = reqMqtt["password"] | "";
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

bool Config::erase() {
  if (!_filesystemBegin()) { return false; }
  return ConfigFile::erase(HOMIE_FS, CONFIG_FILE_PATH, CONFIG_TEMP_FILE_PATH,
                           CONFIG_BACKUP_FILE_PATH, CONFIG_NEXT_BOOT_MODE_FILE_PATH);
}

bool Config::setHomieBootModeOnNextBoot(HomieBootMode bootMode) {
  if (!_filesystemBegin()) { return false; }
  if (!_ensureFilesystemDirectories()) { return false; }

  if (bootMode == HomieBootMode::UNDEFINED) {
    return !HOMIE_FS.exists(CONFIG_NEXT_BOOT_MODE_FILE_PATH) || HOMIE_FS.remove(CONFIG_NEXT_BOOT_MODE_FILE_PATH);
  } else {
    File bootModeFile = HOMIE_FS.open(CONFIG_NEXT_BOOT_MODE_FILE_PATH, "w");
    if (!bootModeFile) {
      Interface::get().getLogger() << F("✖ Cannot open NEXTMODE file") << endl;
      return false;
    }

    const bool written = bootModeFile.printf("#%d", static_cast<int>(bootMode)) == 2;
    bootModeFile.flush();
    bootModeFile.close();
    if (!written || getHomieBootModeOnNextBoot() != bootMode) return false;
    Interface::get().getLogger().printf("Setting next boot mode to %d\n", static_cast<int>(bootMode));
  }
  return true;
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

bool Config::write(const JsonObject config) {
  if (_recoveryFailed) return false;
  if (!_filesystemBegin() || !_ensureFilesystemDirectories()) return false;
  if (!Validation::validateConfig(config).valid || measureJson(config) >= MAX_JSON_CONFIG_FILE_SIZE) return false;
  String serialized;
  const size_t size = measureJson(config);
  if (serializeJson(config, serialized) != size || serialized.length() != size) return false;
  return ConfigFile::write(HOMIE_FS, CONFIG_FILE_PATH, CONFIG_TEMP_FILE_PATH,
                           CONFIG_BACKUP_FILE_PATH, serialized.c_str(), size);
}

bool Config::patch(const char* patch) {
  if (_recoveryFailed) return false;
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

  if (configSize >= MAX_JSON_CONFIG_FILE_SIZE) return false;

  char configJson[MAX_JSON_CONFIG_FILE_SIZE];
  const size_t configRead = configFile.readBytes(configJson, configSize);
  configFile.close();
  if (configRead != configSize) return false;
  configJson[configSize] = '\0';

  StaticJsonDocument<MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE> configJsonDoc;
  if (deserializeJson(configJsonDoc, configJson) != DeserializationError::Ok || !configJsonDoc.is<JsonObject>()) return false;
  JsonObject configObject = configJsonDoc.as<JsonObject>();

  _patchJsonObject(configObject, patchObject);
  if (configJsonDoc.overflowed()) return false;
  // mqtt.effective_base_topic is published for diagnostics only. If a consumer
  // echoes $implementation/config into /config/set, keep the persisted config
  // clean and let the advertiser regenerate the value from mqtt.base_topic.
  configObject["mqtt"].as<JsonObject>().remove("effective_base_topic");

  ConfigValidationResult configValidationResult = Validation::validateConfig(configObject);
  if (!configValidationResult.valid) {
    Interface::get().getLogger() << F("✖ Config file is not valid, reason: ") << configValidationResult.reason << endl;
    return false;
  }

  return write(configObject);
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
