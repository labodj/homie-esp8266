# Maintained Fork Differences

This repository is a maintained fork of upstream `homieiot/homie-esp8266`.
The baseline used for the current fork line is upstream commit
`ba452eae5e23d01463568756465be07f8dc3bfce` from 2023-07-24.

The compatibility rule is conservative: existing sketches and configuration
files should keep working unless a consumer explicitly enables a new compile-time
option. Fork-only behavior is documented here so consumers can distinguish
intentional maintenance work from upstream Homie behavior.

## Compatibility Policy

The fork keeps the Homie 3.0.1 MQTT contract and the public sketch API intact.
Changes are additive where possible:

* SPIFFS remains the default filesystem.
* LittleFS is opt-in with `HOMIE_USE_LITTLEFS=1`.
* SPIFFS-to-LittleFS migration is a temporary OTA build option and is disabled by
  default.
* queue-size overrides keep the upstream-sized default queues.
* ID validation warnings do not reject legacy node, property, or device IDs.
* `setSetRetained()` only extends `overwriteSetter(true)` behavior.

## Platform And Dependency Maintenance

The fork is maintained primarily for PlatformIO consumers, including ESP32
projects that track current Arduino cores through PioArduino:

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
framework = arduino
lib_compat_mode = strict
lib_deps =
  https://github.com/labodj/homie-esp8266.git#develop
```

Dependency metadata has been updated so PlatformIO can resolve ESP8266 and ESP32
builds with strict library compatibility. The package also pins its
AsyncMqttClient dependency to a commit hash so CI and production builds do not
silently move to a different MQTT client implementation.

## Network And MQTT Recovery

The normal-mode boot flow still follows upstream Homie, but connection recovery
is more defensive:

* Wi-Fi and MQTT reconnection use explicit backoff timers.
* missed Wi-Fi and MQTT callbacks are reconciled against the current client
  state from `Homie.loop()`.
* stalled Wi-Fi and MQTT connection attempts are forced into a clean retry.
* if the device cannot return to full `MQTT_READY` state within the recovery
  window, it schedules a reboot to recover the networking stack.

This avoids relying exclusively on asynchronous network callbacks, which can be
missed or delayed on newer ESP32/ESP8266 Arduino core combinations.

## Async Callback Dispatch

Several events that upstream handled directly from asynchronous callbacks are
now queued and dispatched from the main `Homie.loop()` flow:

* Wi-Fi connected/disconnected
* MQTT connected/disconnected
* MQTT publish acknowledgements
* non-OTA inbound MQTT messages
* OTA started/progress/success/failure notifications

The purpose is to keep user callbacks, Homie event handlers, and most MQTT input
processing on one predictable execution path. This reduces races between async
network callbacks and sketch code.

## MQTT Queue Tuning And Diagnostics

The fork exposes two internal queue sizes:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=64
  -D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=32
```

Defaults:

```ini
-D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=16
-D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=16
```

Use larger queues only when production devices log queue-full warnings under
expected traffic. Queue drops are also published as retained Homie statistics:

* `$stats/mqttackdropped`
* `$stats/mqttinbounddropped`

Those counters are cumulative for the current boot and are intended for fleet
monitoring.

## OTA Hardening

MQTT OTA handling is more robust for QoS 1 delivery and disconnect scenarios:

* duplicate retransmissions of an already flashed payload are ignored safely.
* overlapping retransmitted chunks are trimmed before writing to flash.
* out-of-sequence chunks fail explicitly.
* MQTT disconnect during OTA aborts the update and requires a retry.

The OTA helper script was also modernized, but the MQTT OTA topic contract is
kept compatible with Homie 3.0.1.

## Filesystem Selection And Migration

SPIFFS is still the default to protect deployed devices. LittleFS is selected
only when the consumer builds with:

```ini
board_build.filesystem = littlefs
build_flags =
  -D HOMIE_USE_LITTLEFS=1
```

Already provisioned devices need one temporary OTA firmware:

```ini
board_build.filesystem = littlefs
build_flags =
  -D HOMIE_USE_LITTLEFS=1
  -D HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS=1
```

The migration build first tries to mount LittleFS. If LittleFS cannot mount and
SPIFFS still contains `/homie/config.json`, the firmware copies the small Homie
state into RAM, formats/mounts LittleFS, and writes it back. Migrated files:

* `/homie/config.json`
* `/homie/NEXTMODE`, when present and small enough for the bounded buffer

The UI bundle at `/homie/ui_bundle.gz` is intentionally not migrated because it
can be too large to copy through RAM while both filesystems share the same flash
area. Upload it again with `pio run --target uploadfs`.

After one successful boot with the migration firmware, OTA a normal LittleFS
firmware without `HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS`. That removes the SPIFFS
reader and migration code from the production image.

## Public API Extensions

`PropertyInterface::setRetained()` controls whether an advertised property is
retained and whether `HomieNode::setProperty()` starts from a retained or
non-retained publish default for that property.

`SendingPromise::setSetRetained()` is fork-specific. It controls only the
mirrored `/set` publish performed by `overwriteSetter(true)`. The main property
publish remains controlled by `SendingPromise::setRetained()`.

## Statistics

The fork publishes the standard Homie statistics plus these retained topics:

* `$stats/freeheap`: current free heap in bytes
* `$stats/uptimewifi`: seconds since Wi-Fi connectivity was established
* `$stats/uptimemqtt`: seconds since MQTT connectivity was established
* `$stats/mqttackdropped`: cumulative MQTT publish acknowledgement queue drops
* `$stats/mqttinbounddropped`: cumulative deferred inbound MQTT queue drops

## Documentation Status

Some legacy pages still intentionally preserve upstream wording for historical
API behavior. When a behavior is fork-specific, prefer this page, the
PlatformIO/PioArduino quickstart, the compiler-flags page, and the implementation
specifics page in this repository.
