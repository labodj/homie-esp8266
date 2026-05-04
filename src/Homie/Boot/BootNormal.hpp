
#pragma once

#include "Arduino.h"

#include <array>
#include <functional>
#include <libb64/cdecode.h>

#ifndef HOMIE_MDNS
#define HOMIE_MDNS 1
#endif

#ifndef HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE
#define HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE 32
#endif

#ifndef HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE
#define HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE 16
#endif

#ifndef HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED
#define HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED 0
#endif

#ifndef HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH
#define HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH 192
#endif

#ifndef HOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH
#define HOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH 512
#endif

#ifndef HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS
#define HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS 12
#endif

#ifndef HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP
#define HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP 4
#endif

#ifndef HOMIE_PENDING_MQTT_ACKS_PER_LOOP
#define HOMIE_PENDING_MQTT_ACKS_PER_LOOP 8
#endif

#ifndef HOMIE_OTA_STATUS_INFO_MAX_LENGTH
#define HOMIE_OTA_STATUS_INFO_MAX_LENGTH 48
#endif


#ifdef ESP32
#include <WiFi.h>
#include <Update.h>
#if HOMIE_MDNS
#include <ESPmDNS.h>
#endif
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#if HOMIE_MDNS
#include <ESP8266mDNS.h>
#endif
#endif // ESP32


#include <AsyncMqttClient.h>
#include "../../HomieNode.hpp"
#include "../../HomieRange.hpp"
#include "../../StreamingOperator.hpp"
#include "../Constants.hpp"
#include "../Limits.hpp"
#include "../Datatypes/Interface.hpp"
#include "../Utils/Helpers.hpp"
#include "../Uptime.hpp"
#include "../Timer.hpp"
#include "../ExponentialBackoffTimer.hpp"
#include "Boot.hpp"
#include "../Utils/ResetHandler.hpp"

namespace HomieInternals {
class BootNormal : public Boot {
 public:
  BootNormal();
  ~BootNormal();
  void setup();
  void loop();

 private:
  // These queues bridge AsyncMqttClient callbacks and Homie.loop(). They are
  // intentionally bounded; callback context never waits for loop context.
  // Advanced consumers can raise them with HOMIE_PENDING_MQTT_*_QUEUE_SIZE when
  // retained MQTT bursts are expected.
  static_assert(HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE > 0,
                "HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE must be greater than zero");
  static_assert(HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE <= 255,
                "HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE must fit in uint8_t");
  static constexpr uint8_t PENDING_MQTT_MESSAGE_QUEUE_SIZE = HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE;
#if HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED
  static_assert(HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH > 0,
                "HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH must be greater than zero");
  static_assert(HOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH > 0,
                "HOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH must be greater than zero");
  static_assert(HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS > 0,
                "HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS must be greater than zero");
  static_assert(HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS <= 255,
                "HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS must fit in uint8_t");
  static constexpr size_t PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH = HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH;
  static constexpr size_t PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH = HOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH;
  static constexpr uint8_t PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS = HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS;
#endif
  static_assert(HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE > 0,
                "HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE must be greater than zero");
  static_assert(HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE <= 255,
                "HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE must fit in uint8_t");
  static constexpr uint8_t PENDING_MQTT_ACK_QUEUE_SIZE = HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE;
  static_assert(HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP > 0,
                "HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP must be greater than zero");
  static_assert(HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP <= 255,
                "HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP must fit in uint8_t");
  static constexpr uint8_t PENDING_MQTT_MESSAGES_PER_LOOP_LIMIT = HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP;
  static_assert(HOMIE_PENDING_MQTT_ACKS_PER_LOOP > 0,
                "HOMIE_PENDING_MQTT_ACKS_PER_LOOP must be greater than zero");
  static_assert(HOMIE_PENDING_MQTT_ACKS_PER_LOOP <= 255,
                "HOMIE_PENDING_MQTT_ACKS_PER_LOOP must fit in uint8_t");
  static constexpr uint8_t PENDING_MQTT_ACKS_PER_LOOP_LIMIT = HOMIE_PENDING_MQTT_ACKS_PER_LOOP;

  struct AdvertisementProgress {
    bool done = false;
    enum class GlobalStep {
      PUB_INIT,
      PUB_DESCRIPTION,
      PUB_HOMIE,
      PUB_NAME,
      PUB_MAC,
      PUB_LOCALIP,
      PUB_NODES_ATTR,
      PUB_EXTENSIONS,
      PUB_STATS,
      PUB_STATS_INTERVAL,
      PUB_FW_NAME,
      PUB_FW_VERSION,
      PUB_FW_CHECKSUM,
      PUB_IMPLEMENTATION,
      PUB_IMPLEMENTATION_CONFIG,
      PUB_IMPLEMENTATION_VERSION,
      PUB_IMPLEMENTATION_RESET_REASON,
      PUB_IMPLEMENTATION_WIFI_LAST_DISCONNECT_REASON,
      PUB_IMPLEMENTATION_MQTT_LAST_DISCONNECT_REASON,
      PUB_IMPLEMENTATION_OTA_ENABLED,
      PUB_NODES,
      SUB_IMPLEMENTATION_OTA,
      SUB_IMPLEMENTATION_RESET,
      SUB_IMPLEMENTATION_CONFIG_SET,
      SUB_SET,
      SUB_BROADCAST,
      PUB_READY
    } globalStep;

