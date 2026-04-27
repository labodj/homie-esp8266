#include "BootNormal.hpp"

#include <new>

using namespace HomieInternals;

namespace {
#ifdef ESP32
portMUX_TYPE asyncStateMux = portMUX_INITIALIZER_UNLOCKED;
#endif

// Async network callbacks may run outside the main Homie.loop() flow. Keep the
// callback side limited to flags, counters, and bounded queues protected by this
// tiny critical section; all heavy work is consumed later from loop().
void enterAsyncStateCritical() {
#ifdef ESP32
  portENTER_CRITICAL(&asyncStateMux);
#elif defined(ESP8266)
  noInterrupts();
#endif
}

void exitAsyncStateCritical() {
#ifdef ESP32
  portEXIT_CRITICAL(&asyncStateMux);
#elif defined(ESP8266)
  interrupts();
#endif
}

class AsyncStateCriticalGuard {
 public:
  AsyncStateCriticalGuard() {
    enterAsyncStateCritical();
  }

  ~AsyncStateCriticalGuard() {
    exitAsyncStateCritical();
  }

  AsyncStateCriticalGuard(const AsyncStateCriticalGuard&) = delete;
  AsyncStateCriticalGuard& operator=(const AsyncStateCriticalGuard&) = delete;
};

bool takeFlag(volatile bool& flag) {
  AsyncStateCriticalGuard lock;
  const bool value = flag;
  flag = false;
  return value;
}

void setFlag(volatile bool& flag) {
  AsyncStateCriticalGuard lock;
  flag = true;
}

uint16_t takeAndResetCounter(volatile uint16_t& counter) {
  AsyncStateCriticalGuard lock;
  const uint16_t value = counter;
  counter = 0;
  return value;
}

void incrementCounter(volatile uint16_t& counter) {
  AsyncStateCriticalGuard lock;
  if (counter != 0xffffU) counter = counter + 1;
}

void incrementCounter(volatile uint32_t& counter) {
  AsyncStateCriticalGuard lock;
  if (counter != 0xffffffffUL) counter = counter + 1;
}

uint32_t readCounter(volatile uint32_t& counter) {
  AsyncStateCriticalGuard lock;
  return counter;
}

void incrementDropCounters(volatile uint16_t& intervalCounter, volatile uint32_t& totalCounter) {
  incrementCounter(intervalCounter);
  incrementCounter(totalCounter);
}

HomieEvent makeEvent(HomieEventType type) {
  HomieEvent event{};
  event.type = type;
  return event;
}

void dispatchEvent(const HomieEvent& event) {
  Interface::get().eventHandler(event);
}

bool isValidHomieId(const char* value) {
  if (!value || value[0] == '\0') return false;

  const size_t length = strlen(value);
  if (value[0] == '-' || value[length - 1] == '-') return false;

  for (size_t i = 0; i < length; i++) {
    const char c = value[i];
    const bool isLowerAlpha = c >= 'a' && c <= 'z';
    const bool isDigitChar = c >= '0' && c <= '9';
    if (!isLowerAlpha && !isDigitChar && c != '-') {
      return false;
    }
  }

  return true;
}

void warnIfInvalidHomieId(const __FlashStringHelper* scope, const char* value) {
  if (isValidHomieId(value)) return;

#if HOMIE_CONVENTION_V5
  Interface::get().getLogger() << F("✖ ") << scope << F(" \"") << value
                               << F("\" is not Homie v5 ID-compliant")
                               << endl;
  Helpers::abort(F("✖ Homie v5 requires lowercase topic IDs with digits and hyphens only"));
#else
  Interface::get().getLogger() << F("! ") << scope << F(" \"") << value
                               << F("\" is not Homie ID-compliant; keeping it for backward compatibility")
                               << endl;
#endif
}

void warnIfInvalidPropertyId(const char* nodeId, const char* propertyId) {
  if (isValidHomieId(propertyId)) return;

#if HOMIE_CONVENTION_V5
  Interface::get().getLogger() << F("✖ Property ID \"") << propertyId
                               << F("\" on node \"") << nodeId
                               << F("\" is not Homie v5 ID-compliant")
                               << endl;
  Helpers::abort(F("✖ Homie v5 requires lowercase property IDs with digits and hyphens only"));
#else
  Interface::get().getLogger() << F("! Property ID \"") << propertyId
                               << F("\" on node \"") << nodeId
                               << F("\" is not Homie ID-compliant; keeping it for backward compatibility")
                               << endl;
#endif
}

#if HOMIE_CONVENTION_V5
bool isValidHomieV5BaseTopic(const char* baseTopic) {
  size_t end = strlen(baseTopic);
  if (end == 0) return false;
  if (baseTopic[end - 1] == '/') end--;
  if (end == 0) return false;

  if (end >= 2 && baseTopic[end - 1] == '5' && baseTopic[end - 2] == '/') {
    end -= 2;
  }
  if (end == 0) return false;

  for (size_t i = 0; i < end; i++) {
    if (baseTopic[i] == '/') return false;
  }

  char domain[MAX_MQTT_BASE_TOPIC_LENGTH];
  if (end >= sizeof(domain)) return false;
  memcpy(domain, baseTopic, end);
  domain[end] = '\0';
  return isValidHomieId(domain);
}

bool isValidHomieV5Datatype(const char* datatype) {
  if (!datatype || datatype[0] == '\0') return false;

  return strcmp_P(datatype, PSTR("integer")) == 0
      || strcmp_P(datatype, PSTR("float")) == 0
      || strcmp_P(datatype, PSTR("boolean")) == 0
      || strcmp_P(datatype, PSTR("string")) == 0
      || strcmp_P(datatype, PSTR("enum")) == 0
      || strcmp_P(datatype, PSTR("color")) == 0
      || strcmp_P(datatype, PSTR("datetime")) == 0
      || strcmp_P(datatype, PSTR("duration")) == 0
      || strcmp_P(datatype, PSTR("json")) == 0;
}

bool homieV5DatatypeRequiresFormat(const char* datatype) {
  return strcmp_P(datatype, PSTR("enum")) == 0
      || strcmp_P(datatype, PSTR("color")) == 0;
}

bool homieV5DatatypeSupportsFormat(const char* datatype) {
  return strcmp_P(datatype, PSTR("integer")) == 0
      || strcmp_P(datatype, PSTR("float")) == 0
      || strcmp_P(datatype, PSTR("boolean")) == 0
      || strcmp_P(datatype, PSTR("enum")) == 0
      || strcmp_P(datatype, PSTR("color")) == 0
      || strcmp_P(datatype, PSTR("json")) == 0;
}

const char* resolveHomieV5Datatype(const char* datatype, const char* format) {
  if (!isValidHomieV5Datatype(datatype)) return HOMIE_DEFAULT_PROPERTY_DATATYPE;

  if (homieV5DatatypeRequiresFormat(datatype) && (!format || format[0] == '\0')) {
    // enum and color descriptions are invalid without a format. Advertising
    // them as string keeps the $description valid while preserving payloads.
    return HOMIE_DEFAULT_PROPERTY_DATATYPE;
  }

  return datatype;
}

bool shouldAdvertiseHomieV5Format(const char* datatype, const char* format) {
  return format && format[0] != '\0' && homieV5DatatypeSupportsFormat(datatype);
}

uint8_t decimalDigits(uint16_t value) {
  uint8_t digits = 1;
  while (value >= 10) {
    value /= 10;
    digits++;
  }
  return digits;
}

char jsonHexDigit(uint8_t value) {
  return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

void appendJsonString(String& output, const char* value) {
  output += '"';
  if (value) {
    for (const uint8_t* cursor = reinterpret_cast<const uint8_t*>(value); *cursor; cursor++) {
      const uint8_t c = *cursor;
      switch (c) {
        case '"':
          output.concat(F("\\\""));
          break;
        case '\\':
          output.concat(F("\\\\"));
          break;
        case '\b':
          output.concat(F("\\b"));
          break;
        case '\f':
          output.concat(F("\\f"));
          break;
        case '\n':
          output.concat(F("\\n"));
          break;
        case '\r':
          output.concat(F("\\r"));
          break;
        case '\t':
          output.concat(F("\\t"));
          break;
        default:
          if (c < 0x20) {
            output.concat(F("\\u00"));
            output += jsonHexDigit(c >> 4);
            output += jsonHexDigit(c & 0x0f);
          } else {
            output += static_cast<char>(c);
          }
          break;
      }
    }
  }
  output += '"';
}

void fnv1aUpdateByte(uint32_t& hash, uint8_t value) {
  hash ^= value;
  hash *= 16777619UL;
}

void fnv1aUpdateString(uint32_t& hash, const char* value) {
  if (value) {
    while (*value) {
      fnv1aUpdateByte(hash, static_cast<uint8_t>(*value++));
    }
  }
  fnv1aUpdateByte(hash, 0);
}

void fnv1aUpdateBool(uint32_t& hash, bool value) {
  fnv1aUpdateByte(hash, value ? 1 : 0);
}

void appendV5NodeId(String& output, const HomieNode* node, uint16_t rangeIndex, bool rangeNode) {
  output.concat(node->getId());
  if (rangeNode) {
    // Homie v5 topic IDs may contain hyphens but not underscores. Range nodes
    // therefore use "node-<index>" in v5 while v3/v4 keep the legacy "_<index>".
    output += '-';
    output.concat(rangeIndex);
  }
}

void appendV5NodeName(String& output, const HomieNode* node, uint16_t rangeIndex, bool rangeNode) {
  const char* name = node->getName();
  output.concat(name && name[0] != '\0' ? name : node->getId());
  if (rangeNode) {
    output += ' ';
    output.concat(rangeIndex);
  }
}

std::unique_ptr<char[]> buildV5AdvertisedSafeConfigFile(const char* safeConfigFile) {
  if (!safeConfigFile) return nullptr;

  const char* configuredBaseTopic = Interface::get().getConfig().get().mqtt.baseTopic;
  const size_t effectiveBaseTopicLength = Helpers::mqttRootTopicLength(configuredBaseTopic);
  std::unique_ptr<char[]> effectiveBaseTopic(new (std::nothrow) char[effectiveBaseTopicLength + 1]);
  if (!effectiveBaseTopic) return nullptr;
  Helpers::buildMqttRootTopic(effectiveBaseTopic.get(), configuredBaseTopic);

  // $implementation/config mirrors the saved config, but this diagnostic field
  // must describe the runtime MQTT root after the Homie v5 "/5/" segment has
  // been applied. Build a temporary JSON payload so config.json stays unchanged.
  const size_t capacity = MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE
                        + JSON_OBJECT_SIZE(1)
                        + strlen(safeConfigFile)
                        + effectiveBaseTopicLength
                        + 32;
  DynamicJsonDocument advertisedConfig(capacity);
  DeserializationError error = deserializeJson(advertisedConfig, safeConfigFile);
  if (error != DeserializationError::Ok || !advertisedConfig.is<JsonObject>()) {
    return nullptr;
  }

  JsonObject root = advertisedConfig.as<JsonObject>();
  JsonObject mqtt = root["mqtt"].as<JsonObject>();
  if (mqtt.isNull()) mqtt = root.createNestedObject("mqtt");
  mqtt["effective_base_topic"] = effectiveBaseTopic.get();
  if (advertisedConfig.overflowed()) return nullptr;

  const size_t advertisedConfigLength = measureJson(advertisedConfig) + 1;
  std::unique_ptr<char[]> advertisedConfigString(new (std::nothrow) char[advertisedConfigLength]);
  if (!advertisedConfigString) return nullptr;
  serializeJson(advertisedConfig, advertisedConfigString.get(), advertisedConfigLength);
  return advertisedConfigString;
}
#endif
}  // namespace

BootNormal::BootNormal()
  : Boot("normal")
  , _mqttReconnectTimer(MQTT_RECONNECT_INITIAL_INTERVAL, MQTT_RECONNECT_MAX_BACKOFF)
  , _wifiReconnectTimer(MQTT_RECONNECT_INITIAL_INTERVAL, MQTT_RECONNECT_MAX_BACKOFF)
  , _setupFunctionCalled(false)
  , _wifiGotIp(false)
  , _wifiConnectInProgress(false)
  , _mqttConnectInProgress(false)
  , _recoveryInProgress(false)
  , _hostnameConfigured(false)
  , _mdnsStarted(false)
  , _wifiConnectAttemptAt(0)
  , _mqttConnectAttemptAt(0)
  , _recoveryStartedAt(0)
  , _wifiEventPending(false)
  , _wifiDisconnectReasonPending(0)
  , _mqttEventPending(false)
  , _mqttDisconnectReasonPending(0)
  , _pendingMqttMessageReadIndex(0)
  , _pendingMqttMessageWriteIndex(0)
  , _pendingMqttMessageCount(0)
  , _pendingMqttMessagesDropped(0)
  , _pendingMqttMessagesDroppedTotal(0)
  , _pendingMqttMessageQueueLocked(false)
  , _pendingMqttAckReadIndex(0)
  , _pendingMqttAckWriteIndex(0)
  , _pendingMqttAckCount(0)
  , _pendingMqttAcksDropped(0)
  , _pendingMqttAcksDroppedTotal(0)
  , _pendingMqttAckQueueLocked(false)
  , _otaStartedPending(false)
  , _otaProgressPending(false)
  , _otaProgressSizeDone(0)
  , _otaProgressSizeTotal(0)
  , _otaSuccessfulPending(false)
  , _otaFailedPending(false)
  , _mqttConnectNotified(false)
  , _mqttDisconnectNotified(true)
  , _otaOngoing(false)
  , _flaggedForReboot(false)
  , _mqttOfflineMessageId(0)
  , _otaRequestedChecksum{0}
  , _otaIsBase64(false)
  , _otaBase64Pads(0)
  , _otaSizeTotal(0)
  , _otaSizeDone(0)
  , _otaPayloadTotal(0)
  , _otaPayloadProcessed(0)
  , _otaProgressPublishCounter(0)
  , _mqttTopic(nullptr)
  , _mqttClientId(nullptr)
  , _mqttWillTopic(nullptr)
  , _mqttPayloadBuffer(nullptr)
  , _mqttTopicLevels(nullptr)
  , _mqttTopicLevelsCount(0)
  , _mqttTopicCopy(nullptr) {
}

BootNormal::~BootNormal() {
}

void BootNormal::setup() {
  Boot::setup();

  strlcpy(_fwChecksum, ESP.getSketchMD5().c_str(), sizeof(_fwChecksum));
  _fwChecksum[sizeof(_fwChecksum) - 1] = '\0';

  #ifdef ESP32
  // ESP32 Update does not need the ESP8266 async-flash mode toggle.
  #elif defined(ESP8266)
  Update.runAsync(true);
  #endif // ESP32

  _statsTimer.setInterval(Interface::get().getConfig().get().deviceStatsInterval * 1000);

  if (Interface::get().led.enabled) Interface::get().getBlinker().start(LED_WIFI_DELAY);

#if HOMIE_CONVENTION_V5
  if (!isValidHomieV5BaseTopic(Interface::get().getConfig().get().mqtt.baseTopic)) {
    Interface::get().getLogger() << F("✖ mqtt.base_topic \"")
                                 << Interface::get().getConfig().get().mqtt.baseTopic
                                 << F("\" is not a valid Homie v5 root domain") << endl;
    Helpers::abort(F("✖ Homie v5 requires mqtt.base_topic '<domain>/' or '<domain>/5/'"));
  }
#endif

  // Generate topic buffer
  size_t baseTopicLength = Helpers::mqttDeviceBaseTopicLength(Interface::get().getConfig().get().mqtt.baseTopic, Interface::get().getConfig().get().deviceId);
  size_t longestSubtopicLength = 31 + 1;  // /$implementation/ota/firmware/+
  for (HomieNode* iNode : HomieNode::nodes) {
    size_t nodeMaxTopicLength = 1 + strlen(iNode->getId()) + 12 + 1;  // /id/$properties
    if (nodeMaxTopicLength > longestSubtopicLength) longestSubtopicLength = nodeMaxTopicLength;

    for (Property* iProperty : iNode->getProperties()) {
      size_t propertyMaxTopicLength = 1 + strlen(iNode->getId()) + 1 + strlen(iProperty->getId()) + 1;
      if (iProperty->isSettable()) propertyMaxTopicLength += 4;  // /set

      if (propertyMaxTopicLength > longestSubtopicLength) longestSubtopicLength = propertyMaxTopicLength;
    }
  }
  _mqttTopic = std::unique_ptr<char[]>(new (std::nothrow) char[baseTopicLength + longestSubtopicLength]);
  if (!_mqttTopic) Helpers::abort(F("✖ Cannot allocate MQTT topic buffer"));

  #ifdef ESP32
  _wifiGotIpHandler = WiFi.onEvent(std::bind(&BootNormal::_onWifiGotIp, this, std::placeholders::_1, std::placeholders::_2), WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  _wifiDisconnectedHandler = WiFi.onEvent(std::bind(&BootNormal::_onWifiDisconnected, this, std::placeholders::_1, std::placeholders::_2), WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  #elif defined(ESP8266)
  _wifiGotIpHandler = WiFi.onStationModeGotIP(std::bind(&BootNormal::_onWifiGotIp, this, std::placeholders::_1));
  _wifiDisconnectedHandler = WiFi.onStationModeDisconnected(std::bind(&BootNormal::_onWifiDisconnected, this, std::placeholders::_1));
  #endif // ESP32

  Interface::get().getMqttClient().onConnect(std::bind(&BootNormal::_onMqttConnected, this));
  Interface::get().getMqttClient().onDisconnect(std::bind(&BootNormal::_onMqttDisconnected, this, std::placeholders::_1));
  Interface::get().getMqttClient().onMessage(std::bind(&BootNormal::_onMqttMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
  Interface::get().getMqttClient().onPublish(std::bind(&BootNormal::_onMqttPublish, this, std::placeholders::_1));

  Interface::get().getMqttClient().setServer(Interface::get().getConfig().get().mqtt.server.host, Interface::get().getConfig().get().mqtt.server.port);

#if ASYNC_TCP_SSL_ENABLED
  Interface::get().getLogger() << "SSL is: " << Interface::get().getConfig().get().mqtt.server.ssl.enabled << endl;
  Interface::get().getMqttClient().setSecure(Interface::get().getConfig().get().mqtt.server.ssl.enabled);
  if (Interface::get().getConfig().get().mqtt.server.ssl.enabled && Interface::get().getConfig().get().mqtt.server.ssl.hasFingerprint) {
    char hexBuf[MAX_FINGERPRINT_STRING_LENGTH];
    Helpers::byteArrayToHexString(Interface::get().getConfig().get().mqtt.server.ssl.fingerprint, hexBuf, MAX_FINGERPRINT_SIZE);
    Interface::get().getLogger() << "Using fingerprint: " << hexBuf << endl;
    Interface::get().getMqttClient().addServerFingerprint((const uint8_t*)Interface::get().getConfig().get().mqtt.server.ssl.fingerprint);
  }
#endif

  Interface::get().getMqttClient().setMaxTopicLength(MAX_MQTT_TOPIC_LENGTH);
  _mqttClientId = std::unique_ptr<char[]>(new (std::nothrow) char[strlen(Interface::get().brand) + 1 + strlen(Interface::get().getConfig().get().deviceId) + 1]);
  if (!_mqttClientId) Helpers::abort(F("✖ Cannot allocate MQTT client id buffer"));
  strcpy(_mqttClientId.get(), Interface::get().brand);
  strcat_P(_mqttClientId.get(), PSTR("-"));
  strcat(_mqttClientId.get(), Interface::get().getConfig().get().deviceId);
  Interface::get().getMqttClient().setClientId(_mqttClientId.get());
  char* mqttWillTopic = _prefixMqttTopic(PSTR("/$state"));
  _mqttWillTopic = std::unique_ptr<char[]>(new (std::nothrow) char[strlen(mqttWillTopic) + 1]);
  if (!_mqttWillTopic) Helpers::abort(F("✖ Cannot allocate MQTT will topic buffer"));
  memcpy(_mqttWillTopic.get(), mqttWillTopic, strlen(mqttWillTopic) + 1);
  Interface::get().getMqttClient().setWill(_mqttWillTopic.get(), 1, true, "lost");

  if (Interface::get().getConfig().get().mqtt.auth) Interface::get().getMqttClient().setCredentials(Interface::get().getConfig().get().mqtt.username, Interface::get().getConfig().get().mqtt.password);

#if HOMIE_CONFIG
  ResetHandler::Attach();
#endif

  Interface::get().getConfig().log();
  warnIfInvalidHomieId(F("Device ID"), Interface::get().getConfig().get().deviceId);

  for (HomieNode* iNode : HomieNode::nodes) {
    warnIfInvalidHomieId(F("Node ID"), iNode->getId());
    iNode->setup();
    for (Property* iProperty : iNode->getProperties()) {
      warnIfInvalidPropertyId(iNode->getId(), iProperty->getId());
#if HOMIE_CONVENTION_V5
      const char* datatype = iProperty->getDatatype();
      if (!isValidHomieV5Datatype(datatype)) {
        Interface::get().getLogger() << F("! Property \"") << iProperty->getId()
                                     << F("\" on node \"") << iNode->getId()
                                     << F("\" has no valid Homie v5 datatype; $description will use string")
                                     << endl;
      } else if (homieV5DatatypeRequiresFormat(datatype)
                 && (!iProperty->getFormat() || iProperty->getFormat()[0] == '\0')) {
        Interface::get().getLogger() << F("! Property \"") << iProperty->getId()
                                     << F("\" on node \"") << iNode->getId()
                                     << F("\" uses Homie v5 datatype ") << datatype
                                     << F(" without the required format; $description will use string")
                                     << endl;
      }
#endif
    }
  }

  _resetAdvertisementProgress();
  _markConnectivityRecovering();
  _wifiReconnectTimer.activate();
}

void BootNormal::loop() {
  Boot::loop();

  _processPendingAsyncEvents();
  _processPendingEventNotifications();

  if (_flaggedForReboot && Interface::get().reset.idle) {
    Interface::get().getLogger() << F("Device is idle") << endl;

    Interface::get().getLogger() << F("↻ Rebooting...") << endl;
    Serial.flush();
    ESP.restart();
  }

  for (HomieNode* iNode : HomieNode::nodes) {
    if (iNode->runLoopDisconnected || Interface::get().ready) iNode->loop();
  }

  // Self-heal missed async events and stalled connect attempts before scheduling new work.
  _recoverIfNetworkStateDrifted();
  _recoverIfConnectAttemptStalled();

  if (_recoveryInProgress
      && !Interface::get().disable
      && !Interface::get().flaggedForSleep
      && !_otaOngoing
      && !_flaggedForReboot
      && (millis() - _recoveryStartedAt >= CONNECTIVITY_RECOVERY_REBOOT_TIMEOUT)) {
    _scheduleRecoveryReboot(F("connectivity recovery timed out"));
  }

  if (!_wifiConnectInProgress && _wifiReconnectTimer.check()) {
    _wifiConnect();
  }
  if (!_mqttConnectInProgress && _mqttReconnectTimer.check()) {
    _mqttConnect();
    return;
  }

  if (!Interface::get().getMqttClient().connected()) return;

  const uint16_t droppedMessages = takeAndResetCounter(_pendingMqttMessagesDropped);
  if (droppedMessages != 0) {
    Interface::get().getLogger() << F("✖ MQTT inbound queue full, dropped ") << droppedMessages << F(" message(s)") << endl;
  }

  _processPendingMqttMessages();

  // here, we are connected to the broker

  if (!_advertisementProgress.done) {
    _advertise();
    return;
  }

  if (_otaOngoing) return;
  // here, we finished the advertisement

  if (!_mqttConnectNotified) {
    _uptimeMqtt.reset();
    _markConnectivityHealthy();

    Interface::get().ready = true;
    if (Interface::get().led.enabled) Interface::get().getBlinker().stop();

    Interface::get().getLogger() << F("✔ MQTT ready") << endl;
    Interface::get().getLogger() << F("Triggering MQTT_READY event...") << endl;
    const HomieEvent event = makeEvent(HomieEventType::MQTT_READY);
    dispatchEvent(event);

    for (HomieNode* iNode : HomieNode::nodes) {
      iNode->onReadyToOperate();
    }

    if (!_setupFunctionCalled) {
      Interface::get().getLogger() << F("Calling setup function...") << endl;
      Interface::get().setupFunction();
      _setupFunctionCalled = true;
    }

    _mqttConnectNotified = true;
    return;
  }

  // here, we have notified the sketch we are ready

  if (_mqttOfflineMessageId == 0 && Interface::get().flaggedForSleep) {
    Interface::get().getLogger() << F("Device in preparation to sleep...") << endl;
    _mqttOfflineMessageId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$state")), 1, true, "sleeping");
  }

  if (_statsTimer.check()) {
    char statsIntervalStr[5 + 1];
    utoa(Interface::get().getConfig().get().deviceStatsInterval, statsIntervalStr, 10);
    Interface::get().getLogger() << F("〽 Sending statistics...") << endl;
    Interface::get().getLogger() << F("  • Interval: ") << statsIntervalStr << F("s") << endl;
    uint16_t intervalPacketId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats/interval")), 1, true, statsIntervalStr);

    uint8_t quality = Helpers::rssiToPercentage(WiFi.RSSI());
    char qualityStr[3 + 1];
    itoa(quality, qualityStr, 10);
    Interface::get().getLogger() << F("  • Wi-Fi signal quality: ") << qualityStr << F("%") << endl;
    uint16_t signalPacketId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats/signal")), 1, true, qualityStr);

    _uptime.update();
    _uptimeWifi.update();
    _uptimeMqtt.update();
    char statusStr[20 + 1];
    itoa(_uptime.getSeconds(), statusStr, 10);
    Interface::get().getLogger() << F("  • Uptime: ") << statusStr << F("s") << endl;
    uint16_t uptimePacketId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats/uptime")), 1, true, statusStr);

    itoa(_uptimeWifi.getSeconds(), statusStr, 10);
    uint16_t uptimeWifiPacketId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats/uptimewifi")), 1, true, statusStr);

    itoa(_uptimeMqtt.getSeconds(), statusStr, 10);
    uint16_t uptimeMqttPacketId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats/uptimemqtt")), 1, true, statusStr);

    uint32_t freeHeap = ESP.getFreeHeap();
    char freeHeapStr[20 + 1];
    utoa(freeHeap, freeHeapStr, 10);
    Interface::get().getLogger() << F("  • FreeHeap: ") << freeHeapStr << F("b") << endl;
    uint16_t freeHeapPacketId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats/freeheap")), 1, true, freeHeapStr);

    const uint32_t inboundDroppedTotal = readCounter(_pendingMqttMessagesDroppedTotal);
    const uint32_t ackDroppedTotal = readCounter(_pendingMqttAcksDroppedTotal);
    char droppedStr[10 + 1];
    utoa(inboundDroppedTotal, droppedStr, 10);
    uint16_t inboundDroppedPacketId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats/mqttinbounddropped")), 1, true, droppedStr);

    utoa(ackDroppedTotal, droppedStr, 10);
    uint16_t ackDroppedPacketId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats/mqttackdropped")), 1, true, droppedStr);

    if (inboundDroppedTotal != 0 || ackDroppedTotal != 0) {
      Interface::get().getLogger() << F("  • MQTT drops: inbound=") << inboundDroppedTotal
                                   << F(", ack=") << ackDroppedTotal << endl;
    }

    if (intervalPacketId != 0
        && signalPacketId != 0
        && uptimePacketId != 0
        && uptimeWifiPacketId != 0
        && uptimeMqttPacketId != 0
        && freeHeapPacketId != 0
        && inboundDroppedPacketId != 0
        && ackDroppedPacketId != 0) _statsTimer.tick();
    const HomieEvent event = makeEvent(HomieEventType::SENDING_STATISTICS);
    dispatchEvent(event);
  }

  Interface::get().loopFunction();
}

void BootNormal::_prefixMqttTopic() {
  Helpers::buildMqttDeviceBaseTopic(
    _mqttTopic.get(),
    Interface::get().getConfig().get().mqtt.baseTopic,
    Interface::get().getConfig().get().deviceId
  );
}

char* BootNormal::_prefixMqttTopic(PGM_P topic) {
  _prefixMqttTopic();
  strcat_P(_mqttTopic.get(), topic);

  return _mqttTopic.get();
}

bool BootNormal::_publishOtaStatus(int status, const char* info) {
  String payload(status);
  if (info) {
    payload.concat(F(" "));
    payload.concat(info);
  }

  const size_t topicLength = Helpers::mqttDeviceBaseTopicLength(
                               Interface::get().getConfig().get().mqtt.baseTopic,
                               Interface::get().getConfig().get().deviceId
                             )
                           + strlen_P(PSTR("/$implementation/ota/status"));
  std::unique_ptr<char[]> topic(new (std::nothrow) char[topicLength + 1]);
  if (!topic) return false;
  Helpers::buildMqttDeviceBaseTopic(
    topic.get(),
    Interface::get().getConfig().get().mqtt.baseTopic,
    Interface::get().getConfig().get().deviceId
  );
  strcat_P(topic.get(), PSTR("/$implementation/ota/status"));

  return Interface::get().getMqttClient().publish(
            topic.get(), 0, true, payload.c_str()) != 0;
}

void BootNormal::_resetOtaTransferState(bool preserveRequestedChecksum) {
  _otaOngoing = false;
  if (!preserveRequestedChecksum) _otaRequestedChecksum[0] = '\0';
  _otaIsBase64 = false;
  _otaBase64Pads = 0;
  _otaSizeTotal = 0;
  _otaSizeDone = 0;
  _otaPayloadTotal = 0;
  _otaPayloadProcessed = 0;
  _otaProgressPublishCounter = 0;
}

void BootNormal::_failOtaUpdate(int status, const char* info, const __FlashStringHelper* reason) {
  _publishOtaStatus(status, info);

  Interface::get().getLogger() << F("✖ OTA failed (") << status;
  if (info && info[0] != '\0') {
    Interface::get().getLogger() << F(" ") << info;
  }
  Interface::get().getLogger() << F(")") << endl;
  Interface::get().getLogger() << reason << endl;

  setFlag(_otaFailedPending);
  _resetOtaTransferState();
}

void BootNormal::_abortOtaUpdateOnDisconnect() {
  if (!_otaOngoing) return;

  Interface::get().getLogger() << F("✖ MQTT disconnected during OTA, aborting update") << endl;

  #ifdef ESP32
  if (Update.isRunning()) Update.abort();
  #elif defined(ESP8266)
  if (Update.isRunning()) Update.end(false);
  #endif // ESP32

  setFlag(_otaFailedPending);
  _resetOtaTransferState();
}

void BootNormal::_endOtaUpdate(bool success, uint8_t update_error) {
  if (success) {
    Interface::get().getLogger() << F("✔ OTA succeeded") << endl;
    setFlag(_otaSuccessfulPending);
    _publishOtaStatus(200);  // 200 OK
    _flaggedForReboot = true;
  } else {
    int code;
    String info;
    switch (update_error) {
      case UPDATE_ERROR_SIZE:               // new firmware size is zero
      case UPDATE_ERROR_MAGIC_BYTE:         // new firmware does not have 0xE9 in first byte
      #ifdef ESP8266
      case UPDATE_ERROR_NEW_FLASH_CONFIG:   // bad new flash config (does not match flash ID)
      #endif // ESP8266
        code = 400;  // 400 Bad Request
        info.concat(F("BAD_FIRMWARE"));
        break;
      case UPDATE_ERROR_MD5:
        code = 400;  // 400 Bad Request
        info.concat(F("BAD_CHECKSUM"));
        break;
      case UPDATE_ERROR_SPACE:
        code = 400;  // 400 Bad Request
        info.concat(F("NOT_ENOUGH_SPACE"));
        break;
      case UPDATE_ERROR_WRITE:
      case UPDATE_ERROR_ERASE:
      case UPDATE_ERROR_READ:
        code = 500;  // 500 Internal Server Error
        info.concat(F("FLASH_ERROR"));
        break;
      default:
        code = 500;  // 500 Internal Server Error
        info.concat(F("INTERNAL_ERROR "));
        info.concat(update_error);
        break;
    }
    _publishOtaStatus(code, info.c_str());

    Interface::get().getLogger() << F("✖ OTA failed (") << code << F(" ") << info << F(")") << endl;
    setFlag(_otaFailedPending);
  }
  _resetOtaTransferState(success);
}

void BootNormal::_markConnectivityRecovering() {
  if (_recoveryInProgress) return;

  _recoveryInProgress = true;
  _recoveryStartedAt = millis();
}

void BootNormal::_markConnectivityHealthy() {
  _recoveryInProgress = false;
  _recoveryStartedAt = 0;
}

void BootNormal::_scheduleRecoveryReboot(const __FlashStringHelper* reason) {
  if (_flaggedForReboot) return;

  Interface::get().getLogger() << F("✖ Recovery watchdog expired: ") << reason << endl;
  Interface::get().getLogger() << F("↻ Scheduling reboot to recover network stack...") << endl;
  Interface::get().disable = true;
  _flaggedForReboot = true;
}

bool BootNormal::_isWifiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void BootNormal::_processPendingAsyncEvents() {
  bool wifiEventPending = false;
  int32_t wifiDisconnectReason = 0;
  {
    // Snapshot and clear the async flag under lock, then run the recovery logic
    // outside the critical section so callbacks are never blocked by handlers.
    AsyncStateCriticalGuard lock;
    wifiEventPending = _wifiEventPending;
    if (wifiEventPending) {
      _wifiEventPending = false;
      wifiDisconnectReason = _wifiDisconnectReasonPending;
    }
  }

  if (wifiEventPending) {
    if (_isWifiConnected()) {
      _handleWifiConnected(WiFi.localIP(), WiFi.subnetMask(), WiFi.gatewayIP());
    } else {
      _handleWifiDisconnected(wifiDisconnectReason);
    }
  }

  bool mqttEventPending = false;
  int32_t mqttDisconnectReason = 0;
  {
    // MQTT callbacks follow the same pattern as Wi-Fi callbacks: tiny callback
    // work, deterministic processing from the main loop.
    AsyncStateCriticalGuard lock;
    mqttEventPending = _mqttEventPending;
    if (mqttEventPending) {
      _mqttEventPending = false;
      mqttDisconnectReason = _mqttDisconnectReasonPending;
    }
  }

  if (mqttEventPending) {
    if (Interface::get().getMqttClient().connected()) {
      _handleMqttConnected();
    } else {
      _handleMqttDisconnected(static_cast<AsyncMqttClientDisconnectReason>(mqttDisconnectReason));
    }
  }
}

void BootNormal::_processPendingEventNotifications() {
  // Homie events are dispatched from the main loop on this fork. That preserves
  // the public event API while avoiding user callbacks inside async MQTT/Wi-Fi
  // callback context.
  if (takeFlag(_otaStartedPending)) {
    Interface::get().getLogger() << F("Triggering OTA_STARTED event...") << endl;
    const HomieEvent event = makeEvent(HomieEventType::OTA_STARTED);
    dispatchEvent(event);
  }

  bool otaProgressPending = false;
  size_t otaProgressSizeDone = 0;
  size_t otaProgressSizeTotal = 0;
  {
    AsyncStateCriticalGuard lock;
    otaProgressPending = _otaProgressPending;
    if (otaProgressPending) {
      _otaProgressPending = false;
      otaProgressSizeDone = _otaProgressSizeDone;
      otaProgressSizeTotal = _otaProgressSizeTotal;
    }
  }

  if (otaProgressPending) {
    HomieEvent event = makeEvent(HomieEventType::OTA_PROGRESS);
    event.sizeDone = otaProgressSizeDone;
    event.sizeTotal = otaProgressSizeTotal;
    dispatchEvent(event);
  }

  if (takeFlag(_otaSuccessfulPending)) {
    Interface::get().getLogger() << F("Triggering OTA_SUCCESSFUL event...") << endl;
    const HomieEvent event = makeEvent(HomieEventType::OTA_SUCCESSFUL);
    dispatchEvent(event);
  }

  if (takeFlag(_otaFailedPending)) {
    Interface::get().getLogger() << F("Triggering OTA_FAILED event...") << endl;
    const HomieEvent event = makeEvent(HomieEventType::OTA_FAILED);
    dispatchEvent(event);
  }

  while (true) {
    _lockPendingMqttAckQueue();
    if (_pendingMqttAckCount == 0) {
      _unlockPendingMqttAckQueue();
      break;
    }

    const uint8_t readIndex = _pendingMqttAckReadIndex;
    const uint16_t id = _pendingMqttAckIds[readIndex];

    _pendingMqttAckReadIndex = (readIndex + 1) % PENDING_MQTT_ACK_QUEUE_SIZE;
    _pendingMqttAckCount = _pendingMqttAckCount - 1;
    _unlockPendingMqttAckQueue();

    HomieEvent event = makeEvent(HomieEventType::MQTT_PACKET_ACKNOWLEDGED);
    event.packetId = id;
    dispatchEvent(event);

    if (Interface::get().flaggedForSleep && id == _mqttOfflineMessageId) {
      Interface::get().getLogger() << F("Offline message acknowledged. Disconnecting MQTT...") << endl;
      Interface::get().getMqttClient().disconnect();
      break;
    }
  }

  const uint16_t droppedAcks = takeAndResetCounter(_pendingMqttAcksDropped);
  if (droppedAcks != 0) {
    Interface::get().getLogger() << F("✖ MQTT ACK queue full, dropped ") << droppedAcks << F(" acknowledgement event(s)") << endl;
  }
}

void BootNormal::_lockPendingMqttMessageQueue() {
  // The queue is shared with MQTT callbacks. The spin is normally one iteration;
  // delay(0) keeps the watchdog fed if the main loop and callback briefly race.
  while (true) {
    {
      AsyncStateCriticalGuard lock;
      if (!_pendingMqttMessageQueueLocked) {
        _pendingMqttMessageQueueLocked = true;
        return;
      }
    }
    delay(0);
  }
}

void BootNormal::_unlockPendingMqttMessageQueue() {
  AsyncStateCriticalGuard lock;
  _pendingMqttMessageQueueLocked = false;
}

void BootNormal::_lockPendingMqttAckQueue() {
  // Publish acknowledgements can arrive from async MQTT context while the main
  // loop is dispatching queued events, so the ACK ring uses the same tiny lock.
  while (true) {
    {
      AsyncStateCriticalGuard lock;
      if (!_pendingMqttAckQueueLocked) {
        _pendingMqttAckQueueLocked = true;
        return;
      }
    }
    delay(0);
  }
}

void BootNormal::_unlockPendingMqttAckQueue() {
  AsyncStateCriticalGuard lock;
  _pendingMqttAckQueueLocked = false;
}

bool BootNormal::_enqueuePendingMqttAck(uint16_t id) {
  // The callback path stores only the packet id. Event construction and user
  // dispatch happen later in _processPendingEventNotifications().
  _lockPendingMqttAckQueue();
  if (_pendingMqttAckCount >= PENDING_MQTT_ACK_QUEUE_SIZE) {
    _unlockPendingMqttAckQueue();
    return false;
  }

  const uint8_t writeIndex = _pendingMqttAckWriteIndex;
  _pendingMqttAckIds[writeIndex] = id;
  _pendingMqttAckWriteIndex = (writeIndex + 1) % PENDING_MQTT_ACK_QUEUE_SIZE;
  _pendingMqttAckCount = _pendingMqttAckCount + 1;
  _unlockPendingMqttAckQueue();
  return true;
}

bool BootNormal::_enqueuePendingMqttMessage(const char* topic, const char* payload, const AsyncMqttClientMessageProperties& properties) {
  // AsyncMqttClient owns topic/payload memory only for the callback duration.
  // Copy first, then enter the queue lock only for the ring-buffer mutation.
  const size_t topicLength = strlen(topic);
  std::unique_ptr<char[]> topicCopy(new (std::nothrow) char[topicLength + 1]);
  if (!topicCopy) return false;
  memcpy(topicCopy.get(), topic, topicLength + 1);

  const size_t payloadLength = strlen(payload);
  std::unique_ptr<char[]> payloadCopy(new (std::nothrow) char[payloadLength + 1]);
  if (!payloadCopy) return false;
  memcpy(payloadCopy.get(), payload, payloadLength + 1);

  // Allocate before taking the queue lock, then keep the critical section short.
  _lockPendingMqttMessageQueue();
  if (_pendingMqttMessageCount >= PENDING_MQTT_MESSAGE_QUEUE_SIZE) {
    _unlockPendingMqttMessageQueue();
    return false;
  }

  const uint8_t writeIndex = _pendingMqttMessageWriteIndex;
  PendingMqttMessage& slot = _pendingMqttMessages[writeIndex];
  slot.topic = std::move(topicCopy);
  slot.payload = std::move(payloadCopy);
  slot.properties = properties;
  _pendingMqttMessageWriteIndex = (writeIndex + 1) % PENDING_MQTT_MESSAGE_QUEUE_SIZE;
  _pendingMqttMessageCount = _pendingMqttMessageCount + 1;
  _unlockPendingMqttMessageQueue();
  return true;
}

void BootNormal::_flushPendingMqttMessages() {
  _lockPendingMqttMessageQueue();
  for (PendingMqttMessage& slot : _pendingMqttMessages) {
    slot.topic.reset();
    slot.payload.reset();
  }

  _pendingMqttMessageReadIndex = 0;
  _pendingMqttMessageWriteIndex = 0;
  _pendingMqttMessageCount = 0;
  _unlockPendingMqttMessageQueue();
}

void BootNormal::_handleQueuedMqttMessage(std::unique_ptr<char[]> topicCopy, std::unique_ptr<char[]> payloadBuffer, const AsyncMqttClientMessageProperties& properties) {
  std::unique_ptr<char*[]> topicLevels;
  uint8_t topicLevelsCount = 0;
  if (!__splitTopic(topicCopy.get(), topicLevels, topicLevelsCount)) {
    incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
    return;
  }
  if (topicLevelsCount == 0) return;

  const size_t payloadLength = strlen(payloadBuffer.get());

  if (__handleBroadcasts(topicCopy.get(), payloadBuffer.get(), properties, payloadLength, 0, payloadLength, topicLevels.get(), topicLevelsCount))
    return;

  if (strcmp(topicLevels.get()[0], Interface::get().getConfig().get().deviceId) != 0)
    return;

  if (__handleResets(topicCopy.get(), payloadBuffer.get(), properties, payloadLength, 0, payloadLength, topicLevels.get(), topicLevelsCount))
    return;

  if (__handleConfig(topicCopy.get(), payloadBuffer.get(), properties, payloadLength, 0, payloadLength, topicLevels.get(), topicLevelsCount))
    return;

  __handleNodeProperty(topicCopy.get(), payloadBuffer.get(), properties, payloadLength, 0, payloadLength, topicLevels.get(), topicLevelsCount);
}

void BootNormal::_processPendingMqttMessages() {
  while (_pendingMqttMessageCount > 0) {
    std::unique_ptr<char[]> topicCopy;
    std::unique_ptr<char[]> payloadBuffer;
    AsyncMqttClientMessageProperties properties{};

    _lockPendingMqttMessageQueue();
    if (_pendingMqttMessageCount == 0) {
      _unlockPendingMqttMessageQueue();
      return;
    }

    // Move the queued buffers out while locked, then parse and dispatch outside
    // the lock so application handlers cannot block async callback progress.
    const uint8_t readIndex = _pendingMqttMessageReadIndex;
    PendingMqttMessage& slot = _pendingMqttMessages[readIndex];
    _pendingMqttMessageReadIndex = (readIndex + 1) % PENDING_MQTT_MESSAGE_QUEUE_SIZE;
    _pendingMqttMessageCount = _pendingMqttMessageCount - 1;

    if (slot.topic && slot.payload) {
      topicCopy = std::move(slot.topic);
      payloadBuffer = std::move(slot.payload);
      properties = slot.properties;
    }
    _unlockPendingMqttMessageQueue();

    if (!topicCopy || !payloadBuffer) {
      continue;
    }

    _handleQueuedMqttMessage(std::move(topicCopy), std::move(payloadBuffer), properties);

    if (!Interface::get().getMqttClient().connected()) {
      _flushPendingMqttMessages();
      return;
    }
  }
}

void BootNormal::_recoverIfNetworkStateDrifted() {
  if (_isWifiConnected()) {
    if (!_wifiGotIp) {
      Interface::get().getLogger() << F("! Wi-Fi is connected but state was stale. Recovering...") << endl;
      _handleWifiConnected(WiFi.localIP(), WiFi.subnetMask(), WiFi.gatewayIP());
    }
  } else if (_wifiGotIp) {
    Interface::get().getLogger() << F("! Wi-Fi disconnect event was missed. Recovering...") << endl;
    _handleWifiDisconnected(0);
  }

  if (Interface::get().getMqttClient().connected()) {
    if (_mqttDisconnectNotified || _mqttConnectInProgress) {
      Interface::get().getLogger() << F("! MQTT is connected but state was stale. Recovering...") << endl;
      _handleMqttConnected();
    }
  } else if (!_mqttDisconnectNotified && _wifiGotIp) {
    Interface::get().getLogger() << F("! MQTT disconnect event was missed. Recovering...") << endl;
    _handleMqttDisconnected(AsyncMqttClientDisconnectReason::TCP_DISCONNECTED);
  }
}

void BootNormal::_recoverIfConnectAttemptStalled() {
  const uint32_t now = millis();

  if (_wifiConnectInProgress && !_isWifiConnected() && (now - _wifiConnectAttemptAt >= WIFI_CONNECT_ATTEMPT_TIMEOUT)) {
    Interface::get().getLogger() << F("✖ Wi-Fi connect attempt timed out. Forcing a fresh retry...") << endl;
    _wifiConnectInProgress = false;
    WiFi.disconnect();
    _wifiReconnectTimer.deactivate();
    _wifiReconnectTimer.activate();
  }

  if (_mqttConnectInProgress && !Interface::get().getMqttClient().connected() && (now - _mqttConnectAttemptAt >= MQTT_CONNECT_ATTEMPT_TIMEOUT)) {
    Interface::get().getLogger() << F("✖ MQTT connect attempt timed out. Forcing a fresh retry...") << endl;
    _mqttConnectInProgress = false;
    Interface::get().getMqttClient().disconnect(true);
    _handleMqttDisconnected(AsyncMqttClientDisconnectReason::TCP_DISCONNECTED);
    if (_wifiGotIp) {
      _mqttReconnectTimer.deactivate();
      _mqttReconnectTimer.activate();
    }
  }
}

void BootNormal::_handleWifiConnected(const IPAddress& ip, const IPAddress& mask, const IPAddress& gateway) {
  if (_wifiGotIp && !_wifiReconnectTimer.isActive()) return;

  _markConnectivityRecovering();
  _wifiGotIp = true;
  _wifiConnectInProgress = false;
  _wifiReconnectTimer.deactivate();
  _uptimeWifi.reset();

  if (!_mqttDisconnectNotified || _mqttConnectInProgress || Interface::get().getMqttClient().connected()) {
    Interface::get().getMqttClient().disconnect(true);
  }
  _handleMqttDisconnected(AsyncMqttClientDisconnectReason::TCP_DISCONNECTED);
  _mqttReconnectTimer.deactivate();

  if (Interface::get().led.enabled) Interface::get().getBlinker().stop();
  Interface::get().getLogger() << F("✔ Wi-Fi connected, IP: ") << ip << endl;
  Interface::get().getLogger() << F("Triggering WIFI_CONNECTED event...") << endl;
  HomieEvent event = makeEvent(HomieEventType::WIFI_CONNECTED);
  event.ip = ip;
  event.mask = mask;
  event.gateway = gateway;
  dispatchEvent(event);
#if HOMIE_MDNS
  if (!_mdnsStarted && MDNS.begin(Interface::get().getConfig().get().deviceId)) {
    _mdnsStarted = true;
  }
#endif

  _mqttReconnectTimer.activate();
}

void BootNormal::_handleWifiDisconnected(int32_t reason) {
  if (!_wifiGotIp && !_wifiConnectInProgress && _wifiReconnectTimer.isActive()) return;

  _markConnectivityRecovering();
  _wifiGotIp = false;
  _wifiConnectInProgress = false;
#if HOMIE_MDNS
  if (_mdnsStarted) {
    MDNS.end();
    _mdnsStarted = false;
  }
#endif
  Interface::get().getMqttClient().disconnect(true);
  _handleMqttDisconnected(AsyncMqttClientDisconnectReason::TCP_DISCONNECTED);
  _mqttReconnectTimer.deactivate();

  if (Interface::get().led.enabled) Interface::get().getBlinker().start(LED_WIFI_DELAY);
  Interface::get().getLogger() << F("✖ Wi-Fi disconnected, reason: ") << reason << endl;
  Interface::get().getLogger() << F("Triggering WIFI_DISCONNECTED event...") << endl;
  HomieEvent event = makeEvent(HomieEventType::WIFI_DISCONNECTED);
#ifdef ESP32
  event.wifiReason = static_cast<uint8_t>(reason);
#elif defined(ESP8266)
  event.wifiReason = static_cast<WiFiDisconnectReason>(reason);
#endif
  dispatchEvent(event);

  _wifiReconnectTimer.activate();
}

void BootNormal::_wifiConnect() {
  if (Interface::get().disable || _wifiGotIp || _wifiConnectInProgress) return;
  if (_isWifiConnected()) {
    _handleWifiConnected(WiFi.localIP(), WiFi.subnetMask(), WiFi.gatewayIP());
    return;
  }

  _markConnectivityRecovering();
  _wifiConnectInProgress = true;
  _wifiConnectAttemptAt = millis();

  if (Interface::get().led.enabled) Interface::get().getBlinker().start(LED_WIFI_DELAY);
  Interface::get().getLogger() << F("↕ Attempting to connect to Wi-Fi...") << endl;

  #ifdef ESP32
  if (!_hostnameConfigured) {
    WiFi.setHostname(Interface::get().getConfig().get().deviceId);
    _hostnameConfigured = true;
  }
  #endif // ESP32

  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);

  #ifdef ESP8266
  WiFi.hostname(Interface::get().getConfig().get().deviceId);
  #endif // ESP32
  if (strcmp_P(Interface::get().getConfig().get().wifi.ip, PSTR("")) != 0) {  // on _validateConfigWifi there is a requirement for mask and gateway
    IPAddress convertedIp;
    convertedIp.fromString(Interface::get().getConfig().get().wifi.ip);
    IPAddress convertedMask;
    convertedMask.fromString(Interface::get().getConfig().get().wifi.mask);
    IPAddress convertedGateway;
    convertedGateway.fromString(Interface::get().getConfig().get().wifi.gw);

    if (strcmp_P(Interface::get().getConfig().get().wifi.dns1, PSTR("")) != 0) {
      IPAddress convertedDns1;
      convertedDns1.fromString(Interface::get().getConfig().get().wifi.dns1);
      if ((strcmp_P(Interface::get().getConfig().get().wifi.dns2, PSTR("")) != 0)) {  // on _validateConfigWifi there is requirement that we need dns1 if we want to define dns2
        IPAddress convertedDns2;
        convertedDns2.fromString(Interface::get().getConfig().get().wifi.dns2);
        WiFi.config(convertedIp, convertedGateway, convertedMask, convertedDns1, convertedDns2);
      } else {
        WiFi.config(convertedIp, convertedGateway, convertedMask, convertedDns1);
      }
    } else {
      WiFi.config(convertedIp, convertedGateway, convertedMask);
    }
  }

  if (strcmp_P(Interface::get().getConfig().get().wifi.bssid, PSTR("")) != 0) {
    byte bssidBytes[6];
    Helpers::stringToBytes(Interface::get().getConfig().get().wifi.bssid, ':', bssidBytes, 6, 16);
    WiFi.begin(Interface::get().getConfig().get().wifi.ssid, Interface::get().getConfig().get().wifi.password, Interface::get().getConfig().get().wifi.channel, bssidBytes);
  } else {
    WiFi.begin(Interface::get().getConfig().get().wifi.ssid, Interface::get().getConfig().get().wifi.password);
  }

  #ifdef ESP32
  WiFi.setAutoReconnect(false);
  #elif defined(ESP8266)
  WiFi.setAutoReconnect(false);
  #endif // ESP32
}

#ifdef ESP32
void BootNormal::_onWifiGotIp(WiFiEvent_t event, WiFiEventInfo_t info) {
  (void)event;
  (void)info;
  setFlag(_wifiEventPending);
}
#elif defined(ESP8266)
void BootNormal::_onWifiGotIp(const WiFiEventStationModeGotIP& event) {
  (void)event;
  setFlag(_wifiEventPending);
}
#endif // ESP32

#ifdef ESP32
void BootNormal::_onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  (void)event;
  AsyncStateCriticalGuard lock;
  _wifiDisconnectReasonPending = static_cast<int32_t>(info.wifi_sta_disconnected.reason);
  _wifiEventPending = true;
}
#elif defined(ESP8266)
void BootNormal::_onWifiDisconnected(const WiFiEventStationModeDisconnected& event) {
  AsyncStateCriticalGuard lock;
  _wifiDisconnectReasonPending = static_cast<int32_t>(event.reason);
  _wifiEventPending = true;
}
#endif // ESP32

void BootNormal::_mqttConnect() {
  if (!_wifiGotIp || Interface::get().disable || _mqttConnectInProgress) return;
  if (Interface::get().getMqttClient().connected()) {
    _handleMqttConnected();
    return;
  }

  _markConnectivityRecovering();
  _mqttConnectInProgress = true;
  _mqttConnectAttemptAt = millis();

  if (Interface::get().led.enabled) Interface::get().getBlinker().start(LED_MQTT_DELAY);
  _mqttConnectNotified = false;
  Interface::get().getLogger() << F("↕ Attempting to connect to MQTT...") << endl;
  Interface::get().getMqttClient().connect();
}

void BootNormal::_resetAdvertisementProgress() {
  _advertisementProgress.done = false;
  _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_INIT;
  _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_NAME;
  _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_NAME;
  _advertisementProgress.currentNodeIndex = 0;
  _advertisementProgress.currentArrayNodeIndex = 0;
  _advertisementProgress.currentPropertyIndex = 0;
}

size_t BootNormal::_estimateV5DescriptionLength() const {
#if HOMIE_CONVENTION_V5
  const auto& config = Interface::get().getConfig().get();
  const char* deviceName = config.name[0] != '\0' ? config.name : config.deviceId;
  size_t length = 160 + strlen(HOMIE_VERSION) + strlen(deviceName) + strlen(HOMIE_V5_RUNTIME_EXTENSION);

  for (HomieNode* node : HomieNode::nodes) {
    const uint16_t firstIndex = node->isRange() ? node->getLower() : 0;
    const uint16_t lastIndex = node->isRange() ? node->getUpper() : 0;
    for (uint16_t rangeIndex = firstIndex; rangeIndex <= lastIndex; rangeIndex++) {
      const bool rangeNode = node->isRange();
      const char* nodeName = node->getName() ? node->getName() : "";
      const char* nodeType = node->getType() ? node->getType() : "";
      length += 96 + strlen(node->getId()) + strlen(nodeName) + strlen(nodeType);
      if (rangeNode) length += 2 + decimalDigits(rangeIndex);  // "-<index>" and friendly-name suffix.

      for (Property* property : node->getProperties()) {
        const char* propertyName = property->getName();
        const char* unit = property->getUnit() ? property->getUnit() : "";
        const char* propertyFormat = property->getFormat();
        const char* datatype = resolveHomieV5Datatype(property->getDatatype(), propertyFormat);
        const char* format = shouldAdvertiseHomieV5Format(datatype, propertyFormat)
                           ? propertyFormat
                           : "";
        length += 112 + strlen(property->getId())
                    + strlen(propertyName && propertyName[0] != '\0' ? propertyName : property->getId())
                    + strlen(datatype)
                    + strlen(unit)
                    + strlen(format);
      }

      if (!rangeNode || rangeIndex == lastIndex) break;
    }
  }

  return length;
#else
  return 0;
#endif
}

uint32_t BootNormal::_computeV5DescriptionVersion() const {
#if HOMIE_CONVENTION_V5
  const auto& config = Interface::get().getConfig().get();
  const char* deviceName = config.name[0] != '\0' ? config.name : config.deviceId;
  uint32_t hash = 2166136261UL;

  fnv1aUpdateString(hash, HOMIE_VERSION);
  fnv1aUpdateString(hash, deviceName);
  fnv1aUpdateString(hash, HOMIE_V5_RUNTIME_EXTENSION);

  for (HomieNode* node : HomieNode::nodes) {
    const uint16_t firstIndex = node->isRange() ? node->getLower() : 0;
    const uint16_t lastIndex = node->isRange() ? node->getUpper() : 0;
    for (uint16_t rangeIndex = firstIndex; rangeIndex <= lastIndex; rangeIndex++) {
      const bool rangeNode = node->isRange();
      String nodeId;
      String nodeName;
      appendV5NodeId(nodeId, node, rangeIndex, rangeNode);
      appendV5NodeName(nodeName, node, rangeIndex, rangeNode);

      fnv1aUpdateString(hash, nodeId.c_str());
      fnv1aUpdateString(hash, nodeName.c_str());
      fnv1aUpdateString(hash, node->getType() ? node->getType() : "");

      for (Property* property : node->getProperties()) {
        const char* propertyName = property->getName();
        const char* unit = property->getUnit() ? property->getUnit() : "";
        const char* propertyFormat = property->getFormat();
        const char* datatype = resolveHomieV5Datatype(property->getDatatype(), propertyFormat);
        const char* format = shouldAdvertiseHomieV5Format(datatype, propertyFormat)
                           ? propertyFormat
                           : "";
        fnv1aUpdateString(hash, property->getId());
        fnv1aUpdateString(hash, propertyName && propertyName[0] != '\0' ? propertyName : property->getId());
        fnv1aUpdateString(hash, datatype);
        fnv1aUpdateBool(hash, property->isSettable());
        fnv1aUpdateBool(hash, property->isRetained());
        fnv1aUpdateString(hash, unit);
        fnv1aUpdateString(hash, format);
      }

      if (!rangeNode || rangeIndex == lastIndex) break;
    }
  }

  return hash == 0 ? 1 : hash;
#else
  return 0;
#endif
}

uint16_t BootNormal::_publishV5Description() {
#if HOMIE_CONVENTION_V5
  const auto& config = Interface::get().getConfig().get();
  const char* deviceName = config.name[0] != '\0' ? config.name : config.deviceId;

  String description;
  if (!description.reserve(_estimateV5DescriptionLength())) {
    Interface::get().getLogger() << F("✖ Cannot allocate Homie v5 $description buffer") << endl;
    return 0;
  }

  description.concat(F("{\"homie\":"));
  appendJsonString(description, HOMIE_VERSION);
  description.concat(F(",\"name\":"));
  appendJsonString(description, deviceName);
  description.concat(F(",\"version\":"));
  description.concat(static_cast<unsigned long>(_computeV5DescriptionVersion()));
  description.concat(F(",\"extensions\":["));
  appendJsonString(description, HOMIE_V5_RUNTIME_EXTENSION);
  description.concat(F("],\"nodes\":{"));

  bool firstNode = true;
  for (HomieNode* node : HomieNode::nodes) {
    const uint16_t firstIndex = node->isRange() ? node->getLower() : 0;
    const uint16_t lastIndex = node->isRange() ? node->getUpper() : 0;
    for (uint16_t rangeIndex = firstIndex; rangeIndex <= lastIndex; rangeIndex++) {
      const bool rangeNode = node->isRange();
      String nodeId;
      String nodeName;
      appendV5NodeId(nodeId, node, rangeIndex, rangeNode);
      appendV5NodeName(nodeName, node, rangeIndex, rangeNode);

      if (!firstNode) description += ',';
      firstNode = false;
      appendJsonString(description, nodeId.c_str());
      description.concat(F(":{\"name\":"));
      appendJsonString(description, nodeName.c_str());
      if (node->getType() && node->getType()[0] != '\0') {
        description.concat(F(",\"type\":"));
        appendJsonString(description, node->getType());
      }
      description.concat(F(",\"properties\":{"));

      bool firstProperty = true;
      for (Property* property : node->getProperties()) {
        const char* propertyName = property->getName();
        const char* propertyFormat = property->getFormat();
        const char* datatype = resolveHomieV5Datatype(property->getDatatype(), propertyFormat);
        const char* format = shouldAdvertiseHomieV5Format(datatype, propertyFormat)
                           ? propertyFormat
                           : nullptr;

        if (!firstProperty) description += ',';
        firstProperty = false;
        appendJsonString(description, property->getId());
        description.concat(F(":{\"name\":"));
        appendJsonString(description, propertyName && propertyName[0] != '\0' ? propertyName : property->getId());
        description.concat(F(",\"datatype\":"));
        appendJsonString(description, datatype);
        description.concat(F(",\"settable\":"));
        description.concat(property->isSettable() ? F("true") : F("false"));
        description.concat(F(",\"retained\":"));
        description.concat(property->isRetained() ? F("true") : F("false"));
        if (property->getUnit() && property->getUnit()[0] != '\0') {
          description.concat(F(",\"unit\":"));
          appendJsonString(description, property->getUnit());
        }
        if (format) {
          description.concat(F(",\"format\":"));
          appendJsonString(description, format);
        }
        description += '}';
      }

      description.concat(F("}}"));
      if (!rangeNode || rangeIndex == lastIndex) break;
    }
  }

  description.concat(F("}}"));
  return Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$description")), 1, true, description.c_str());
#else
  return 0;
#endif
}

void BootNormal::_handleMqttConnected() {
  if (!_mqttDisconnectNotified && !_mqttConnectInProgress) return;

  _mqttConnectInProgress = false;
  _mqttDisconnectNotified = false;
  _mqttReconnectTimer.deactivate();
  _statsTimer.activate();

  Update.end();

  Interface::get().getLogger() << F("Sending initial information...") << endl;

  _advertise();
}

void BootNormal::_handleMqttDisconnected(AsyncMqttClientDisconnectReason reason) {
  _abortOtaUpdateOnDisconnect();

  Interface::get().ready = false;
  _mqttConnectNotified = false;
  _mqttConnectInProgress = false;
  _resetAdvertisementProgress();
  _flushPendingMqttMessages();
  _statsTimer.deactivate();

  if (!_mqttDisconnectNotified) {
    Interface::get().getLogger() << F("✖ MQTT disconnected, reason: ") << (int8_t)reason << endl;
    Interface::get().getLogger() << F("Triggering MQTT_DISCONNECTED event...") << endl;
    HomieEvent event = makeEvent(HomieEventType::MQTT_DISCONNECTED);
    event.mqttReason = reason;
    dispatchEvent(event);

    _mqttDisconnectNotified = true;

    if (Interface::get().flaggedForSleep) {
      _mqttOfflineMessageId = 0;
      Interface::get().getLogger() << F("Triggering READY_TO_SLEEP event...") << endl;
      const HomieEvent sleepEvent = makeEvent(HomieEventType::READY_TO_SLEEP);
      dispatchEvent(sleepEvent);

      return;
    }
  }

  _markConnectivityRecovering();
  if (_wifiGotIp) {
    _mqttReconnectTimer.activate();
  } else {
    _mqttReconnectTimer.deactivate();
  }
}

void BootNormal::_advertise() {
  uint16_t packetId;
  switch (_advertisementProgress.globalStep) {
    case AdvertisementProgress::GlobalStep::PUB_INIT:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$state")), 1, true, "init");
      if (packetId != 0) {
#if HOMIE_CONVENTION_V5
        _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_DESCRIPTION;
#else
        _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_HOMIE;
#endif
      }
      break;
    case AdvertisementProgress::GlobalStep::PUB_DESCRIPTION:
#if HOMIE_CONVENTION_V5
      packetId = _publishV5Description();
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_MAC;
#else
      _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_HOMIE;
#endif
      break;
    case AdvertisementProgress::GlobalStep::PUB_HOMIE:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$homie")), 1, true, HOMIE_VERSION);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_NAME;
      break;
    case AdvertisementProgress::GlobalStep::PUB_NAME:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$name")), 1, true, Interface::get().getConfig().get().name);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_MAC;
      break;
    case AdvertisementProgress::GlobalStep::PUB_MAC:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$mac")), 1, true, WiFi.macAddress().c_str());
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_LOCALIP;
      break;
    case AdvertisementProgress::GlobalStep::PUB_LOCALIP:
    {
      IPAddress localIp = WiFi.localIP();
      char localIpStr[MAX_IP_STRING_LENGTH];
      Helpers::ipToString(localIp, localIpStr);
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$localip")), 1, true, localIpStr);
      if (packetId != 0) {
#if HOMIE_CONVENTION_V5
        _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_STATS;
#else
        _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_NODES_ATTR;
#endif
      }
      break;
    }
    case AdvertisementProgress::GlobalStep::PUB_NODES_ATTR:
    {
      String nodes;
      for (HomieNode* node : HomieNode::nodes) {
        nodes.concat(node->getId());
        if (node->isRange())
          nodes.concat(F("[]"));
        nodes.concat(F(","));
      }
      if (HomieNode::nodes.size() >= 1) nodes.remove(nodes.length() - 1);
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$nodes")), 1, true, nodes.c_str());
#if HOMIE_CONVENTION_V4
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_EXTENSIONS;
#else
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_STATS;
#endif
      break;
    }
    case AdvertisementProgress::GlobalStep::PUB_EXTENSIONS:
#if HOMIE_CONVENTION_V4
      // Homie 4.0.0 makes $extensions mandatory. The official legacy
      // extensions document the firmware and stats attributes that this library
      // keeps for backward compatibility with Homie 3.0.1 consumers.
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$extensions")), 1, true, HOMIE_EXTENSIONS);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_STATS;
#else
      _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_STATS;
#endif
      break;
    case AdvertisementProgress::GlobalStep::PUB_STATS:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats")), 1, true, "signal,uptime,uptimewifi,uptimemqtt,freeheap,mqttinbounddropped,mqttackdropped");
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_STATS_INTERVAL;
      break;
    case AdvertisementProgress::GlobalStep::PUB_STATS_INTERVAL:
      char statsIntervalStr[5 + 1];
      utoa(Interface::get().getConfig().get().deviceStatsInterval, statsIntervalStr, 10);
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$stats/interval")), 1, true, statsIntervalStr);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_FW_NAME;
      break;
    case AdvertisementProgress::GlobalStep::PUB_FW_NAME:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$fw/name")), 1, true, Interface::get().firmware.name);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_FW_VERSION;
      break;
    case AdvertisementProgress::GlobalStep::PUB_FW_VERSION:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$fw/version")), 1, true, Interface::get().firmware.version);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_FW_CHECKSUM;
      break;
    case AdvertisementProgress::GlobalStep::PUB_FW_CHECKSUM:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$fw/checksum")), 1, true, _fwChecksum);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_IMPLEMENTATION;
      break;
    case AdvertisementProgress::GlobalStep::PUB_IMPLEMENTATION:
      #ifdef ESP32
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$implementation")), 1, true, "esp32");
      #elif defined(ESP8266)
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$implementation")), 1, true, "esp8266");
      #endif // ESP32
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_IMPLEMENTATION_CONFIG;
      break;
    case AdvertisementProgress::GlobalStep::PUB_IMPLEMENTATION_CONFIG:
    {
      char* safeConfigFile = Interface::get().getConfig().getSafeConfigFile();
#if HOMIE_CONVENTION_V5
      std::unique_ptr<char[]> advertisedSafeConfigFile = buildV5AdvertisedSafeConfigFile(safeConfigFile);
      const char* safeConfigPayload = advertisedSafeConfigFile ? advertisedSafeConfigFile.get() : safeConfigFile;
#else
      const char* safeConfigPayload = safeConfigFile;
#endif
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$implementation/config")), 1, true, safeConfigPayload);
      delete[] safeConfigFile;
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_IMPLEMENTATION_VERSION;
      break;
    }
    case AdvertisementProgress::GlobalStep::PUB_IMPLEMENTATION_VERSION:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$implementation/version")), 1, true, HOMIE_ESP8266_VERSION);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_IMPLEMENTATION_OTA_ENABLED;
      break;
    case AdvertisementProgress::GlobalStep::PUB_IMPLEMENTATION_OTA_ENABLED:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$implementation/ota/enabled")), 1, true, Interface::get().getConfig().get().ota.enabled ? "true" : "false");
      if (packetId != 0) {
#if HOMIE_CONVENTION_V5
        _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::SUB_IMPLEMENTATION_OTA;
#else
        if (HomieNode::nodes.size()) {  // skip if no nodes to publish
          _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_NODES;
          _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_NAME;
          _advertisementProgress.currentNodeIndex = 0;
        } else {
          _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::SUB_IMPLEMENTATION_OTA;
        }
#endif
      }
      break;
    case AdvertisementProgress::GlobalStep::PUB_NODES:
    {
      HomieNode* node = HomieNode::nodes[_advertisementProgress.currentNodeIndex];
      std::unique_ptr<char[]> subtopic = std::unique_ptr<char[]>(new (std::nothrow) char[1 + strlen(node->getId()) + 12 + 1]);  // /id/$properties
      if (!subtopic) return;
      switch (_advertisementProgress.nodeStep) {
        case AdvertisementProgress::NodeStep::PUB_NAME:
          strcpy_P(subtopic.get(), PSTR("/"));
          strcat(subtopic.get(), node->getId());
          strcat_P(subtopic.get(), PSTR("/$name"));
          packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, node->getName());
          if (packetId != 0) _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_TYPE;
          break;
        case AdvertisementProgress::NodeStep::PUB_TYPE:
          strcpy_P(subtopic.get(), PSTR("/"));
          strcat(subtopic.get(), node->getId());
          strcat_P(subtopic.get(), PSTR("/$type"));
          packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, node->getType());
          if (packetId != 0) _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_ARRAY;
          break;
        case AdvertisementProgress::NodeStep::PUB_ARRAY:
        {
          if (!node->isRange()) {
            _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_PROPERTIES;
            break;
          }
          strcpy_P(subtopic.get(), PSTR("/"));
          strcat(subtopic.get(), node->getId());
          strcat_P(subtopic.get(), PSTR("/$array"));
          String arrayInfo;
          arrayInfo.concat(node->getLower());
          arrayInfo.concat("-");
          arrayInfo.concat(node->getUpper());

          packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, arrayInfo.c_str());
          if (packetId != 0) {
            _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_ARRAY_NODES;
            _advertisementProgress.currentArrayNodeIndex = node->getLower();
          }
          break;
        }
        case AdvertisementProgress::NodeStep::PUB_ARRAY_NODES:
        {
          String id;
          id.concat(node->getId());
          id.concat("_");
          id.concat(_advertisementProgress.currentArrayNodeIndex);
          strcpy_P(subtopic.get(), PSTR("/"));
          strcat(subtopic.get(), id.c_str());
          strcat_P(subtopic.get(), PSTR("/$name"));
          packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, id.c_str());
          if (packetId != 0) {
            if (_advertisementProgress.currentArrayNodeIndex < node->getUpper()) {
              _advertisementProgress.currentArrayNodeIndex++;
            } else {
              _advertisementProgress.currentArrayNodeIndex = node->getLower();
              _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_PROPERTIES;
            }
          }
          break;
        }
        case AdvertisementProgress::NodeStep::PUB_PROPERTIES:
        {
          strcpy_P(subtopic.get(), PSTR("/"));
          strcat(subtopic.get(), node->getId());
          strcat_P(subtopic.get(), PSTR("/$properties"));
          String properties;
          for (Property* iProperty : node->getProperties()) {
            properties.concat(iProperty->getId());
            properties.concat(",");
          }
          if (node->getProperties().size() >= 1) properties.remove(properties.length() - 1);
          packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, properties.c_str());
          if (packetId != 0) {
            if (node->getProperties().size()) {
              // There are properties of the node to be advertised
              _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_PROPERTIES_ATTRIBUTES;
              _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_NAME;
              _advertisementProgress.currentPropertyIndex = 0;
            } else {
              // No properties of the node to be advertised
              if (_advertisementProgress.currentNodeIndex < HomieNode::nodes.size() - 1) {
                // There are nodes to be advertised
                _advertisementProgress.currentNodeIndex++;
                _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_NAME;
                _advertisementProgress.currentPropertyIndex = 0;
                _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_NAME;
              } else {
                // All nodes have been advertised
                _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::SUB_IMPLEMENTATION_OTA;
              }
            }
          }
          break;
        }
        case AdvertisementProgress::NodeStep::PUB_PROPERTIES_ATTRIBUTES:
        {
          HomieNode* node = HomieNode::nodes[_advertisementProgress.currentNodeIndex];
          Property* iProperty = node->getProperties()[_advertisementProgress.currentPropertyIndex];
          std::unique_ptr<char[]> subtopic = std::unique_ptr<char[]>(new (std::nothrow) char[1 + strlen(node->getId()) + 1 + strlen(iProperty->getId()) + 10 + 1]);  // /nodeId/propId/$settable
          if (!subtopic) return;
          switch (_advertisementProgress.propertyStep) {
            case AdvertisementProgress::PropertyStep::PUB_NAME:
            {
              const char* propertyName = iProperty->getName();
#if HOMIE_CONVENTION_V4
              // Homie 4.0.0 requires every property to advertise $name. Older
              // sketches did not have to call setName(), so v4 mode falls back
              // to the property id instead of making the sketch fail at boot.
              if (!propertyName || propertyName[0] == '\0') propertyName = iProperty->getId();
#endif
              if (propertyName && (propertyName[0] != '\0')) {
                strcpy_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), node->getId());
                strcat_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), iProperty->getId());
                strcat_P(subtopic.get(), PSTR("/$name"));
                packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, propertyName);
                if (packetId != 0) _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_SETTABLE;
              } else {
                _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_SETTABLE;
              }
              break;
            }
            case AdvertisementProgress::PropertyStep::PUB_SETTABLE:
              if (iProperty->isSettable()) {
                strcpy_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), node->getId());
                strcat_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), iProperty->getId());
                strcat_P(subtopic.get(), PSTR("/$settable"));
                packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, "true");
                if (packetId != 0) _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_RETAINED;
              } else {
                _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_RETAINED;
              }
              break;
            case AdvertisementProgress::PropertyStep::PUB_RETAINED:
              if (!iProperty->isRetained()) {
                strcpy_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), node->getId());
                strcat_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), iProperty->getId());
                strcat_P(subtopic.get(), PSTR("/$retained"));
                packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, "false");
                if (packetId != 0) _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_DATATYPE;
              } else {
                _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_DATATYPE;
              }
              break;
            case AdvertisementProgress::PropertyStep::PUB_DATATYPE:
            {
              const char* datatype = iProperty->getDatatype();
#if HOMIE_CONVENTION_V4
              // Homie 4.0.0 requires $datatype. The conservative fallback is
              // string because it is the least restrictive Homie payload type.
              if (!datatype || datatype[0] == '\0') datatype = HOMIE_DEFAULT_PROPERTY_DATATYPE;
#endif
              if (datatype && (datatype[0] != '\0')) {
                strcpy_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), node->getId());
                strcat_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), iProperty->getId());
                strcat_P(subtopic.get(), PSTR("/$datatype"));
                packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, datatype);
                if (packetId != 0) _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_UNIT;
              } else {
                _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_UNIT;
              }
              break;
            }
            case AdvertisementProgress::PropertyStep::PUB_UNIT:
              if (iProperty->getUnit() && (iProperty->getUnit()[0] != '\0')) {
                strcpy_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), node->getId());
                strcat_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), iProperty->getId());
                strcat_P(subtopic.get(), PSTR("/$unit"));
                packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, iProperty->getUnit());
                if (packetId != 0) _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_FORMAT;
              } else {
                _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_FORMAT;
              }
              break;
            case AdvertisementProgress::PropertyStep::PUB_FORMAT:
            {
              bool sent = false;
              if (iProperty->getFormat() && (iProperty->getFormat()[0] != '\0')) {
                strcpy_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), node->getId());
                strcat_P(subtopic.get(), PSTR("/"));
                strcat(subtopic.get(), iProperty->getId());
                strcat_P(subtopic.get(), PSTR("/$format"));
                packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(subtopic.get()), 1, true, iProperty->getFormat());
                if (packetId != 0) sent = true;
              } else {
                sent = true;
              }

              if (sent) {
                if (_advertisementProgress.currentPropertyIndex < node->getProperties().size() - 1) {
                  // Not all properties of the node have been advertised
                  _advertisementProgress.currentPropertyIndex++;
                  _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_NAME;
                } else {
                  // All properties of the node have been advertised
                  if (_advertisementProgress.currentNodeIndex < HomieNode::nodes.size() - 1) {
                    // Not all nodes have been advertised
                    _advertisementProgress.currentNodeIndex++;
                    _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_NAME;
                    _advertisementProgress.currentPropertyIndex = 0;
                    _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_NAME;
                  } else {
                    // All nodes have been advertised -> next global step
                    _advertisementProgress.currentNodeIndex = 0;
                    _advertisementProgress.nodeStep = AdvertisementProgress::NodeStep::PUB_NAME;
                    _advertisementProgress.currentPropertyIndex = 0;
                    _advertisementProgress.propertyStep = AdvertisementProgress::PropertyStep::PUB_NAME;
                    _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::SUB_IMPLEMENTATION_OTA;
                  }
                }
              }
              break;
            }
          }
        }
      }
      break;
    }
    case AdvertisementProgress::GlobalStep::SUB_IMPLEMENTATION_OTA:
      packetId = Interface::get().getMqttClient().subscribe(_prefixMqttTopic(PSTR("/$implementation/ota/firmware/+")), 1);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::SUB_IMPLEMENTATION_RESET;
      break;
    case AdvertisementProgress::GlobalStep::SUB_IMPLEMENTATION_RESET:
      packetId = Interface::get().getMqttClient().subscribe(_prefixMqttTopic(PSTR("/$implementation/reset")), 1);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::SUB_IMPLEMENTATION_CONFIG_SET;
      break;
    case AdvertisementProgress::GlobalStep::SUB_IMPLEMENTATION_CONFIG_SET:
      packetId = Interface::get().getMqttClient().subscribe(_prefixMqttTopic(PSTR("/$implementation/config/set")), 1);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::SUB_SET;
      break;
    case AdvertisementProgress::GlobalStep::SUB_SET:
      packetId = Interface::get().getMqttClient().subscribe(_prefixMqttTopic(PSTR("/+/+/set")), 2);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::SUB_BROADCAST;
      break;
    case AdvertisementProgress::GlobalStep::SUB_BROADCAST:
    {
      const size_t rootTopicLength = Helpers::mqttRootTopicLength(Interface::get().getConfig().get().mqtt.baseTopic);
      std::unique_ptr<char[]> broadcastTopic(new (std::nothrow) char[rootTopicLength + strlen_P(PSTR("$broadcast/#")) + 1]);
      if (!broadcastTopic) return;
      Helpers::buildMqttRootTopic(broadcastTopic.get(), Interface::get().getConfig().get().mqtt.baseTopic);
#if HOMIE_CONVENTION_V5
      strcat_P(broadcastTopic.get(), PSTR("$broadcast/#"));
#else
      strcat_P(broadcastTopic.get(), PSTR("$broadcast/+"));
#endif
      packetId = Interface::get().getMqttClient().subscribe(broadcastTopic.get(), 2);
      if (packetId != 0) _advertisementProgress.globalStep = AdvertisementProgress::GlobalStep::PUB_READY;
      break;
    }
    case AdvertisementProgress::GlobalStep::PUB_READY:
      packetId = Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$state")), 1, true, "ready");
      if (packetId != 0) _advertisementProgress.done = true;
      break;
  }
}

