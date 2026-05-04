# Compiler flags

Compiler flags let you add, remove or tune Homie features at build time.

Use them when a firmware image is too large for OTA, when a deployment needs a
specific storage backend, or when a device needs more room for MQTT bursts. The
defaults are conservative: existing Homie 3.0.1 sketches should keep working
unless you explicitly opt in to a different behavior.

## HOMIE_CONFIG

Set `HOMIE_CONFIG=0` to disable configuration mode completely.

When configuration mode is disabled, upload `/homie/config.json` to the selected
filesystem before the device starts. SPIFFS is the default filesystem; LittleFS
is used only when `HOMIE_USE_LITTLEFS=1` is compiled in. Without a valid
configuration file, the device logs the missing configuration error and
restarts.

```ini
build_flags =
  -D HOMIE_CONFIG=0
```

This reduces the firmware size by about 50 KB.

## HOMIE_MDNS

Set `HOMIE_MDNS=0` to disable publishing the device identifier through mDNS.

```ini
build_flags =
  -D HOMIE_MDNS=0
```

This reduces the firmware size by about 6.4 KB.

## HOMIE_CONVENTION_VERSION

This maintained fork advertises the Homie `3.0.1` MQTT convention by default to
preserve existing deployments and consumers. Newer convention modes are
build-time opt-ins because they change what controllers discover on MQTT.

Set `HOMIE_CONVENTION_VERSION=4` to enable Homie `4.0.0` compatibility mode:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=4
```

In v4 mode the device publishes `$homie` as `4.0.0`, publishes the mandatory
`$extensions` attribute, and advertises the official Homie legacy firmware and
legacy stats extensions:

- `org.homie.legacy-firmware:0.1.1:[4.x]`
- `org.homie.legacy-stats:0.1.1:[4.x]`

For compatibility with existing sketches, v4 mode fills missing required
property metadata at advertisement time:

- Missing property `$name` falls back to the property id.
- Missing property `$datatype` falls back to `string`.
- Datatypes outside the Homie v4 set are advertised as `string`.
- `enum` and `color` properties without the required `$format` are advertised
  as `string`.

Those fallbacks keep old sketches discoverable, but production v4 devices should
still set explicit names, datatypes and formats for every advertised property
that needs structured controller discovery.

All convention modes abort boot if the MQTT root, device ID, firmware name, node
ID or property ID is not Homie ID-compliant. Firmware names are payload metadata,
but Homie 3.0.1 and the Homie v4 legacy firmware extension require `$fw/name` to
use the same character set as a device ID. Range nodes are also rejected in v4
mode because the library's historical range-node metadata is not part of the
Homie v4 core convention.

Set `HOMIE_CONVENTION_VERSION=5` to enable Homie `5.0` discovery:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=5
```

In v5 mode the MQTT root is versioned as required by the specification. The
default base topic `homie/` becomes `homie/5/<device-id>`. A custom base topic
must be a single Homie domain segment such as `lab/`; the firmware then
publishes under `lab/5/<device-id>`. Passing `homie/5/` is accepted and is not
rewritten to `homie/5/5/`.

The v5 mode publishes a retained `$description` JSON document instead of the
Homie 3/4 per-topic discovery attributes for nodes and properties. The document
contains:

- `homie: "5.0"`
- a deterministic numeric `version` that changes when advertised metadata
  changes
- `nodes` with node and property objects
- property `datatype`, `settable`, `retained`, `unit` and `format` metadata
- the fork runtime extension `io.github.labodj.esp-runtime`

Homie topic IDs must use lowercase letters, digits and hyphens only. Invalid
IDs abort boot because publishing them would violate the selected convention and
make discovery unreliable.

Range nodes use `node-<index>` in v5 mode because `_` is not a valid Homie v5
topic ID character. Homie 3 mode keeps the historical `node_<index>` range topic
shape.

The property retention flag advertised with `setRetained()` is the effective
runtime publish flag. When a property is advertised as non-retained, v5 property
publishes use QoS 0 even if a sketch tries to override the publish promise with
a higher QoS. This matches the Homie v5 retained-message rules for non-retained
properties.

The existing `$fw`, `$implementation`, `$stats`, OTA and configuration topics
remain available in v5 mode as fork extension topics declared in
`$description.extensions`; strict Homie v5 consumers can ignore them if they do
not implement the extension.

## HOMIE_STRICT_PROPERTY_VALIDATION

Set `HOMIE_STRICT_PROPERTY_VALIDATION=1` to make the library validate property
formats and payloads at runtime:

```ini
build_flags =
  -D HOMIE_STRICT_PROPERTY_VALIDATION=1
```

The default is `0` to keep embedded firmware small and fast. With the default
profile, the library validates the Homie structure it owns: MQTT roots, device
IDs, node IDs, property IDs, convention metadata, retain flags, v5 QoS rules and
the v5 empty-string encoding. The sketch remains responsible for making
`setDatatype()`, `setFormat()` and published values agree with the selected
Homie convention.