    enum class NodeStep {
      PUB_NAME,
      PUB_TYPE,
      PUB_ARRAY,
      PUB_ARRAY_NODES,
      PUB_PROPERTIES,
      PUB_PROPERTIES_ATTRIBUTES
    } nodeStep;

    enum class PropertyStep {
      PUB_NAME,
      PUB_SETTABLE,
      PUB_RETAINED,
      PUB_DATATYPE,
      PUB_UNIT,
      PUB_FORMAT
    } propertyStep;

    size_t currentNodeIndex;
    size_t currentArrayNodeIndex;
    size_t currentPropertyIndex;
  } _advertisementProgress;
  struct PendingMqttMessage {
#if HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED
    char topic[PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH + 1];
    char payload[PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH + 1];
#else
    // Owning copies are required because AsyncMqttClient callback buffers are
    // no longer valid once the callback returns.
    std::unique_ptr<char[]> topic;
    std::unique_ptr<char[]> payload;
#endif
#if HOMIE_STRICT_PROPERTY_VALIDATION
    size_t payloadLength = 0;
#endif
    AsyncMqttClientMessageProperties properties{};
  };
  Uptime _uptime;
  Uptime _uptimeWifi;
  Uptime _uptimeMqtt;
  Timer _statsTimer;
  ExponentialBackoffTimer _mqttReconnectTimer;
  ExponentialBackoffTimer _wifiReconnectTimer;
  bool _setupFunctionCalled;
  bool _wifiGotIp;
  bool _wifiConnectInProgress;
  bool _mqttConnectInProgress;
  bool _recoveryInProgress;
  bool _hostnameConfigured;
  bool _mdnsStarted;
  uint32_t _wifiConnectAttemptAt;
  uint32_t _mqttConnectAttemptAt;
  uint32_t _recoveryStartedAt;
  int32_t _lastWifiDisconnectReason;
  int32_t _lastMqttDisconnectReason;
  // Volatile fields below are written from async Wi-Fi/MQTT callbacks and
  // consumed from Homie.loop() under AsyncStateCriticalGuard in BootNormal.cpp.
  volatile bool _wifiEventPending;
  volatile int32_t _wifiDisconnectReasonPending;
  volatile bool _mqttEventPending;
  volatile int32_t _mqttDisconnectReasonPending;
  volatile uint8_t _pendingMqttMessageReadIndex;
  volatile uint8_t _pendingMqttMessageWriteIndex;
  volatile uint8_t _pendingMqttMessageCount;
  volatile uint16_t _pendingMqttMessagesDropped;
  volatile uint32_t _pendingMqttMessagesDroppedTotal;
  volatile uint8_t _pendingMqttMessageMaxDepth;
  volatile uint8_t _pendingMqttAckReadIndex;
  volatile uint8_t _pendingMqttAckWriteIndex;
  volatile uint8_t _pendingMqttAckCount;
  volatile uint16_t _pendingMqttAcksDropped;
  volatile uint32_t _pendingMqttAcksDroppedTotal;
  volatile uint8_t _pendingMqttAckMaxDepth;
  volatile bool _otaStartedPending;
  volatile bool _otaProgressPending;
  volatile size_t _otaProgressSizeDone;
  volatile size_t _otaProgressSizeTotal;
  volatile bool _otaSuccessfulPending;
  volatile bool _otaFailedPending;
  volatile bool _otaStatusPending;
  volatile int _otaStatusCode;
  volatile uint16_t _otaStatusSequence;
  volatile uint32_t _otaStatusQueuedAt;
  char _otaStatusInfo[HOMIE_OTA_STATUS_INFO_MAX_LENGTH];
  #ifdef ESP32
  WiFiEventId_t _wifiGotIpHandler;
  WiFiEventId_t _wifiDisconnectedHandler;
  #elif defined(ESP8266)
  WiFiEventHandler _wifiGotIpHandler;
  WiFiEventHandler _wifiDisconnectedHandler;
  #endif // ESP32
  bool _mqttConnectNotified;
  bool _mqttDisconnectNotified;
  bool _otaOngoing;
  bool _flaggedForReboot;
  uint16_t _mqttOfflineMessageId;
  char _fwChecksum[32 + 1];
  char _otaRequestedChecksum[32 + 1];
  bool _otaIsBase64;
  base64_decodestate _otaBase64State;
  size_t _otaBase64Pads;
  // OTA byte counters track both decoded firmware bytes and raw MQTT payload
  // bytes so retransmitted QoS 1 chunks can be identified and trimmed safely.
  size_t _otaSizeTotal;
  size_t _otaSizeDone;
  size_t _otaPayloadTotal;
  size_t _otaPayloadProcessed;
  uint16_t _otaProgressPublishCounter;

