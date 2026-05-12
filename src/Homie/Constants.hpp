#pragma once

#ifdef ESP32
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif // ESP32

#ifndef HOMIE_CONFIG
#define HOMIE_CONFIG 1
#endif

// Keep the default runtime lightweight. The library always validates the Homie
// structure it owns; exact property format and payload compliance can be
// checked at runtime by opting into the heavier validation path.
#ifndef HOMIE_STRICT_PROPERTY_VALIDATION
#define HOMIE_STRICT_PROPERTY_VALIDATION 0
#endif

// MQTT convention advertisement is intentionally build-time selected. Keeping
// Homie 3.0.1 as the default preserves the existing device discovery contract.
// Newer convention modes are opt-in because they change how controllers
// discover devices, especially Homie 5 where discovery moves to $description.
#ifndef HOMIE_CONVENTION_VERSION
#define HOMIE_CONVENTION_VERSION 3
#endif

#if HOMIE_CONVENTION_VERSION != 3 && HOMIE_CONVENTION_VERSION != 4 && HOMIE_CONVENTION_VERSION != 5
#error "HOMIE_CONVENTION_VERSION must be 3, 4 or 5"
#endif

#if HOMIE_CONVENTION_VERSION == 4
#define HOMIE_CONVENTION_V4 1
#else
#define HOMIE_CONVENTION_V4 0
#endif

#if HOMIE_CONVENTION_VERSION == 5
#define HOMIE_CONVENTION_V5 1
#else
#define HOMIE_CONVENTION_V5 0
#endif

namespace HomieInternals {
#if HOMIE_CONVENTION_V5
const char HOMIE_VERSION[] = "5.0";
const char HOMIE_V5_RUNTIME_EXTENSION[] = "io.github.labodj.esp-runtime";
#elif HOMIE_CONVENTION_V4
const char HOMIE_VERSION[] = "4.0.0";
const char HOMIE_EXTENSIONS[] = "org.homie.legacy-firmware:0.1.1:[4.x],org.homie.legacy-stats:0.1.1:[4.x]";
#else
const char HOMIE_VERSION[] = "3.0.1";
#endif

const char HOMIE_ESP8266_VERSION[] = "3.6.1";
const char HOMIE_DEFAULT_PROPERTY_DATATYPE[] = "string";

  const IPAddress ACCESS_POINT_IP(192, 168, 123, 1);

  const uint16_t DEFAULT_MQTT_PORT = 1883;
  const char DEFAULT_MQTT_BASE_TOPIC[] = "homie/";

  const uint8_t DEFAULT_RESET_PIN = 0;  // == D3 on nodeMCU
  const uint8_t DEFAULT_RESET_STATE = LOW;
  const uint16_t DEFAULT_RESET_TIME = 5 * 1000;

  const char DEFAULT_BRAND[] = "Homie";

  const uint16_t CONFIG_SCAN_INTERVAL = 20 * 1000;
  const uint32_t STATS_SEND_INTERVAL_SEC = 1 * 60;
  const uint16_t MQTT_RECONNECT_INITIAL_INTERVAL = 1000;
  const uint8_t MQTT_RECONNECT_MAX_BACKOFF = 6;
  // If an async connect attempt does not complete within this window, treat it as stuck
  // and restart that leg of the recovery sequence from a clean state.
  const uint32_t WIFI_CONNECT_ATTEMPT_TIMEOUT = 30 * 1000UL;
  const uint32_t MQTT_CONNECT_ATTEMPT_TIMEOUT = 30 * 1000UL;
  // If the device never reaches full MQTT-ready state again, fall back to a reboot.
  const uint32_t CONNECTIVITY_RECOVERY_REBOOT_TIMEOUT = 15 * 60 * 1000UL;

  const float LED_WIFI_DELAY = 1;
  const float LED_MQTT_DELAY = 0.2;

  const char CONFIG_DIRECTORY_PATH[] = "/homie";
  const char CONFIG_UI_BUNDLE_PATH[] = "/homie/ui_bundle.gz";
  const char CONFIG_NEXT_BOOT_MODE_FILE_PATH[] = "/homie/NEXTMODE";
  const char CONFIG_FILE_PATH[] = "/homie/config.json";
}  // namespace HomieInternals
