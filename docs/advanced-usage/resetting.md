# Resetting

Resetting the device means erasing the stored configuration and rebooting from
`normal` mode to `configuration` mode. By default, you can do it by holding the
configured reset trigger for 5 seconds. On many dev boards this is the `FLASH`
or `BOOT` button.

This behavior is configurable:

```c++
void setup() {
  Homie.setResetTrigger(1, LOW, 2000); // before Homie.setup()
  // ...
}
```

The device now resets if pin `1` is `LOW` for `2000` ms. You can also disable
this reset trigger completely:

```c++
void setup() {
  Homie.disableResetTrigger(); // before Homie.setup()
  // ...
}
```

You can also trigger a device reset from your sketch:

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
