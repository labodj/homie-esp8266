# Resetting

Resetting the device means erasing the stored configuration and rebooting from
`normal` mode to `configuration` mode. A shorter press opens configuration
without erasing the stored device ID, Wi-Fi, MQTT or firmware settings.
On many dev boards the configured trigger is the `FLASH` or `BOOT` button:

- Hold for at least 5 seconds, then release before 15 seconds: open the portal
  and retain the current configuration. Leave unchanged password fields blank.
- Hold for at least 15 seconds, then release: factory reset, including device ID.
- Shorter presses do nothing. The action is selected on release and waits until
  the device is idle. A failed erase or boot-mode write cancels the restart.

To leave the portal without saving, power-cycle the device. If a valid stored
configuration exists it will be used again.

This behavior is configurable:

```c++
void setup() {
  Homie.setResetTrigger(1, LOW, 2000); // before Homie.setup()
  // ...
}
```

Pin `1` held `LOW` for at least `2000` ms opens configuration on release;
at least three times that interval (`6000` ms) selects factory reset.
You can also disable this trigger completely:

```c++
void setup() {
  Homie.disableResetTrigger(); // before Homie.setup()
  // ...
}
```

You can also trigger a factory reset from your sketch (this erases the ID):

```c++
void loop() {
  Homie.reset();
}
```

This resets the device as soon as it is idle. If the device is running critical
work, such as moving shutters, temporarily mark it as not idle until that work is
finished.

```c++
Homie.setIdle(false);
```

If a reset is requested while the device is not idle, Homie records the pending
reset. When you call `Homie.setIdle(true);` again, the device resets
immediately.
