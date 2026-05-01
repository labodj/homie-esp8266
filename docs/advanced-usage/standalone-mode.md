# Standalone mode

Homie for ESP8266 has a special mode named `standalone`. It was added after a
[feature request](https://github.com/homieiot/homie-esp8266/issues/125) for
devices that should not boot into `configuration` mode on first startup. This
lets a device run before it has a saved configuration, without exposing the
configuration AP.

To enable this mode, call `Homie.setStandalone()`:

```c++
void setup() {
  Homie.setStandalone(); // before Homie.setup()
  // ...
}
```

To configure the device later, reset it the same way you would move from
`normal` mode to `configuration` mode.
