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

**HOMIE_CONVENTION_VERSION**

This maintained fork advertises the Homie `3.0.1` MQTT convention by default to
preserve existing deployments and consumers. Newer convention modes are
build-time opt-ins because they change what controllers discover on MQTT.

Set `HOMIE_CONVENTION_VERSION=4` to enable the opt-in Homie `4.0.0`
compatibility mode:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=4
```

In v4 mode the device publishes `$homie` as `4.0.0`, publishes the mandatory
`$extensions` attribute, and advertises the official Homie legacy firmware and
legacy stats extensions:

- `org.homie.legacy-firmware:0.1.1:[4.x]`
- `org.homie.legacy-stats:0.1.1:[4.x]`

For compatibility with existing sketches, v4 mode also fills missing required
property metadata at advertisement time:

- missing property `$name` falls back to the property id.
- missing property `$datatype` falls back to `string`.

Those fallbacks keep old sketches discoverable, but production v4 devices should
still set explicit names and datatypes for every advertised property.

Set `HOMIE_CONVENTION_VERSION=5` to enable Homie `5.0` discovery:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=5
```

In v5 mode the MQTT root is versioned as required by the specification. The
default base topic `homie/` becomes `homie/5/<device-id>`. A custom base topic
must be a single Homie domain segment such as `lab/`; the firmware then publishes
under `lab/5/<device-id>`. Passing `homie/5/` is also accepted and is not
rewritten to `homie/5/5/`.

The v5 mode publishes a retained `$description` JSON document instead of the
Homie 3/4 per-topic discovery attributes for nodes and properties. The document
contains:

- `homie: "5.0"`
- a deterministic numeric `version` that changes when advertised metadata changes
- `nodes` with node and property objects
- property `datatype`, `settable`, `retained`, `unit` and `format` metadata
- the fork runtime extension:
  `io.github.labodj.esp-runtime`

Homie v5 topic IDs are stricter than older deployments. Device, node and
property IDs must use lowercase letters, digits and hyphens only. Invalid IDs
abort boot in v5 mode instead of being kept as compatibility warnings, because
publishing invalid IDs would violate the v5 topic rules.

Range nodes use `node-<index>` in v5 mode because `_` is not a valid Homie v5
topic ID character. Homie 3/4 modes keep the historical `node_<index>` range
topic shape.

The existing `$fw`, `$implementation`, `$stats`, OTA and configuration topics
remain available in v5 mode as fork extension topics declared in
`$description.extensions`; strict Homie v5 consumers can ignore them if they do
not implement the extension.

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

- `/homie/config.json`
- `/homie/NEXTMODE`, when present

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
