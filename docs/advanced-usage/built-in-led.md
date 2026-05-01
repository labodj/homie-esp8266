# Built-in LED

By default, Homie for ESP8266 will blink the built-in LED to indicate its
status. Note it does not indicate activity, only the status of the device (in
`configuration` mode, connecting to Wi-Fi or connecting to MQTT), see
[Getting started](../quickstart/getting-started.md) for more information.

However, on some boards like the ESP-01, the built-in LED is tied to the TX
port. That is fine when serial logging is disabled, but it becomes noisy when
serial logging is enabled. You can disable the built-in LED feedback:

```c++
void setup() {
  Homie.disableLedFeedback(); // before Homie.setup()
  // ...
}
```

Instead of disabling LED feedback completely, you can move it to another pin:

```c++
void setup() {
  Homie.setLedPin(16, HIGH); // before Homie.setup(); 2nd param is LED-off state
  // ...
}
```
