## 1. I see some garbage on the Serial monitor?

You are probably using a generic ESP8266. The problem with these modules is the built-in LED is tied to the serial line. You can do two things:

- Disable the serial logging, to have the LED working:

```c++
void setup() {
  Homie.enableLogging(false); // before Homie.setup()
  // ...
}
```

- Disable the LED blinking, to have the serial line working:

```c++
void setup() {
  Homie.enableBuiltInLedIndicator(false); // before Homie.setup()
  // ...
}
```

## 2. I see an `abort` message on the Serial monitor?

`abort()` is called by Homie for ESP8266 when the framework is used in a bad way. The possible causes are:

- You are calling a function that is meant to be called before `Homie.setup()`, after `Homie.setup()`

- One of the string you've used (in `setFirmware()`, `subscribe()`, etc.) is too long. Check the `Limits.hpp` file to see the max length possible for each string.

## 3. The network is completely unstable... What's going on?

The framework needs to work continuously (ie. `Homie.loop()` needs to be called very frequently). In other words, don't use `delay()` (see [avoid delay](http://playground.arduino.cc/Code/AvoidDelay)) or anything that might block the code for more than 50ms or so. There is also a known Arduino for ESP8266 issue with `analogRead()`, see [Limitations and known issues](limitations-and-known-issues.md#adc-readings).

## 4. My device resets itself without me doing anything?

You have probably connected a sensor to the default reset pin of the framework (D3 on NodeMCU, GPIO0 on other boards). See [Resetting](../advanced-usage/resetting.md).

## 5. I see `MQTT ACK queue full` during startup or reconnect

This means the device is receiving MQTT publish acknowledgements faster than
Homie can dispatch the related internal events.

On this maintained fork you can increase the internal acknowledgement queue
size with:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_ACK_QUEUE_SIZE=64
```

See [Compiler-Flags](../advanced-usage/compiler-flags.md) for details. Only
increase it if you actually see this warning in practice.

## 6. PlatformIO tries to build `AsyncTCP`, `ESPAsyncTCP`, or `RPAsyncTCP` for the wrong board

Use strict library compatibility mode in your `platformio.ini`:

```ini
lib_compat_mode = strict
```

The async networking dependencies publish platform metadata, but PlatformIO's
default `soft` compatibility mode may still compile transitive libraries for
other targets when using `pio ci` or unusual dependency layouts.

## 7. I see `MQTT inbound queue full`

Homie defers non-OTA MQTT input handling from async MQTT callbacks into
`Homie.loop()`. If expected broker traffic fills that queue, increase it with:

```ini
build_flags =
  -D HOMIE_PENDING_MQTT_MESSAGE_QUEUE_SIZE=32
```

Keep `Homie.loop()` frequent; increasing the queue only absorbs bursts.

Both queue drop counters are published in the regular Homie statistics as
`$stats/mqttackdropped` and `$stats/mqttinbounddropped`. If either value grows
after boot under normal traffic, tune the related queue or reduce retained MQTT
traffic delivered to the device.
