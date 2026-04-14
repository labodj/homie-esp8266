# Homie for ESP8266 / ESP32

![homie-esp8266 banner](banner.png)

An Arduino for ESP8266 / ESP32 implementation of [Homie](https://github.com/homieiot/convention), an MQTT convention for the IoT.

This fork keeps the original Homie 3.0.1 API and behavior as intact as possible, with targeted fixes and maintenance work for newer ESP32 / ESP8266 Arduino environments.

This branch of Homie for ESP8266 implements [Homie 3.0.1](https://github.com/homieiot/convention/releases/tag/v3.0.1) and adds support for ESP32.

[![works with MQTT Homie](https://homieiot.github.io/img/works-with-homie.svg "works with MQTT Homie")](https://homieiot.github.io/)

## Download

This repository contains the maintained development branch of the fork.
The supported consumption path is a git dependency from PlatformIO.

## Recovery Policy

The normal-mode flow stays close to upstream Homie, but reconnect handling is stricter on this fork:

* Wi-Fi and MQTT reconnect attempts are driven by explicit backoff timers instead of relying on the network stack alone
* Missed Wi-Fi or MQTT disconnect/connect callbacks are reconciled against the current client state, so the internal Homie state can self-heal
* A Wi-Fi or MQTT connect attempt that stays pending for more than 30 seconds is treated as stuck and restarted from a clean state
* If the device cannot get back to full `MQTT_READY` state for 15 minutes, it schedules a reboot to recover the network stack

These values are defined in `src/Homie/Constants.hpp`.


## Using with PlatformIO

[PlatformIO](http://platformio.org) is an open source ecosystem for IoT development with a cross-platform build system, library manager and full support for Espressif Arduino development on both ESP8266 and ESP32. It works on the popular host OS: Mac OS X, Windows, Linux 32/64, Linux ARM (like Raspberry Pi, BeagleBone, CubieBoard).

1. Install [PlatformIO IDE](http://platformio.org/platformio-ide)
2. Create new project using "PlatformIO Home > New Project"
3. Open [Project Configuration File `platformio.ini`](http://docs.platformio.org/page/projectconf.html)

### Maintained fork

4. Add "Homie" to project using `platformio.ini` and [lib_deps](http://docs.platformio.org/page/projectconf/section_env_library.html#lib-deps) option:
```ini
[env:myboard]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = ...
framework = arduino
lib_deps = https://github.com/labodj/homie-esp8266.git#develop
```

For ESP8266 consumers, keep using the ESP8266 PlatformIO platform and add
`PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY` if your network stack needs it for
reliable OTA behavior. That flag is not part of the maintained ESP32 path.

If you need reproducible builds, pin a commit SHA instead of the branch name in `lib_deps`.

## Features

* Automatic connection/reconnection to Wi-Fi/MQTT
* [JSON configuration file](./docs/configuration/json-configuration-file.md) to configure the device
* [Cute HTTP API / Web UI / App](./docs/configuration/http-json-api.md) to remotely send the configuration to the device and get information about it
* [Custom settings](./docs/advanced-usage/custom-settings.md)
* [OTA over MQTT](./docs/others/ota-configuration-updates.md)
* [Magic bytes](./docs/advanced-usage/magic-bytes.md)
* Pretty [straightforward sketches](./examples), a simple light for example: (**TODO**: adapt to V3)

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

The project documentation in this repository is still derived from upstream Homie and is being updated incrementally for this fork. For the maintained path today, prefer the PlatformIO git dependency described above.

Fork-specific behavior already documented in this repository includes:

* stricter Wi-Fi / MQTT recovery on ESP32 and ESP8266
* ESP32-aware `$implementation` reporting (`esp32` on ESP32 builds, `esp8266` on ESP8266 builds)
* OTA delivery hardening for QoS 1 retransmits and MQTT disconnects
* fork-specific statistics such as `$stats/uptimewifi` and `$stats/uptimemqtt`

## Donate

I am a student and maintaining Homie for ESP8266 takes time. **I am not in need and I will continue to maintain this project as much as I can even without donations**. Consider this as a way to tip the project if you like it. :wink:

[![Donate button](https://www.paypal.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=JSGTYJPMNRC74)
