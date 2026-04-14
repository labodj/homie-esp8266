
#pragma once

#include "Arduino.h"

#include <array>
#include <atomic>
#include <functional>
#include <libb64/cdecode.h>

#ifndef HOMIE_MDNS
#define HOMIE_MDNS 1
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
  static constexpr uint8_t PENDING_MQTT_MESSAGE_QUEUE_SIZE = 16;
  static constexpr uint8_t PENDING_MQTT_ACK_QUEUE_SIZE = 16;

  struct AdvertisementProgress {
    bool done = false;
    enum class GlobalStep {
      PUB_INIT,
      PUB_HOMIE,
      PUB_NAME,
      PUB_MAC,
      PUB_LOCALIP,
      PUB_NODES_ATTR,
      PUB_STATS,
      PUB_STATS_INTERVAL,
      PUB_FW_NAME,
      PUB_FW_VERSION,
      PUB_FW_CHECKSUM,
      PUB_IMPLEMENTATION,
      PUB_IMPLEMENTATION_CONFIG,
      PUB_IMPLEMENTATION_VERSION,
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
    std::unique_ptr<char[]> topic;
    std::unique_ptr<char[]> payload;
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
  std::atomic<bool> _wifiEventPending;
  std::atomic<int32_t> _wifiDisconnectReasonPending;
  std::atomic<bool> _mqttEventPending;
  std::atomic<int32_t> _mqttDisconnectReasonPending;
  std::atomic<uint8_t> _pendingMqttMessageReadIndex;
  std::atomic<uint8_t> _pendingMqttMessageWriteIndex;
  std::atomic<uint8_t> _pendingMqttMessageCount;
  std::atomic<uint16_t> _pendingMqttMessagesDropped;
  std::atomic<bool> _pendingMqttMessageQueueLocked;
  std::atomic<uint8_t> _pendingMqttAckReadIndex;
  std::atomic<uint8_t> _pendingMqttAckWriteIndex;
  std::atomic<uint8_t> _pendingMqttAckCount;
  std::atomic<uint16_t> _pendingMqttAcksDropped;
  std::atomic<bool> _otaStartedPending;
  std::atomic<bool> _otaProgressPending;
  std::atomic<size_t> _otaProgressSizeDone;
  std::atomic<size_t> _otaProgressSizeTotal;
  std::atomic<bool> _otaSuccessfulPending;
  std::atomic<bool> _otaFailedPending;
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
  size_t _otaSizeTotal;
  size_t _otaSizeDone;
  size_t _otaPayloadTotal;
  size_t _otaPayloadProcessed;
  uint16_t _otaProgressPublishCounter;

  std::unique_ptr<char[]> _mqttTopic;

  std::unique_ptr<char[]> _mqttClientId;
  std::unique_ptr<char[]> _mqttWillTopic;
  std::unique_ptr<char[]> _mqttPayloadBuffer;
  std::unique_ptr<char*[]> _mqttTopicLevels;
  uint8_t _mqttTopicLevelsCount;
  std::unique_ptr<char[]> _mqttTopicCopy;
  std::array<PendingMqttMessage, PENDING_MQTT_MESSAGE_QUEUE_SIZE> _pendingMqttMessages;
  std::array<uint16_t, PENDING_MQTT_ACK_QUEUE_SIZE> _pendingMqttAckIds;

  void _wifiConnect();
  void _markConnectivityRecovering();
  void _markConnectivityHealthy();
  void _scheduleRecoveryReboot(const __FlashStringHelper* reason);
  bool _isWifiConnected() const;
  void _processPendingAsyncEvents();
  void _processPendingEventNotifications();
  void _lockPendingMqttMessageQueue();
  void _unlockPendingMqttMessageQueue();
  void _processPendingMqttMessages();
  void _flushPendingMqttMessages();
  bool _enqueuePendingMqttAck(uint16_t id);
  bool _enqueuePendingMqttMessage(const char* topic, const char* payload, const AsyncMqttClientMessageProperties& properties);
  void _handleQueuedMqttMessage(std::unique_ptr<char[]> topicCopy, std::unique_ptr<char[]> payloadBuffer, const AsyncMqttClientMessageProperties& properties);
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
  void _advertise();
  void _onMqttConnected();
  void _onMqttDisconnected(AsyncMqttClientDisconnectReason reason);
  void _onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
  void _onMqttPublish(uint16_t id);
  void _prefixMqttTopic();
  char* _prefixMqttTopic(PGM_P topic);
  bool _publishOtaStatus(int status, const char* info = nullptr);
  void _resetOtaTransferState(bool preserveRequestedChecksum = false);
  void _failOtaUpdate(int status, const char* info, const __FlashStringHelper* reason);
  void _abortOtaUpdateOnDisconnect();
  void _endOtaUpdate(bool success, uint8_t update_error = UPDATE_ERROR_OK);

  // _onMqttMessage Helpers
  void __splitTopic(char* topic, std::unique_ptr<char*[]>& topicLevels, uint8_t& topicLevelsCount);
  bool __fillPayloadBuffer(std::unique_ptr<char[]>& payloadBuffer, char* payload, size_t len, size_t index, size_t total);
  bool __handleOTAUpdates(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
  bool __handleBroadcasts(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
  bool __handleResets(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
  bool __handleConfig(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
  bool __handleNodeProperty(char* topic, char* payload, const AsyncMqttClientMessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount);
};
}  // namespace HomieInternals
