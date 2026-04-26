Compiler flags can be used to add or remove certain functionality from homie-esp8266.

Removing functionality can become useful if your firmware gets too large in size and is not upgradable any more over OTA. On the other hand these compiler flags allow to remove features from homie-esp8266, that you do not require or do not use.

Adding functionality or features is useful to enable only partly implemented features or unstable or experimental features.

**HOMIE_CONFIG**

This compiler flag allows to disable the configuration mode completely. To configure your homie-esp8266, you need to upload the configuration to the configured filesystem before starting the device. The default filesystem is SPIFFS, unless `HOMIE_USE_LITTLEFS=1` is set. Without a proper configuration the device will just restart after writing the error message about the missing configuration to the logger. Add the following to your platformio.ini file:

```
build_flags = -D HOMIE_CONFIG=0
```

This reduces the firmware size by about 50000 bytes.

**HOMIE_MDNS**

This compiler flag allows to disable the publishing of the device identifier via mDNS protocol. Add the following to your platformio.ini file:

```
build_flags = -D HOMIE_MDNS=0
```

This reduces the firmware size by about 6400 bytes.

**ASYNC_TCP_SSL_ENABLED**

This compiler flag allows to use SSL encryption for MQTT connections. All other network connections still can not be encrypted like HTTP or OTA.

```
build_flags =
  -D ASYNC_TCP_SSL_ENABLED=1
  -D PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH
```

The additional flag `PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH` is necessary for SSL encryption to work properly.

**HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE**

This maintained fork exposes a build-time override for the internal queue that
stores MQTT publish acknowledgement events before `BootNormal::loop()`
dispatches them.

Use it when the device emits a large retained advertisement burst and logs
`MQTT ACK queue full` during startup or reconnect:

```
build_flags =
  -D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=64
```

Default:

```
-D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=16
```

This is an advanced tuning option. Increase it only if the default queue size is
not sufficient for your device and broker timing.

Both drop counters are also exposed in Homie statistics as
`$stats/mqttackdropped` and `$stats/mqttinbounddropped`, so production devices
can be monitored without relying only on serial logs.

**HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE**

This maintained fork also exposes the size of the internal queue used to defer
non-OTA MQTT input handling from async MQTT callbacks into `BootNormal::loop()`.

Use it only when the device logs `MQTT inbound queue full` under expected broker
traffic:

```
build_flags =
  -D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=32
```

Default:

```
-D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=16
```

**HOMIE_USE_LITTLEFS**

This maintained fork keeps SPIFFS as the default storage backend to preserve
existing deployments. Set this flag to use LittleFS for configuration files,
next-boot mode state, and the optional configuration UI bundle:

```
board_build.filesystem = littlefs
build_flags =
  -D HOMIE_USE_LITTLEFS=1
```

**HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS**

Build a temporary OTA migration firmware with this flag when already provisioned
devices need to move from SPIFFS to LittleFS:

```
board_build.filesystem = littlefs
build_flags =
  -D HOMIE_USE_LITTLEFS=1
  -D HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS=1
```

The flag is disabled by default so normal LittleFS builds do not include SPIFFS
or migration code. A migration build tries to copy data only when LittleFS cannot
mount and a SPIFFS filesystem with `/homie/config.json` is still present. The
migration copies:

* `/homie/config.json`
* `/homie/NEXTMODE`, when present

The UI bundle at `/homie/ui_bundle.gz` is not migrated because it can be too
large to hold in RAM while LittleFS formats the shared flash area. Upload the UI
bundle again with PlatformIO `uploadfs` after switching to LittleFS.

After the device has booted once and migrated successfully, replace the migration
firmware with a normal LittleFS-only OTA build that keeps only
`HOMIE_USE_LITTLEFS=1`.

## ESP8266-only networking flags

Flags such as `PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY` and
`PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH` are relevant to the ESP8266
Arduino core. They are not part of the maintained ESP32 path of this fork.
