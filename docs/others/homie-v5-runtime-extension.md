# Homie v5 fork runtime extension

Homie v5 core discovery is intentionally compact: device, node and property
metadata live in the retained `$description` JSON document. This maintained fork
still exposes OTA, configuration, firmware and runtime diagnostics topics that
come from the historical Homie for ESP8266 implementation.

In Homie v5 mode those topics are declared as an extension instead of being
treated as core Homie attributes.

## Extension ID

```text
io.github.labodj.esp-runtime
```

The device lists this identifier in `$description.extensions`. Homie v5 expects
extension entries to be extension IDs, so the runtime extension version is
documented here rather than encoded in the JSON value.

Extension version: `0.1.0`

## Topic Root

All topics below are relative to the v5 device base:

```text
<domain>/5/<device-id>
```

With the default configuration this is:

```text
homie/5/<device-id>
```

## Retained Runtime Attributes

```text
$mac
$localip
$stats
$stats/interval
$stats/signal
$stats/uptime
$stats/uptimewifi
$stats/uptimemqtt
$stats/freeheap
$stats/mqttinbounddropped
$stats/mqttackdropped
$stats/mqttinboundmaxdepth
$stats/mqttackmaxdepth
$fw/name
$fw/version
$fw/checksum
$implementation
$implementation/config
$implementation/version
$implementation/reset/reason
$implementation/wifi/last_disconnect_reason
$implementation/mqtt/last_disconnect_reason
$implementation/ota/enabled
$implementation/ota/status
```

These topics keep the existing payloads used by the Homie 3.0.1 implementation
path. They are published as retained messages so late subscribers can inspect
the latest runtime state, including the latest OTA status.

`$implementation/config` publishes the safe configuration JSON with secrets
stripped. In v5 mode its `mqtt` object also contains the generated
`effective_base_topic` field. This is the actual runtime root after the
mandatory v5 segment is applied, so a saved `mqtt.base_topic` of `homie/`
advertises `mqtt.effective_base_topic` as `homie/5/`. The field is read-only and
is removed from `/config/set` patches before they are written to the filesystem.

`$implementation/reset/reason` publishes the platform reset reason observed on
the current boot. `$implementation/wifi/last_disconnect_reason` and
`$implementation/mqtt/last_disconnect_reason` publish the most recent disconnect
reason seen during the current boot, or `none` before a disconnect has been
observed. ESP32 Wi-Fi reasons are published as platform reason names when
available. They are retained diagnostics intended for post-incident inspection.

## Command Topics

```text
$implementation/reset
$implementation/config/set
$implementation/ota/firmware/<md5 checksum>
```

`$implementation/reset` accepts `true` and causes the device to reset.

`$implementation/config/set` accepts an incremental JSON configuration patch and
clears the command topic after processing.

`$implementation/ota/firmware/<md5 checksum>` accepts the firmware payload. The
last topic level must be the lowercase or uppercase hexadecimal MD5 checksum of
the firmware image.

## OTA Status Payloads

```text
200
202
206 <bytes-written>/<bytes-total>
304
400 BAD_FIRMWARE
400 BAD_CHECKSUM
400 NOT_ENOUGH_SPACE
400 NOT_REQUESTED
403
500 FLASH_ERROR
500 INTERNAL_ERROR
```

The stable OTA updater entry point supports v5 topics with:

```bash
python scripts/homie_ota.py --homie-version 5 -i <device-id> firmware.bin
```

## Core v5 Boundaries

The extension does not redefine Homie v5 core behavior:

- discovery remains the retained `$description` JSON document
- property values are still published on
  `<domain>/5/<device-id>/<node-id>/<property-id>`
- property commands still use the non-retained `set` command topic
- strict Homie v5 consumers can ignore this extension and still discover the
  device through `$description`
