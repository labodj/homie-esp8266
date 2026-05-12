# Maintained fork differences

This repository is a maintained fork of upstream `homieiot/homie-esp8266`. The
baseline used for the current fork line is upstream commit
`ba452eae5e23d01463568756465be07f8dc3bfce` from 2023-07-24.

The compatibility rule is conservative: existing sketches and configuration
files should keep working unless a consumer explicitly enables a new
compile-time option. Fork-only behavior is documented here so consumers can
distinguish intentional maintenance work from upstream Homie behavior.

## Compatibility policy

The fork keeps the Homie 3.0.1 MQTT contract and the public sketch API intact.
Changes are additive where possible:

- SPIFFS remains the default filesystem.
- LittleFS is opt-in with `HOMIE_USE_LITTLEFS=1`.
- SPIFFS-to-LittleFS migration is a temporary OTA build option and is disabled
  by default.
- Homie `4.0.0` MQTT discovery is opt-in with `HOMIE_CONVENTION_VERSION=4`;
  Homie `3.0.1` remains the default advertised convention.
- Homie `5.0` MQTT discovery is opt-in with `HOMIE_CONVENTION_VERSION=5` and
  uses the v5 `$description` model instead of changing the default runtime.
- Queue-size overrides keep the upstream-sized default queues.
- Homie topic IDs are now validated strictly because invalid IDs create
  non-compliant MQTT topics in every convention mode.
- `setRetained()` and `overwriteSetter()` remain available for source
  compatibility, but Homie publishing follows the advertised property metadata
  and never mirrors device values onto command topics.

## Platform and dependency maintenance

The fork is maintained primarily for PlatformIO consumers, including ESP32
projects that track current Arduino cores through PioArduino:

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32dev
framework = arduino
lib_compat_mode = strict
lib_deps =
  labodj/homie-v5 @ ^3.6.1
```

Dependency metadata has been updated so PlatformIO can resolve ESP8266 and ESP32
builds with strict library compatibility. The package also pins its
AsyncMqttClient dependency to a commit hash so CI and production builds do not
silently move to a different MQTT client implementation.

The pinned `AsyncMqttClient` source is a small metadata-only fork used to point
PlatformIO at the maintained `esp32async` async TCP packages. It is not intended
to diverge from AsyncMqttClient behavior.

## Network and MQTT recovery

The normal-mode boot flow still follows upstream Homie, but connection recovery
is more defensive:

- Wi-Fi and MQTT reconnection use explicit backoff timers.
- Missed Wi-Fi and MQTT callbacks are reconciled against the current client
  state from `Homie.loop()`.
- Stalled Wi-Fi and MQTT connection attempts are forced into a clean retry.
- If the device cannot return to full `MQTT_READY` state within the recovery
  window, it schedules a reboot to recover the networking stack.

This avoids relying exclusively on asynchronous network callbacks, which can be
missed or delayed on newer ESP32/ESP8266 Arduino core combinations.

## Async callback dispatch

Several events that upstream handled directly from asynchronous callbacks are
now queued and dispatched from the main `Homie.loop()` flow:

- Wi-Fi connected/disconnected
- MQTT connected/disconnected
- MQTT publish acknowledgements
- non-OTA inbound MQTT messages
- OTA started/progress/success/failure notifications

The purpose is to keep user callbacks, Homie event handlers, and most MQTT input
processing on one predictable execution path. This reduces races between async
network callbacks and sketch code.

## MQTT queue tuning and diagnostics

The fork exposes queue sizing and allocation flags for unusual MQTT bursts:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=64
  -D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=32
  -D HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED=1
```

Defaults:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=32
  -D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=16
  -D HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED=0
  -D HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH=192
  -D HOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH=512
  -D HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS=12