void BootNormal::_onMqttConnected() {
  setFlag(_mqttEventPending);
}

void BootNormal::_onMqttDisconnected(AsyncMqttClientDisconnectReason reason) {
  AsyncStateCriticalGuard lock;
  _mqttDisconnectReasonPending = static_cast<int32_t>(reason);
  _mqttEventPending = true;
}

void BootNormal::_onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if (total == 0) return;  // no empty message possible

  if (index == 0) {
    // Copy and split the topic once. Chunked MQTT payload callbacks reuse this
    // topic state until the final chunk is received.
    size_t topicLength = strlen(topic);
    _mqttTopicCopy = std::unique_ptr<char[]>(new (std::nothrow) char[topicLength + 1]);
    if (!_mqttTopicCopy) {
      incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
      return;
    }
    memcpy(_mqttTopicCopy.get(), topic, topicLength);
    _mqttTopicCopy.get()[topicLength] = '\0';

    // Split the topic copy on each "/"
    if (!__splitTopic(_mqttTopicCopy.get(), _mqttTopicLevels, _mqttTopicLevelsCount)) {
      incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
      return;
    }
    if (_mqttTopicLevelsCount == 0) {
      incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
      return;
    }
  } else if (!_mqttTopicCopy || !_mqttTopicLevels) {
    incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
    return;
  }

  // OTA chunks may contain binary/base64 payloads and are streamed directly to
  // the Update API. All other MQTT messages wait for a complete C-string buffer
  // before being queued to the main loop.
  if (__handleOTAUpdates(_mqttTopicCopy.get(), payload, properties, len, index, total, _mqttTopicLevels.get(), _mqttTopicLevelsCount))
    return;

  if (__fillPayloadBuffer(_mqttPayloadBuffer, payload, len, index, total))
    return;

  if (!_enqueuePendingMqttMessage(topic, _mqttPayloadBuffer.get(), properties)) {
    incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
  }
}

