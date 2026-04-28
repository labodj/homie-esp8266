# Homie for ESP8266 / ESP32

![homie-esp8266 banner][banner]

An Arduino for ESP8266 / ESP32 implementation of [Homie](https://github.com/homieiot/convention), an MQTT convention for the IoT.

This fork keeps the original Homie 3.0.1 API and behavior as intact as possible, with targeted fixes and maintenance work for newer ESP32 / ESP8266 Arduino environments.

This branch of Homie for ESP8266 implements [Homie 3.0.1](https://github.com/homieiot/convention/releases/tag/v3.0.1), adds support for ESP32, and includes opt-in Homie 4.0.0 and Homie 5.0 discovery modes for consumers that explicitly build with `HOMIE_CONVENTION_VERSION=4` or `HOMIE_CONVENTION_VERSION=5`.

[![works with MQTT Homie](https://homieiot.github.io/img/works-with-homie.svg "works with MQTT Homie")](https://homieiot.github.io/)

## Download

This repository contains the maintained development branch of the fork.
The supported consumption path is the `labodj/homie-v5` PlatformIO Registry
package. Use a git dependency only when testing unreleased `develop` changes.

## Documentation

Fork documentation is published at:

- https://labodj.github.io/homie-esp8266/

Key pages:

- [Getting started][docs-getting-started]
- [PlatformIO / PioArduino setup][docs-platformio-pioarduino]
- [JSON configuration file][docs-json-config]
- [HTTP JSON API][docs-http-api]
- [OTA over MQTT][docs-ota]
- [Maintained fork differences][docs-fork-differences]
- [Implementation specifics][docs-implementation-specifics]
- [Homie v5 runtime extension][docs-v5-runtime-extension]

The generated site reflects the maintained fork. When a fork-specific page differs
from upstream, prefer the fork site and the documents tracked in this repository.

## Recovery Policy

The normal-mode flow stays close to upstream Homie, but reconnect handling is stricter on this fork:

- Wi-Fi and MQTT reconnect attempts are driven by explicit backoff timers instead of relying on the network stack alone
- Missed Wi-Fi or MQTT disconnect/connect callbacks are reconciled against the current client state, so the internal Homie state can self-heal
- A Wi-Fi or MQTT connect attempt that stays pending for more than 30 seconds is treated as stuck and restarted from a clean state
- If the device cannot get back to full `MQTT_READY` state for 15 minutes, it schedules a reboot to recover the network stack

These values are defined in `src/Homie/Constants.hpp`.

## Using with PlatformIO

[PlatformIO](http://platformio.org) is an open source ecosystem for IoT development with a cross-platform build system, library manager and full support for Espressif Arduino development on both ESP8266 and ESP32. It works on the popular host OS: Mac OS X, Windows, Linux 32/64, Linux ARM (like Raspberry Pi, BeagleBone, CubieBoard).

1. Install [PlatformIO IDE](http://platformio.org/platformio-ide)
2. Create new project using "PlatformIO Home > New Project"
3. Open [Project Configuration File `platformio.ini`](http://docs.platformio.org/page/projectconf.html)

### Maintained fork

4. Add `labodj/homie-v5` to project using `platformio.ini` and [lib_deps](http://docs.platformio.org/page/projectconf/section_env_library.html#lib-deps) option:

```ini
[env:myboard]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = ...
framework = arduino
lib_compat_mode = strict
lib_deps = labodj/homie-v5 @ ^3.4.0
```

For ESP8266 consumers, keep using the ESP8266 PlatformIO platform and add
`PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY` if your network stack needs it for
reliable OTA behavior. That flag is not part of the maintained ESP32 path.

If you need unreleased changes from `develop`, use the git dependency and pin a
commit SHA instead of the branch name in `lib_deps`.

The PlatformIO package currently pins a small fork of `AsyncMqttClient`. That
fork only updates the async TCP dependency metadata to the maintained
`esp32async` packages required by modern ESP8266 / ESP32 Arduino toolchains.

### Compile-time tuning

This fork exposes a small number of internal queue sizes as build-time overrides
for advanced consumers that need to absorb larger MQTT bursts during startup or
reconnect.

The queue overrides are:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=64
  -D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=32
  -D HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED=1
```

Defaults:

```ini
-D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=32
-D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=16
-D HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED=0
-D HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LENGTH=192
-D HOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH=512
-D HOMIE_PENDING_MQTT_MESSAGE_MAX_TOPIC_LEVELS=12
```

The ACK queue stores MQTT publish acknowledgement events before
`BootNormal::loop()` dispatches them. The message queue defers non-OTA MQTT input
from async callbacks into the main loop. Increase them only when the device logs
the related queue warning under expected traffic. Advanced ESP32 consumers can
enable the preallocated message queue to avoid per-message heap allocation in
the async MQTT callback path; messages exceeding the configured topic, payload
or topic-level limits are rejected and counted as inbound drops.

Storage defaults to SPIFFS for backward compatibility. LittleFS can be enabled
explicitly:

```ini
board_build.filesystem = littlefs
build_flags =
  -D HOMIE_USE_LITTLEFS=1
```

For already provisioned devices, build one temporary OTA migration firmware with:

```ini
board_build.filesystem = littlefs
build_flags =
  -D HOMIE_USE_LITTLEFS=1
  -D HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS=1
```

That firmware reads SPIFFS `/homie/config.json` and `/homie/NEXTMODE` into RAM,
formats and mounts LittleFS, then writes the migrated files back. The UI bundle
is not migrated because it can be too large to copy safely in RAM; upload it
again with `pio run --target uploadfs`. After the device has booted once with
the migration firmware, OTA a normal LittleFS-only firmware without
`HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS`.

Homie 3.0.1 remains the default advertised MQTT convention. To opt into Homie
4.0.0 discovery metadata, build with:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=4
```

The v4 mode publishes the mandatory `$extensions` topic and declares the
official legacy firmware and stats extensions so the existing `$fw`, `$mac`,
`$localip` and `$stats` topics remain documented by the Homie v4 ecosystem.
Older sketches that omitted property names or datatypes still advertise in v4
mode through conservative fallbacks, but production firmware should set
`setName()` and `setDatatype()` explicitly for every advertised property.

To opt into Homie 5.0 discovery metadata, build with:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=5
```

Homie v5 mode publishes under `homie/5/<device-id>` by default and uses a
retained `$description` JSON document for discovery. The historical OTA,
configuration, firmware and statistics topics remain available as the declared
fork extension `io.github.labodj.esp-runtime`. Device, node and
property IDs must be valid Homie v5 IDs: lowercase letters, digits and hyphens.

## Features

- Automatic connection/reconnection to Wi-Fi/MQTT
- [JSON configuration file][docs-json-config] to configure the device
- [Cute HTTP API / Web UI / App][docs-http-api] to remotely send the configuration to the device and get information about it
- [Custom settings][docs-custom-settings]
- [OTA over MQTT][docs-ota]
- [Magic bytes][docs-magic-bytes]
- Pretty [straightforward sketches][examples], a simple light for example:

```c++
#include <Homie.h>

const int PIN_RELAY = 5;

HomieNode lightNode("light", "Light", "switch");

bool lightOnHandler(const HomieRange& range, const String& value) {
  if (value != "true" && value != "false") return false;

  bool on = (value == "true");
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  lightNode.setProperty("on").send(value);
  Homie.getLogger() << "Light is " << (on ? "on" : "off") << endl;

  return true;
}

void setup() {
  Serial.begin(115200);
  Serial << endl << endl;
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  Homie_setFirmware("awesome-relay", "1.0.0");

  lightNode.advertise("on").setName("On").setDatatype("boolean").settable(lightOnHandler);

  Homie.setup();
}

void loop() {
  Homie.loop();
}
```

## Requirements, installation and usage

The project documentation in this repository is still derived from upstream
Homie and is being updated incrementally for this fork. For the maintained path
today, prefer the PlatformIO Registry package described above.

Fork-specific behavior already documented in this repository includes:

- a maintained fork differences page with the upstream baseline and compatibility policy
- stricter Wi-Fi / MQTT recovery on ESP32 and ESP8266
- LittleFS opt-in builds and SPIFFS-to-LittleFS OTA migration
- deferred async MQTT/event dispatch and queue tuning flags
- ESP32-aware `$implementation` reporting (`esp32` on ESP32 builds, `esp8266` on ESP8266 builds)
- OTA delivery hardening for QoS 1 retransmits and MQTT disconnects
- opt-in Homie 4.0.0 convention advertisement with official legacy firmware and
  legacy stats extensions
- fork-specific statistics such as `$stats/uptimewifi`, `$stats/uptimemqtt`,
  `$stats/mqttackdropped` and `$stats/mqttinbounddropped`

[banner]: https://raw.githubusercontent.com/labodj/homie-esp8266/develop/banner.png
[docs-getting-started]: https://labodj.github.io/homie-esp8266/quickstart/getting-started/
[docs-platformio-pioarduino]: https://labodj.github.io/homie-esp8266/quickstart/platformio-pioarduino/
[docs-json-config]: https://labodj.github.io/homie-esp8266/configuration/json-configuration-file/
[docs-http-api]: https://labodj.github.io/homie-esp8266/configuration/http-json-api/
[docs-ota]: https://labodj.github.io/homie-esp8266/others/ota-configuration-updates/
[docs-fork-differences]: https://labodj.github.io/homie-esp8266/others/fork-differences/
[docs-implementation-specifics]: https://labodj.github.io/homie-esp8266/others/homie-implementation-specifics/
[docs-v5-runtime-extension]: https://labodj.github.io/homie-esp8266/others/homie-v5-runtime-extension/
[docs-custom-settings]: https://labodj.github.io/homie-esp8266/advanced-usage/custom-settings/
[docs-magic-bytes]: https://labodj.github.io/homie-esp8266/advanced-usage/magic-bytes/
[examples]: https://github.com/labodj/homie-esp8266/tree/develop/examples