```

Use larger queues only when production devices log queue-full warnings under
expected traffic. Queue drops are also published as retained Homie statistics:

- `$stats/mqttackdropped`
- `$stats/mqttinbounddropped`

Those counters are cumulative for the current boot and are intended for fleet
monitoring.

When `HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED=1`, queued inbound MQTT messages
use fixed-size slot storage. This removes per-message heap allocation from the
async MQTT callback path at the cost of reserved RAM and explicit topic/payload
size limits.

## OTA hardening

MQTT OTA handling is more robust for QoS 1 delivery and disconnect scenarios:

- duplicate retransmissions of an already flashed payload are ignored safely.
- overlapping retransmitted chunks are trimmed before writing to flash.
- out-of-sequence chunks fail explicitly.
- MQTT disconnect during OTA aborts the update and requires a retry.

The OTA helper script was also modernized, but the MQTT OTA topic contract is
kept compatible with Homie 3.0.1.

## Filesystem selection and migration

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

- `/homie/config.json`
- `/homie/NEXTMODE`, when present and small enough for the bounded buffer

The UI bundle at `/homie/ui_bundle.gz` is intentionally not migrated because it
can be too large to copy through RAM while both filesystems share the same flash
area. Upload it again with `pio run --target uploadfs`.

After one successful boot with the migration firmware, OTA a normal LittleFS
firmware without `HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS`. That removes the SPIFFS
reader and migration code from the production image.

## Public API extensions

`PropertyInterface::setRetained()` controls whether an advertised property is
retained. That advertised property flag is also the effective MQTT retain flag
used by property publishes.

`SendingPromise::setRetained()` and `SendingPromise::overwriteSetter()` are kept
for older sketches that already call them. `send()` ignores contradictory retain
overrides and ignores `overwriteSetter(true)` because Homie command topics are
reserved for controllers.

## Homie v4 compatibility

The fork can advertise Homie `4.0.0` metadata when built with:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=4
```

This is intentionally compile-time only, so deployed Homie `3.0.1` devices and
their consumers do not change behavior accidentally. In v4 mode the device:

- publishes `$homie` as `4.0.0`
- publishes the mandatory `$extensions` attribute
- declares `org.homie.legacy-firmware:0.1.1:[4.x]`
- declares `org.homie.legacy-stats:0.1.1:[4.x]`
- keeps the existing legacy firmware, local IP, MAC and stats topics

Homie v4 requires property `$name` and `$datatype`. Older sketches did not
always set those fields, so v4 mode publishes safe fallbacks during discovery:
the property id as `$name`, and `string` as `$datatype`. Invalid datatypes and
`enum`/`color` properties without the required format are also advertised as
`string`. Explicit metadata in the sketch always wins and is still recommended
for production firmware.

All convention modes abort boot for invalid MQTT roots, device IDs, firmware
names, node IDs or property IDs. Homie v4 mode also rejects range nodes because
the historical `$array` range metadata is not part of Homie v4 core discovery.

## Homie v5 compatibility

The fork can publish Homie `5.0` discovery metadata when built with:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=5
```

In v5 mode the device:

- publishes under `<domain>/5/<device-id>`
- publishes retained `$state` and `$description` topics
- places device, node and property metadata in `$description`
- uses `node-<index>` for range nodes instead of the legacy `node_<index>`
- ignores retained `/set` commands because Homie v5 command messages must be
  non-retained
- publishes non-retained property values with QoS 0, as required by Homie v5
- can validate property publishes and `/set` command payloads against the
  advertised datatype and format when `HOMIE_STRICT_PROPERTY_VALIDATION=1`
- adds `mqtt.effective_base_topic` to the advertised safe config so diagnostics
  show the real v5 runtime root while the saved `mqtt.base_topic` remains stable

The historical OTA, configuration, firmware and stats topics remain available as
the declared extension `io.github.labodj.esp-runtime`. This keeps existing
operational tooling usable while keeping Homie v5 core discovery strictly
separated from fork-specific runtime topics.

## Statistics

The fork publishes the standard Homie statistics plus these retained topics:

- `$stats/freeheap`: current free heap in bytes
- `$stats/uptimewifi`: seconds since Wi-Fi connectivity was established
- `$stats/uptimemqtt`: seconds since MQTT connectivity was established
- `$stats/mqttackdropped`: cumulative MQTT publish acknowledgement queue drops
- `$stats/mqttinbounddropped`: cumulative deferred inbound MQTT queue drops

## Documentation status

Some legacy pages still intentionally preserve upstream wording for historical
API behavior. When a behavior is fork-specific, prefer this page, the
PlatformIO/PioArduino quickstart, the compiler-flags page, and the
implementation specifics page in this repository.
