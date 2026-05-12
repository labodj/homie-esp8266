# Getting started

This _Getting Started_ guide assumes you have a board with a user-configurable
LED and a user-programmable button, like a NodeMCU DevKit 1.0 or a typical ESP32
dev board. These restrictions can be lifted (see next pages).

This page stays close to the original upstream guide, so some examples are still
ESP8266-centric. Use it as a walkthrough of the Homie programming flow; for the
maintained installation path, start from the PlatformIO examples below or the
dedicated [PlatformIO / PioArduino](platformio-pioarduino.md) page.

The maintained installation path for this fork is PlatformIO with the
`labodj/homie-v5` Registry package. The Arduino IDE / ZIP flow below is legacy
upstream documentation and is not the recommended path for this fork. See
[PlatformIO / PioArduino](platformio-pioarduino.md) for the maintained ESP32 and
ESP8266 project configuration examples.

To use this fork, you will typically need:

- An ESP32 or ESP8266 board
- PlatformIO for the maintained path, or the Arduino IDE for legacy
  upstream-style setups
- Basic knowledge of the Arduino environment, such as uploading sketches and
  importing libraries
- To understand [the Homie convention](https://github.com/homieiot/convention)

## Installing Homie for ESP8266

There are two ways to install Homie for ESP8266.

### 1a. With [PlatformIO](http://platformio.org)

This fork is consumed through the `labodj/homie-v5` PlatformIO Registry package.
For an ESP32 starting point, add this to your `platformio.ini`:

```ini
[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32dev
framework = arduino
lib_compat_mode = strict
lib_deps =
  labodj/homie-v5 @ ^3.6.1
```

ESP8266 projects should keep the ESP8266 PlatformIO platform and can use the
example in [PlatformIO / PioArduino](platformio-pioarduino.md) as a template.

If you need unreleased changes from `develop`, use the git dependency and pin a
commit SHA instead of the branch name.

Dependencies are installed automatically.

### 1b. For the Arduino IDE

This section is kept only as upstream legacy guidance. It is not the recommended
installation path for this fork, and its dependency list may not match the
maintained PlatformIO path.

There is a YouTube video with instructions:

[![YouTube logo](../assets/youtube.png) How to install Homie libraries on Arduino IDE](https://www.youtube.com/watch?v=bH3KfFfYUvg)

1. Download an upstream release archive compatible with your target environment

2. Load the `.zip` with **Sketch → Include Library → Add .ZIP Library**

Upstream Homie for ESP8266 has 5 dependencies:

- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) >= 5.0.8
- [Bounce2](https://github.com/thomasfredericks/Bounce2)
- [ESPAsyncTCP](https://github.com/me-no-dev/ESPAsyncTCP) >=
  [c8ed544](https://github.com/me-no-dev/ESPAsyncTCP)
- [AsyncMqttClient](https://github.com/marvinroger/async-mqtt-client)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)

Some of them are available through the Arduino IDE, with **Sketch → Include
Library → Manage Libraries**. For the others, install it by downloading the
`.zip` on GitHub.

## Bare minimum sketch

```c++
#include <Homie.h>

void setup() {
  Serial.begin(115200);
  Serial << endl << endl;

  // The underscore is not a typo. See Magic bytes.
  Homie_setFirmware("bare-minimum", "1.0.0");
  Homie.setup();
}

void loop() {
  Homie.loop();
}
```

This is the bare minimum needed for Homie for ESP8266 to work correctly.

!!! tip "LED"
    ![Solid LED](../assets/led_solid.gif)
    If you upload this sketch, you will notice the LED on the board will light
    on. This is because you are in `configuration` mode.

Homie for ESP8266 has 3 modes of operation:

1. By default, the `configuration` mode is the initial one. It spawns an AP and
   an HTTP web server exposing a JSON API. Connect to that AP to list available
   Wi-Fi networks and send the configuration, such as the Wi-Fi SSID, Wi-Fi
   password and settings. Once the device receives the credentials, it boots
   into `normal` mode.

2. The `normal` mode is the mode the device will be in most of the time. It
   connects to Wi-Fi and MQTT, sends initial information to the Homie server,
   like the local IP and the version of the firmware currently running, and
   subscribes to the needed MQTT topics. It automatically reconnects to Wi-Fi
   and MQTT when the connection is lost. It also handles OTA. The device can
   return to `configuration` mode in different ways, including a button press or
   a custom function. See [Resetting](../advanced-usage/resetting.md).

3. The `standalone` mode. See
   [Standalone mode](../advanced-usage/standalone-mode.md).

!!! warning
    **As a rule of thumb, avoid blocking the device for more than 50 ms at a
    time.** Longer blocking work can cause unexpected behavior.

## Connecting to the AP and configuring the device

Homie for ESP8266 has spawned a secure AP named `Homie-xxxxxxxxxxxx`, like
`Homie-c631f278df44`. Connect to it.

!!! tip "Hardware device ID"
    This `c631f278df44` ID is unique to each device, and you cannot change it
    (it is the MAC address of the station mode). If you flash a new
    sketch, this ID won't change.

Once connected, the web server is available at `http://192.168.123.1`. Every
domain name is resolved by the built-in DNS server to this address. You can then
configure the device using the
[HTTP JSON API](../configuration/http-json-api.md). When the device receives its
configuration, it will reboot into `normal` mode.

## Understanding what happens in `normal` mode

### Visual codes

When the device boots in `normal` mode, it will start blinking:

!!! tip "LED"
    ![Slowly blinking LED](../assets/led_wifi.gif)
    Slowly when connecting to the Wi-Fi

!!! tip "LED"
    ![Fast blinking LED](../assets/led_mqtt.gif)
    Faster when connecting to the MQTT broker

This way, you can have quick feedback on what is going on. If both connections
are established, the LED will stay off. The device will also blink during
automatic reconnection if the Wi-Fi or MQTT broker connection is lost.

### Under the hood

Although the sketch looks small, Homie for ESP8266 is already handling several
runtime tasks:

- It automatically connects to the Wi-Fi and MQTT broker, removing the network
  boilerplate from your sketch.
- It exposes the Homie device on MQTT, as `<base topic>/<device ID>`, for
  example `homie/c631f278df44`.
- It subscribes to the special OTA and configuration topics, automatically
  flashing a sketch if available or updating the configuration.
- It checks for the configured reset trigger on the board to return to
  `configuration` mode.

## Creating a useful sketch

Now that we understand how Homie for ESP8266 works, let's create a useful
sketch. We want to create a smart light.

[![GitHub logo](../assets/github.png) LightOnOff.ino](https://github.com/labodj/homie-esp8266/blob/develop/examples/LightOnOff/LightOnOff.ino)

Alright, step by step:

1. We create a node with an ID of `light`, a name of `Light`, and a type of
   `switch` with `HomieNode lightNode("light", "Light", "switch")`.
2. We set the name and the version of the firmware with
   `Homie_setFirmware("awesome-light" ,"1.0.0");`.
3. We want our `light` node to advertise an `on` property, which is settable. We
   do that with `lightNode.advertise("on").settable(lightOnHandler);`. The
   `lightOnHandler` function will be called when the value of this property is
   changed.
4. In the `lightOnHandler` function, we want to update the state of the `light`
   node. We do this with `lightNode.setProperty("on").send("true");`.

In about thirty lines of code, we have created a smart light without hard-coded
credentials, with automatic reconnection in case of network failure, and with
OTA support.

## Creating a sensor node

In the previous example sketch, we were reacting to property changes. But what
if we want, for example, to send a temperature every 5 minutes? We could do this
in the Arduino `loop()` function. But then, we would have to check if we are in
`normal` mode, and we would have to ensure the network connection is up before
being able to send anything.

Homie for ESP8266 provides a dedicated hook for that.

[![GitHub logo](../assets/github.png) TemperatureSensor.ino](https://github.com/labodj/homie-esp8266/blob/develop/examples/TemperatureSensor/TemperatureSensor.ino)

The only new calls here are the `Homie.setSetupFunction(setupHandler);` and
`Homie.setLoopFunction(loopHandler);` calls. The setup function will be called
once, when the device is in `normal` mode and the network connection is up. The
loop function will be called every time, when the device is in `normal` mode and
the network connection is up. Your sketch can focus on the device behavior while
Homie handles the connection state.

Now that you understand the basic usage of Homie for ESP8266, you can head on to
the next pages to learn about more powerful features like input handlers, the
event system and custom settings.