void BootNormal::_onMqttPublish(uint16_t id) {
  if (!_enqueuePendingMqttAck(id)) {
    incrementDropCounters(_pendingMqttAcksDropped, _pendingMqttAcksDroppedTotal);
  }
}

// _onMqttMessage Helpers

bool BootNormal::__splitTopic(char* topic, std::unique_ptr<char*[]>& topicLevels, uint8_t& topicLevelsCount) {
  // split topic on each "/"
  const size_t rootTopicLength = Helpers::mqttRootTopicLength(Interface::get().getConfig().get().mqtt.baseTopic);
  if (strlen(topic) < rootTopicLength) {
    topicLevelsCount = 0;
    return false;
  }
  char* afterBaseTopic = topic + rootTopicLength;

  uint8_t levelsCount = 1;
  for (uint8_t i = 0; i < strlen(afterBaseTopic); i++) {
    if (afterBaseTopic[i] == '/') levelsCount++;
  }

  topicLevels = std::unique_ptr<char*[]>(new (std::nothrow) char*[levelsCount]);
  if (!topicLevels) {
    topicLevelsCount = 0;
    return false;
  }

  const char* delimiter = "/";
  uint8_t topicLevelIndex = 0;
  char* saveptr = nullptr;

  char* token = strtok_r(afterBaseTopic, delimiter, &saveptr);
  while (token != nullptr) {
    topicLevels[topicLevelIndex++] = token;

    token = strtok_r(nullptr, delimiter, &saveptr);
  }

  topicLevelsCount = topicLevelIndex;
  return true;
}