When strict property validation is enabled, invalid formats are omitted or
normalized where possible, outgoing property publishes are rejected if the
payload does not match the advertised datatype and format, and incoming `/set`
commands with invalid payloads are ignored before application handlers run.

## ASYNC_TCP_SSL_ENABLED

Set `ASYNC_TCP_SSL_ENABLED=1` to use SSL encryption for MQTT connections. HTTP
and OTA connections are still not encrypted by this flag.

```ini
build_flags =
  -D ASYNC_TCP_SSL_ENABLED=1
  -D PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH
```

The additional `PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH` flag is necessary
for SSL encryption to work properly on ESP8266.

## HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE

This fork exposes a build-time override for the internal queue that stores MQTT
publish acknowledgement events before `BootNormal::loop()` dispatches them.

Use it when the device emits a large retained advertisement burst and logs
`MQTT ACK queue full` during startup or reconnect:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=64
```

Default:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=32
```

## HOMIE_PENDING_MQTT_ACKS_PER_LOOP

This flag controls how many queued MQTT publish acknowledgement events are
dispatched from one `BootNormal::loop()` iteration. Raising it drains ACK bursts
faster; lowering it gives more loop fairness to sketch code and other Homie
work.

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_ACKS_PER_LOOP=8
```

Default:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_ACKS_PER_LOOP=8
```

This is an advanced tuning option. Increase it only if the default queue size is
not sufficient for your device and broker timing.

Both drop counters are exposed in Homie statistics as `$stats/mqttackdropped`
and `$stats/mqttinbounddropped`, so production devices can be monitored without
relying only on serial logs.

## HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE

This fork also exposes the size of the internal queue used to defer non-OTA MQTT
input handling from async MQTT callbacks into `BootNormal::loop()`.

Use it only when the device logs `MQTT inbound queue full` under expected broker
traffic:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=32
```

Default:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=16
```

## HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP

This flag controls how many queued non-OTA MQTT input messages are processed
from one `BootNormal::loop()` iteration. Raising it drains inbound bursts
faster; lowering it makes long command bursts yield back to the rest of the
firmware sooner.

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP=4
```

Default:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_MESSAGES_PER_LOOP=4
```

## HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED

Set this flag to `1` to make the deferred inbound MQTT queue use fixed-size
topic and payload storage instead of allocating one topic and one payload buffer
per queued message from the async MQTT callback path:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED=1
```

Default:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED=0
```

When enabled, the fixed buffers are controlled by these limits:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH=192
  -D HOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH=512
  -D HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS=12
```

Messages exceeding those limits are rejected and counted in
`$stats/mqttinbounddropped`. Keep the feature disabled on memory-constrained
ESP8266 builds unless the reserved RAM budget is acceptable.

## HOMIE_OTA_STATUS_INFO_MAX_LENGTH

This flag controls the fixed-size buffer used to queue short OTA status text
from the OTA path into the main loop before publishing the status event.
Increase it only if custom OTA status strings are being truncated.

```ini
build_flags =
  -D HOMIE_OTA_STATUS_INFO_MAX_LENGTH=48
```

Default:

```ini
build_flags =
  -D HOMIE_OTA_STATUS_INFO_MAX_LENGTH=48
```

## HOMIE_USE_LITTLEFS

This fork keeps SPIFFS as the default storage backend to preserve existing
deployments. Set this flag to use LittleFS for configuration files, next-boot
mode state, and the optional configuration UI bundle:

```ini
board_build.filesystem = littlefs
build_flags =
  -D HOMIE_USE_LITTLEFS=1
```

## HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS

Build a temporary OTA migration firmware with this flag when already provisioned
devices need to move from SPIFFS to LittleFS:

```ini
board_build.filesystem = littlefs
build_flags =
  -D HOMIE_USE_LITTLEFS=1
  -D HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS=1
```

The flag is disabled by default so normal LittleFS builds do not include SPIFFS
or migration code. A migration build tries to copy data only when LittleFS
cannot mount and a SPIFFS filesystem with `/homie/config.json` is still present.
The migration copies:

- `/homie/config.json`
- `/homie/NEXTMODE`, when present

The UI bundle at `/homie/ui_bundle.gz` is not migrated because it can be too
large to hold in RAM while LittleFS formats the shared flash area. Upload the UI
bundle again with PlatformIO `uploadfs` after switching to LittleFS.

After the device has booted once and migrated successfully, replace the
migration firmware with a normal LittleFS-only OTA build that keeps only
`HOMIE_USE_LITTLEFS=1`.

## ESP8266-only networking flags

Flags such as `PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY` and
`PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH` are relevant to the ESP8266
Arduino core. They are not part of the maintained ESP32 path of this fork.
