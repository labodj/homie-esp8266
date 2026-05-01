# Branding

By default, Homie for ESP8266 spawns a `Homie-xxxxxxxxxxxx` AP and connects to
the MQTT broker with the `Homie-xxxxxxxxxxxx` client ID. Use a custom brand when
the device should expose a different prefix:

```c++
void setup() {
  Homie_setBrand("MyIoTSystem"); // before Homie.setup()
  // ...
}
```