bool HomieInternals::BootNormal::__fillPayloadBuffer(std::unique_ptr<char[]>& payloadBuffer, char* payload, size_t len, size_t index, size_t total) {
  // Reallocate the payload buffer every time a new message is received.
  if (index == 0) {
    payloadBuffer = std::unique_ptr<char[]>(new (std::nothrow) char[total + 1]);
    if (!payloadBuffer) {
      incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
      return true;
    }
  } else if (payloadBuffer == nullptr) {
    incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
    return true;
  }

  // copy payload into buffer
  memcpy(payloadBuffer.get() + index, payload, len);

  // return if payload buffer is not complete
  if (index + len != total)
    return true;
  // terminate buffer
  payloadBuffer.get()[total] = '\0';
  return false;
}

bool HomieInternals::BootNormal::__handleOTAUpdates(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount) {
  if (
    topicLevelsCount == 5
    && strcmp(topicLevels[0], Interface::get().getConfig().get().deviceId) == 0
    && strcmp_P(topicLevels[1], PSTR("$implementation")) == 0
    && strcmp_P(topicLevels[2], PSTR("ota")) == 0
    && strcmp_P(topicLevels[3], PSTR("firmware")) == 0
    ) {
    char* firmwareMd5 = topicLevels[4];

    if (index == 0 && !_otaOngoing) {
      if (_flaggedForReboot && _otaRequestedChecksum[0] != '\0' && strcmp(firmwareMd5, _otaRequestedChecksum) == 0) {
        Interface::get().getLogger() << F("! Ignoring duplicate OTA payload for firmware already flashed; reboot pending") << endl;
        _publishOtaStatus(200);  // repeat terminal success for a retransmitted QoS1 publish
        return true;
      }

      Interface::get().getLogger() << F("Receiving OTA payload") << endl;
      if (!Interface::get().getConfig().get().ota.enabled) {
        _publishOtaStatus(403);  // 403 Forbidden
        Interface::get().getLogger() << F("✖ Aborting, OTA not enabled") << endl;
        return true;
      }

      if (!Helpers::validateMd5(firmwareMd5)) {
        _endOtaUpdate(false, UPDATE_ERROR_MD5);
        Interface::get().getLogger() << F("✖ Aborting, invalid MD5") << endl;
        return true;
      } else if (strcmp(firmwareMd5, _fwChecksum) == 0) {
        _publishOtaStatus(304);  // 304 Not Modified
        Interface::get().getLogger() << F("✖ Aborting, firmware is the same") << endl;
        return true;
      } else {
        Update.setMD5(firmwareMd5);
        strlcpy(_otaRequestedChecksum, firmwareMd5, sizeof(_otaRequestedChecksum));
        _otaRequestedChecksum[sizeof(_otaRequestedChecksum) - 1] = '\0';
        _otaPayloadTotal = total;
        _otaPayloadProcessed = 0;
        _otaProgressPublishCounter = 0;
        _publishOtaStatus(202);
        _otaOngoing = true;

        Interface::get().getLogger() << F("↕ OTA started") << endl;
        setFlag(_otaStartedPending);
      }
    } else if (!_otaOngoing) {
      return true; // we've not validated the checksum
    }

    if (strcmp(firmwareMd5, _otaRequestedChecksum) != 0) {
      _failOtaUpdate(400, "NOT_REQUESTED", F("✖ Aborting, received OTA data for a different firmware request"));
      return true;
    }

    if (total != _otaPayloadTotal) {
      _failOtaUpdate(500, "INTERNAL_ERROR", F("✖ Aborting, OTA payload size changed mid-transfer"));
      return true;
    }

    const size_t rawChunkEnd = index + len;
    size_t skipRawPrefix = 0;

    if (index < _otaPayloadProcessed) {
      skipRawPrefix = _otaPayloadProcessed - index;
      if (skipRawPrefix >= len) {
        Interface::get().getLogger() << F("! Ignoring duplicate OTA chunk at offset ") << index;
        if (properties.dup) Interface::get().getLogger() << F(" (dup)");
        Interface::get().getLogger() << endl;
        return true;
      }

      Interface::get().getLogger() << F("! Trimming duplicate OTA bytes up to offset ") << _otaPayloadProcessed;
      if (properties.dup) Interface::get().getLogger() << F(" (dup)");
      Interface::get().getLogger() << endl;

      payload += skipRawPrefix;
      len -= skipRawPrefix;
      index += skipRawPrefix;
    }

    if (index != _otaPayloadProcessed) {
      _failOtaUpdate(500, "INTERNAL_ERROR", F("✖ Aborting, OTA chunk arrived out of sequence"));
      return true;
    }

    // here, we need to flash the payload

    if (index == 0) {
      // Autodetect if firmware is binary or base64-encoded. ESP firmware always has a magic first byte 0xE9.
      if (*payload == 0xE9) {
        _otaIsBase64 = false;
        Interface::get().getLogger() << F("Firmware is binary") << endl;
      } else {
        // Base64-decode first two bytes. Compare decoded value against magic byte.
        char plain[2];  // need 12 bits
        base64_init_decodestate(&_otaBase64State);
        int l = base64_decode_block(payload, 2, plain, &_otaBase64State);
        if ((l == 1) && (plain[0] == 0xE9)) {
          _otaIsBase64 = true;
          _otaBase64Pads = 0;
          Interface::get().getLogger() << F("Firmware is base64-encoded") << endl;
          if (total % 4) {
            // Base64 encoded length not a multiple of 4 bytes
            _endOtaUpdate(false, UPDATE_ERROR_MAGIC_BYTE);
            return true;
          }

          // Restart base64-decoder
          base64_init_decodestate(&_otaBase64State);
        } else {
          // Bad firmware format
          _endOtaUpdate(false, UPDATE_ERROR_MAGIC_BYTE);
          return true;
        }
      }
      _otaSizeDone = 0;
      _otaSizeTotal = _otaIsBase64 ? base64_decode_expected_len(total) : total;
      bool success = Update.begin(_otaSizeTotal);
      if (!success) {
        // Detected error during begin (e.g. size == 0 or size > space)
        _endOtaUpdate(false, Update.getError());
        return true;
      }
    }

    size_t write_len;
    if (_otaIsBase64) {
      // Base64-firmware: Make sure there are no non-base64 characters in the payload.
      // libb64/cdecode.c doesn't ignore such characters if the compiler treats `char`
      // as `unsigned char`.
      size_t bin_len = 0;
      char* p = payload;
      for (size_t i = 0; i < len; i++) {
        char c = *p++;
        bool b64 = ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '+') || (c == '/');
        if (b64) {
          bin_len++;
        } else if (c == '=') {
          // Ignore "=" padding (but only at the end and only up to 2)
          if (index + i < total - 2) {
            _endOtaUpdate(false, UPDATE_ERROR_MAGIC_BYTE);
            return true;
          }
          // Note the number of pad characters at the end
          _otaBase64Pads++;
        } else {
          // Non-base64 character in firmware
          _endOtaUpdate(false, UPDATE_ERROR_MAGIC_BYTE);
          return true;
        }
      }
      if (bin_len > 0) {
        // Decode base64 payload in-place. base64_decode_block() can decode in-place,
        // except for the first two base64-characters which make one binary byte plus
        // 4 extra bits (saved in _otaBase64State). So we "manually" decode the first
        // two characters into a temporary buffer and manually merge that back into
        // the payload. This one is a little tricky, but it saves us from having to
        // dynamically allocate some 800 bytes of memory for every payload chunk.
        size_t dec_len = bin_len > 1 ? 2 : 1;
        char c;
        write_len = static_cast<size_t>(base64_decode_block(payload, dec_len, &c, &_otaBase64State));
        *payload = c;

        if (bin_len > 1) {
          write_len += static_cast<size_t>(base64_decode_block((const char*)payload + dec_len, bin_len - dec_len, payload + write_len, &_otaBase64State));
        }
      } else {
        write_len = 0;
      }
    } else {
      // Binary firmware
      write_len = len;
    }
    if (write_len > 0) {
      bool success = Update.write(reinterpret_cast<uint8_t*>(payload), write_len) > 0;
      if (success) {
        // Flash write successful.
        _otaSizeDone += write_len;
        if (_otaIsBase64 && (index + len == total)) {
          // Having received the last chunk of base64 encoded firmware, we can now determine
          // the real size of the binary firmware from the number of padding character ("="):
          // If we have received 1 pad character, real firmware size modulo 3 was 2.
          // If we have received 2 pad characters, real firmware size modulo 3 was 1.
          // Correct the total firmware length accordingly.
          _otaSizeTotal -= _otaBase64Pads;
        }

        String progress(_otaSizeDone);
        progress.concat(F("/"));
        progress.concat(_otaSizeTotal);
        Interface::get().getLogger() << F("Receiving OTA firmware (") << progress << F(")...") << endl;

        {
          AsyncStateCriticalGuard lock;
          _otaProgressSizeDone = _otaSizeDone;
          _otaProgressSizeTotal = _otaSizeTotal;
          _otaProgressPending = true;
        }

        if (_otaProgressPublishCounter == 100) {
          _publishOtaStatus(206, progress.c_str());  // 206 Partial Content
          _otaProgressPublishCounter = 0;
        }
        ++_otaProgressPublishCounter;
      } else {
        // Error erasing or writing flash
        _endOtaUpdate(false, Update.getError());
        return true;
      }
    }

    _otaPayloadProcessed = rawChunkEnd;

    // Done with the update?
    if (rawChunkEnd == total) {
      // With base64-coded firmware, we may have provided a length off by one or two
      // to Update.begin() because the base64-coded firmware may use padding (one or
      // two "=") at the end. In case of base64, total length was adjusted above.
      // Check the real length here and ask Update::end() to skip this test.
      if ((_otaIsBase64) && (_otaSizeDone != _otaSizeTotal)) {
        _endOtaUpdate(false, UPDATE_ERROR_SIZE);
        return true;
      }
      bool success = Update.end(_otaIsBase64);
      _endOtaUpdate(success, Update.getError());
    }
    return true;
  }
  return false;
}

