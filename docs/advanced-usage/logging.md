# Logging

By default, Homie for ESP8266 writes debug messages to `Serial`. Disable logging
when the sketch needs the serial line for something else.

```c++
void setup() {
  Homie.disableLogging(); // before Homie.setup()
  // ...
}
```

!!! warning
    It's up to you to call `Serial.begin();`, whether logging is enabled or not.

You can also change the `Print` instance to log to:

```c++
void setup() {
  Homie.setLoggingPrinter(&Serial2); // before Homie.setup()
  // ...
}
```

You can use the logger from your code with the `getLogger()` client:

```c++
Homie.getLogger() << "Hey!" << endl;
```
