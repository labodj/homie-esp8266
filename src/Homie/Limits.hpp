#pragma once

#include <ArduinoJson.h>

namespace HomieInternals {
  const uint16_t MAX_JSON_CONFIG_FILE_SIZE = 1000;

  // max setting elements
  const uint8_t MAX_CONFIG_SETTING_SIZE = 10;
  // 7 root fields, 9 Wi-Fi fields, 8 MQTT fields, 1 OTA field and max settings.
  // The extra MQTT slot accepts the read-only
  // effective_base_topic diagnostic if a tool echoes the advertised config.
  const uint16_t MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE = JSON_OBJECT_SIZE(7) + JSON_OBJECT_SIZE(9) + JSON_OBJECT_SIZE(9) + JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(MAX_CONFIG_SETTING_SIZE);
  // HTTP requests outlive their buffers during connection tests. Merging also
  // temporarily retains strings replaced in the previous configuration.
  const uint16_t MAX_JSON_CONFIG_OWNED_BUFFER_SIZE = MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE + 2 * MAX_JSON_CONFIG_FILE_SIZE;

  const uint8_t MAX_MAC_LENGTH = 6;
  const uint8_t MAX_MAC_STRING_LENGTH = (MAX_MAC_LENGTH * 2) + 1;

  const uint8_t MAX_WIFI_SSID_LENGTH = 32 + 1;
  const uint8_t MAX_WIFI_PASSWORD_LENGTH = 64 + 1;
  const uint16_t MAX_HOSTNAME_LENGTH = 255 + 1;

  const uint8_t MAX_MQTT_CREDS_LENGTH = 32 + 1;
  const uint8_t MAX_MQTT_BASE_TOPIC_LENGTH = 48 + 1;
  const uint8_t MAX_MQTT_TOPIC_LENGTH = 128 + 1;

  const uint8_t MAX_FRIENDLY_NAME_LENGTH = 64 + 1;
  const uint8_t MAX_DEVICE_ID_LENGTH = 32 + 1;

  const uint8_t MAX_BRAND_LENGTH = MAX_WIFI_SSID_LENGTH - 10 - 1;
  const uint8_t MAX_FIRMWARE_NAME_LENGTH = 32 + 1;
  const uint8_t MAX_FIRMWARE_VERSION_LENGTH = 16 + 1;

  const uint8_t MAX_NODE_ID_LENGTH = 24 + 1;
  const uint8_t MAX_NODE_TYPE_LENGTH = 24 + 1;
  const uint8_t MAX_NODE_PROPERTY_LENGTH = 24 + 1;

  const uint8_t MAX_IP_STRING_LENGTH = 16 + 1;

}  // namespace HomieInternals