bool HomieInternals::BootNormal::__handleBroadcasts(char * topic, char * payload, const AsyncMqttClientMessageProperties & properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount) {
  if (
    topicLevelsCount >= 2
    && strcmp_P(topicLevels[0], PSTR("$broadcast")) == 0
    ) {
    String broadcastLevel(topicLevels[1]);
    for (uint8_t levelIndex = 2; levelIndex < topicLevelsCount; levelIndex++) {
      broadcastLevel += '/';
      broadcastLevel.concat(topicLevels[levelIndex]);
    }
    Interface::get().getLogger() << F("📢 Calling broadcast handler...") << endl;
    bool handled = Interface::get().broadcastHandler(broadcastLevel, payload);
    if (!handled) {
      Interface::get().getLogger() << F("The following broadcast was not handled:") << endl;
      Interface::get().getLogger() << F("  • Level: ") << broadcastLevel << endl;
      Interface::get().getLogger() << F("  • Value: ") << payload << endl;
    }
    return true;
  }
  return false;
}

bool HomieInternals::BootNormal::__handleResets(char * topic, char * payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount) {
  if (
    topicLevelsCount == 3
    && strcmp_P(topicLevels[1], PSTR("$implementation")) == 0
    && strcmp_P(topicLevels[2], PSTR("reset")) == 0
    && strcmp_P(payload, PSTR("true")) == 0
    ) {
    Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$implementation/reset")), 1, true, "false");
    Interface::get().getLogger() << F("Flagged for reset by network") << endl;
    Interface::get().disable = true;
    Interface::get().reset.resetFlag = true;
    return true;
  }
  return false;
}

