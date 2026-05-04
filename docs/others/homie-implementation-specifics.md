# Homie implementation specifics

The Homie `$implementation` identifier is platform-dependent on this fork:

- `esp32` on ESP32 builds
- `esp8266` on ESP8266 builds

## Version

- `$implementation/version`: maintained fork version
- `$implementation/reset/reason`: reset reason reported by the platform on the
  current boot, for example `poweron`, `software`, `brownout`, `task_watchdog`
  or the platform-provided ESP8266 reset string
- `$implementation/wifi/last_disconnect_reason`: Wi-Fi disconnect reason
  observed during the current boot, or `none`. ESP32 builds publish the platform
  reason name when available, for example `NO_AP_FOUND`.
- `$implementation/mqtt/last_disconnect_reason`: MQTT disconnect reason observed
  during the current boot, or `none`

## Homie convention advertisement

The maintained fork advertises Homie `3.0.1` by default. Build with
`HOMIE_CONVENTION_VERSION=4` to advertise Homie `4.0.0`, or with
`HOMIE_CONVENTION_VERSION=5` to publish Homie `5.0` discovery metadata.

In Homie v4 mode the device publishes:

- `$homie`: `4.0.0`
- `$extensions`:
  `org.homie.legacy-firmware:0.1.1:[4.x],org.homie.legacy-stats:0.1.1:[4.x]`

The legacy extensions cover the existing `$mac`, `$localip`, `$fw/name`,
`$fw/version`, `$stats/interval`, `$stats/uptime`, `$stats/signal` and
`$stats/freeheap` topics. Fork-specific OTA and diagnostics topics remain
documented in this page.

All convention modes abort boot for invalid MQTT roots, device IDs, firmware
names, node IDs or property IDs. Homie v4 mode also rejects range nodes. Missing
property names and datatypes still use safe discovery fallbacks, while invalid
datatypes and `enum`/`color` properties without the required format are
advertised as `string`.

In Homie v5 mode the device base topic is `<domain>/5/<device-id>`, for example
`homie/5/kitchen-light`. Device, node and property discovery is published as a
retained `$description` JSON document. The existing runtime topics in this page
remain available as the declared fork extension:

```text
io.github.labodj.esp-runtime
```

See [Homie v5 fork runtime extension](homie-v5-runtime-extension.md) for the
extension contract. These topics are intentionally documented as an extension in
v5 mode because they are not part of Homie v5 core discovery.

Non-retained property publishes use QoS 0 in v5 mode. Retained `/set` commands
are ignored in both v4 and v5 modes because command messages must be
non-retained. By default, exact property format and payload correctness is the
sketch's responsibility; enable `HOMIE_STRICT_PROPERTY_VALIDATION=1` to have the
library reject invalid property publishes and `/set` command payloads.

## Reset

- `$implementation/reset`: You can publish `true` to this topic to reset the
  device

## Configuration

- `$implementation/config`: the `config.json` is published there, with
  `wifi.password`, `mqtt.username` and `mqtt.password` fields stripped
- `$implementation/config/set`: you can update the `config.json` by sending
  incremental JSON on this topic

In Homie v5 mode the advertised `$implementation/config` also includes
`mqtt.effective_base_topic`. This field is generated at publish time and shows
the actual MQTT root used by the runtime, for example `homie/5/` when the saved
`mqtt.base_topic` is `homie/`. It is diagnostic only: it is not written to
`/homie/config.json`, and `/config/set` removes it if a management tool echoes
the advertised config back to the device.

## OTA

- `$implementation/ota/enabled`: `true` if OTA is enabled, `false` otherwise
- `$implementation/ota/firmware/<md5 checksum>`: send the firmware payload to
  this topic, where the last topic level is the hexadecimal MD5 checksum of the
  firmware image
- `$implementation/ota/status`: HTTP-like status code indicating the status of
  the OTA. Common values are:

- `200`: OTA successfully flashed.
- `202`: OTA request or checksum accepted.
- `206 465/349680`: OTA in progress. The data after the status code corresponds
  to `<bytes written>/<bytes total>`.
- `304`: The current firmware is already up-to-date.
- `400 BAD_FIRMWARE`: the OTA request is invalid. The identifier might be
  `BAD_FIRMWARE`, `BAD_CHECKSUM`, `NOT_ENOUGH_SPACE` or `NOT_REQUESTED`.
- `403`: OTA is not enabled.
- `500 FLASH_ERROR`: the device flash/update path failed. The identifier might
  be `FLASH_ERROR`.

On this fork, the OTA handler is hardened for MQTT QoS 1 delivery:

- duplicate retransmissions of already-flashed payloads are ignored safely
- overlapping retransmitted chunks are trimmed before flashing
- out-of-sequence chunks fail explicitly instead of silently corrupting the
  update
- if MQTT disconnects during OTA, the update is aborted cleanly and must be
  retried

## Filesystem

SPIFFS remains the default storage backend for compatibility with existing
devices. LittleFS is selected only when `HOMIE_USE_LITTLEFS=1` is compiled into
the firmware. A temporary migration build can add
`HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS=1` to copy `/homie/config.json` and
`/homie/NEXTMODE` from SPIFFS to LittleFS on first boot.

The UI bundle is not migrated because SPIFFS and LittleFS share the same flash
area and the bundle can be too large to hold in RAM while the destination
filesystem is formatted.

## Statistics

The fork publishes the standard Homie statistics plus these additional retained
topics:

- `$stats/freeheap`: current free heap in bytes
- `$stats/uptimewifi`: seconds since Wi-Fi connectivity was established
- `$stats/uptimemqtt`: seconds since MQTT connectivity was established
- `$stats/mqttackdropped`: cumulative MQTT publish acknowledgement queue drops
- `$stats/mqttinbounddropped`: cumulative deferred inbound MQTT queue drops
- `$stats/mqttackmaxdepth`: maximum MQTT publish acknowledgement queue depth
  since boot
- `$stats/mqttinboundmaxdepth`: maximum deferred inbound MQTT queue depth since
  boot
