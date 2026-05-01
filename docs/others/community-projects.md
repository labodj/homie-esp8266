# Community projects

This page lists community projects that work with Homie.

[![works with MQTT Homie](https://homieiot.github.io/img/works-with-homie.svg)](https://homieiot.github.io/)

## [jpmens/homie-ota](https://github.com/jpmens/homie-ota)

homie-ota is written in Python. It provides an OTA server for Homie devices and
an inventory for keeping track of them.
homie-ota also enables you to trigger an OTA update (over MQTT, using the Homie
convention) from within its inventory. New firmware can be uploaded to homie-ota
which detects firmware name (fwname) and version (fwversion) from the uploaded
binary blob, thanks to an idea and code contributed by Marvin.

## [stufisher/homie-control](https://github.com/stufisher/homie-control)

homie-control provides a web UI to manage Homie devices as well as a series of
virtual Python devices for extended functionality.

It lets you:

- Historically log device properties
- Schedule changes in event properties, such as watering your garden once a day
- Execute profiles of property values, such as turning a series of lights on and
  off simultaneously
- Trigger property changes based on:
  - When a network device is connected or disconnected, such as turning the
    lights on when your phone joins your Wi-Fi
  - Sunset / rise
  - When another property changes