  std::unique_ptr<char[]> _mqttTopic;
  std::unique_ptr<char[]> _mqttRootTopic;
  size_t _mqttRootTopicLength;

  std::unique_ptr<char[]> _mqttClientId;
  std::unique_ptr<char[]> _mqttWillTopic;
  std::unique_ptr<char[]> _mqttPayloadBuffer;
  size_t _mqttPayloadBufferCapacity;
  std::unique_ptr<char*[]> _mqttTopicLevels;
  uint8_t _mqttTopicLevelsCapacity;
  uint8_t _mqttTopicLevelsCount;
  std::unique_ptr<char[]> _mqttTopicCopy;
  size_t _mqttTopicCopyCapacity;
  bool _mqttTopicValid;
#if HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED
  std::array<char, PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH + 1> _mqttPreallocatedTopicCopy;
  std::array<char, PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH + 1> _mqttPreallocatedPayloadBuffer;
  std::array<char*, PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS> _mqttPreallocatedTopicLevels;
  uint8_t _mqttPreallocatedTopicLevelsCount;
  bool _mqttPreallocatedTopicValid;
#endif
  std::array<PendingMqttMessage, PENDING_MQTT_MESSAGE_QUEUE_SIZE> _pendingMqttMessages;
  std::array<uint16_t, PENDING_MQTT_ACK_QUEUE_SIZE> _pendingMqttAckIds;

  void _wifiConnect();
  void _markConnectivityRecovering();
  void _markConnectivityHealthy();
  void _scheduleRecoveryReboot(const __FlashStringHelper* reason);
  bool _isWifiConnected() const;
  void _processPendingAsyncEvents();
  void _processPendingEventNotifications();
  void _processPendingMqttMessages();
  void _flushPendingMqttMessages();
  bool _enqueuePendingMqttAck(uint16_t id);
  bool _enqueuePendingMqttMessage(const char* topic, const char* payload, size_t payloadLength, const AsyncMqttClientMessageProperties& properties);
  void _handleQueuedMqttMessage(char* topic, char* payload, size_t payloadLength, const AsyncMqttClientMessageProperties& properties);
  void _recoverIfNetworkStateDrifted();
  void _recoverIfConnectAttemptStalled();
  void _handleWifiConnected(const IPAddress& ip, const IPAddress& mask, const IPAddress& gateway);
  void _handleWifiDisconnected(int32_t reason);
  #ifdef ESP32
  void _onWifiGotIp(WiFiEvent_t event, WiFiEventInfo_t info);
  void _onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
  #elif defined(ESP8266)
  void _onWifiGotIp(const WiFiEventStationModeGotIP& event);
  void _onWifiDisconnected(const WiFiEventStationModeDisconnected& event);
  #endif // ESP32
  void _mqttConnect();
  void _handleMqttConnected();
  void _handleMqttDisconnected(AsyncMqttClientDisconnectReason reason);
  void _resetAdvertisementProgress();
  // Homie v5 discovery is a single retained JSON document. These helpers keep
  // sizing, version hashing and publishing separate from the v3/v4 per-topic
  // advertisement state machine.
  size_t _estimateV5DescriptionLength() const;
  uint32_t _computeV5DescriptionVersion() const;
  uint16_t _publishV5Description();
  void _advertise();
  void _onMqttConnected();
  void _onMqttDisconnected(AsyncMqttClientDisconnectReason reason);
  void _onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
  void _onMqttPublish(uint16_t id);
  void _prefixMqttTopic();
  char* _prefixMqttTopic(PGM_P topic);
  bool _publishOtaStatus(int status, const char* info = nullptr);
  void _queueOtaStatus(int status, const char* info = nullptr);
  void _processPendingOtaStatus();
  void _resetOtaTransferState(bool preserveRequestedChecksum = false);
  void _failOtaUpdate(int status, const char* info, const __FlashStringHelper* reason);
  void _abortOtaUpdateOnDisconnect();
  void _endOtaUpdate(bool success, uint8_t update_error = UPDATE_ERROR_OK);
  bool _writeOtaPayload(char* payload, size_t length);

  // _onMqttMessage Helpers
  bool __splitTopic(char* topic, std::unique_ptr<char*[]>& topicLevels, uint8_t& topicLevelsCount, uint8_t* topicLevelsCapacity = nullptr);
#if HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED
  bool __splitTopicFixed(char* topic, std::array<char*, PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS>& topicLevels, uint8_t& topicLevelsCount);
  bool __fillPreallocatedPayloadBuffer(char* payload, size_t len, size_t index, size_t total);
#endif
  bool __fillPayloadBuffer(std::unique_ptr<char[]>& payloadBuffer, size_t& payloadBufferCapacity, char* payload, size_t len, size_t index, size_t total);
  bool __handleOTAUpdates(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
  bool __handleBroadcasts(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
  bool __handleResets(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
  bool __handleConfig(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
  bool __handleNodeProperty(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
};
}  // namespace HomieInternals
