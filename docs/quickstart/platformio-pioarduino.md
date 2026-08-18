# PlatformIO / PioArduino

The maintained installation path for this fork is the `labodj/homie-v5`
PlatformIO Registry package. For ESP32 projects that track current Arduino
cores, use the PioArduino platform package:

```ini
[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32dev
framework = arduino
lib_compat_mode = strict
build_flags =
  -D EMC_ALLOW_NOT_CONNECTED_PUBLISH=0
lib_deps =
  labodj/homie-v5 @ ^4.0.0
```

For unreleased changes, use the git URL dependency and pin it to a commit SHA
instead of tracking the branch.

ESP8266 projects should keep the ESP8266 PlatformIO platform:

```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
lib_compat_mode = strict
build_flags =
  -D PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY
  -D EMC_ALLOW_NOT_CONNECTED_PUBLISH=0
lib_deps =
  labodj/homie-v5 @ ^4.0.0
```

`lib_compat_mode = strict` is recommended because the async networking
dependencies are platform-specific. Without strict compatibility, PlatformIO may
try to compile ESP32 or RP2040 async TCP dependencies for ESP8266 builds, or the
other way around.

The library package pins `bertmelis/espMqttClient` to an exact post-1.7.3 commit
instead of a moving tag. This keeps CI and developer builds repeatable, includes
the long-topic parser fix and directly resolves the maintained ESP32Async TCP
packages. `EMC_ALLOW_NOT_CONNECTED_PUBLISH=0` keeps disconnected publishes from
entering the client's offline outbox.

The asynchronous client does not support MQTT TLS. Do not set
`ASYNC_TCP_SSL_ENABLED`; configurations with `mqtt.ssl=true` are rejected.

SPIFFS remains the default storage backend for compatibility with existing
devices. To build and upload LittleFS images, configure both PlatformIO and
Homie:

```ini
board_build.filesystem = littlefs
build_flags =
  -D HOMIE_USE_LITTLEFS=1
```

For already provisioned SPIFFS devices, first OTA a temporary migration firmware
that adds `-D HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS=1`. After one successful boot,
OTA a normal LittleFS-only firmware without the migration flag. The migration
only covers the small state needed to keep a provisioned device online; upload
the UI bundle again with `pio run --target uploadfs` because it is intentionally
not copied through RAM during migration.

See [Maintained fork differences](../others/fork-differences.md) for the full
compatibility policy and the exact fork-only behavior.