bool HomieInternals::BootNormal::__handleConfig(char * topic, char * payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount) {
  if (
    topicLevelsCount == 4
    && strcmp_P(topicLevels[1], PSTR("$implementation")) == 0
    && strcmp_P(topicLevels[2], PSTR("config")) == 0
    && strcmp_P(topicLevels[3], PSTR("set")) == 0
    ) {
    Interface::get().getMqttClient().publish(_prefixMqttTopic(PSTR("/$implementation/config/set")), 1, true, "");
    if (Interface::get().getConfig().patch(payload)) {
      Interface::get().getLogger() << F("✔ Configuration updated") << endl;
      _flaggedForReboot = true;
      Interface::get().getLogger() << F("Flagged for reboot") << endl;
    } else {
      Interface::get().getLogger() << F("✖ Configuration not updated") << endl;
    }
    return true;
  }
  return false;
}

bool HomieInternals::BootNormal::__handleNodeProperty(char * topic, char * payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount) {
  if (topicLevelsCount != 4 || strcmp_P(topicLevels[3], PSTR("set")) != 0) {
    return false;
  }
#if HOMIE_CONVENTION_V5
  if (properties.retain) {
    Interface::get().getLogger() << F("! Ignoring retained Homie v5 set command for ")
                                 << topicLevels[1] << F("/") << topicLevels[2] << endl;
    return true;
  }
#endif

  // initialize HomieRange
  HomieRange range;
  range.isRange = false;
  range.index = 0;

  char* node = topicLevels[1];
  char* property = topicLevels[2];
  HomieNode* homieNode = nullptr;

#if HOMIE_CONVENTION_V5
  for (HomieNode* iNode : HomieNode::nodes) {
    if (strcmp(node, iNode->getId()) == 0) {
      homieNode = iNode;
      break;
    }

    const size_t nodeIdLength = strlen(iNode->getId());
    if (!iNode->isRange()
        || strncmp(node, iNode->getId(), nodeIdLength) != 0
        || node[nodeIdLength] != '-'
        || node[nodeIdLength + 1] == '\0') {
      continue;
    }

    char* rangeIndexStr = node + nodeIdLength + 1;
    String rangeIndexTest = String(rangeIndexStr);
    for (uint8_t i = 0; i < rangeIndexTest.length(); i++) {
      if (!isDigit(rangeIndexTest.charAt(i))) {
        Interface::get().getLogger() << F("Range index ") << rangeIndexStr << F(" is not valid") << endl;
        return true;
      }
    }
    range.isRange = true;
    range.index = rangeIndexTest.toInt();
    homieNode = iNode;
    break;
  }
#else
  int16_t rangeSeparator = -1;
  for (uint16_t i = 0; i < strlen(node); i++) {
    if (node[i] == '_') {
      rangeSeparator = i;
      break;
    }
  }
  if (rangeSeparator != -1) {
    range.isRange = true;
    node[rangeSeparator] = '\0';
    char* rangeIndexStr = node + rangeSeparator + 1;
    String rangeIndexTest = String(rangeIndexStr);
    for (uint8_t i = 0; i < rangeIndexTest.length(); i++) {
      if (!isDigit(rangeIndexTest.charAt(i))) {
        Interface::get().getLogger() << F("Range index ") << rangeIndexStr << F(" is not valid") << endl;
        return true;
      }
    }
    range.index = rangeIndexTest.toInt();
  }
  homieNode = HomieNode::find(node);
#endif

  if (!homieNode) {
    Interface::get().getLogger() << F("Node ") << node << F(" not registered") << endl;
    return true;
  }

  #ifdef DEBUG
    Interface::get().getLogger() << F("Received network message for ") << homieNode->getId() << endl;
  #endif // DEBUG

  if (homieNode->isRange()) {
    if (range.index < homieNode->getLower() || range.index > homieNode->getUpper()) {
      Interface::get().getLogger() << F("Range index ") << range.index << F(" is not within the bounds of ") << homieNode->getId() << endl;
      return true;
    }
  }

  Property* propertyObject = nullptr;
  for (Property* iProperty : homieNode->getProperties()) {
    if (strcmp(property, iProperty->getId()) == 0) {
      propertyObject = iProperty;
      break;
    }
  }

  if (!propertyObject || !propertyObject->isSettable()) {
    Interface::get().getLogger() << F("Node ") << node << F(": ") << property << F(" property not settable") << endl;
    return true;
  }

  #ifdef DEBUG
    Interface::get().getLogger() << F("Calling global input handler...") << endl;
  #endif // DEBUG
  bool handled = Interface::get().globalInputHandler(*homieNode, range, String(property), String(payload));
  if (handled) return true;

  #ifdef DEBUG
    Interface::get().getLogger() << F("Calling node input handler...") << endl;
  #endif // DEBUG
  handled = homieNode->handleInput(range, String(property), String(payload));
  if (handled) return true;

  #ifdef DEBUG
    Interface::get().getLogger() << F("Calling property input handler...") << endl;
  #endif // DEBUG
  handled = propertyObject->getInputHandler()(range, String(payload));

  if (!handled) {
    Interface::get().getLogger() << F("No handlers handled the following packet:") << endl;
    Interface::get().getLogger() << F("  • Node ID: ") << node << endl;
    Interface::get().getLogger() << F("  • Is range? ");
    if (range.isRange) {
      Interface::get().getLogger() << F("yes (") << range.index << F(")") << endl;
    } else {
      Interface::get().getLogger() << F("no") << endl;
    }
    Interface::get().getLogger() << F("  • Property: ") << property << endl;
    Interface::get().getLogger() << F("  • Value: ") << payload << endl;
  }

  return false;
}
