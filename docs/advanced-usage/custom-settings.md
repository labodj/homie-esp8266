# Custom settings

Homie for ESP8266 lets you implement custom settings that can be set from the
JSON configuration file and the Configuration API. Below is an example of how to
use this feature:

```c++
HomieSetting<long> percentageSetting(
  "percentage",
  "A simple percentage"
);  // id, description

void setup() {
  percentageSetting.setDefaultValue(50).setValidator([] (long candidate) {
    return (candidate >= 0) && (candidate <= 100);
  });

  Homie.setup();
}
```

!!! tip "setDefaultValue() before Homie.setup()"
    As shown in the example above, the **default value** has to be set
    **before** `Homie.setup()` is called. Otherwise you get an error on startup
    if there is also no value configured in the JSON configuration file.

An `HomieSetting` instance can be of the following types:

| Type          | Value                                               |
| ------------- | --------------------------------------------------- |
| `bool`        | `true` or `false`                                   |
| `long`        | An integer from `-2,147,483,648` to `2,147,483,647` |
| `double`      | A floating number that can fit into a `real64_t`    |
| `const char*` | Any string                                          |

By default, a setting is mandatory, so it must be present in the configuration
file. If you give it a default value with `setDefaultValue()`, the setting
becomes optional. You can validate a setting by passing a validator function to
`setValidator()`. To get the setting from your code, use `get()`. To check
whether the returned value is the default or the configured value, use
`wasProvided()`.

For this example, if you want to provide the `percentage` setting, put this in
your configuration file:

```json
{
  "settings": {
    "percentage": 75
  }
}
```

## Updating custom settings

To change custom settings over MQTT, publish the JSON object to
`homie/<device>/$implementation/config/set`. The object can include every
setting you need to update, or only the fields that changed.

From the example above, the compact payload is:

```json
{ "settings": { "percentage": 75 } }
```

!!! tip "Updated custom settings are saved automatically"
    Homie saves updated custom settings to the configured filesystem, then
    reboots. SPIFFS is used by default; LittleFS is opt-in with
    `HOMIE_USE_LITTLEFS=1`. On the next boot, the stored value is read into RAM
    and used by the sketch.

!!! tip "If reboots are disruptive, choose another settings flow"
    Sometimes the device is designed for uninterrupted service. In that case, a
    reboot after a settings update may not be acceptable for the device behavior.

## Example

See the following example for a concrete use case:

[![GitHub logo](../assets/github.png) CustomSettings.ino](https://github.com/labodj/homie-esp8266/blob/develop/examples/CustomSettings/CustomSettings.ino)
