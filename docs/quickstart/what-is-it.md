# What is Homie for ESP8266?

Homie for ESP8266 is an Arduino implementation of
[Homie](https://github.com/homieiot/convention), a thin and simple MQTT
convention for the IoT. It also gives your sketch the framework code it needs to
manage Wi-Fi, MQTT connection and reconnection, and device configuration. You do
not have to hard-code credentials in your sketch: they can be provided through a
small JSON API and handled internally by Homie for ESP8266.

This maintained fork keeps the original Homie 3.0.1 API while adding support for
current ESP32 and ESP8266 Arduino environments. Homie 3.0.1 remains the default
advertised MQTT convention. Homie 4.0.0 and Homie 5.0 discovery metadata can be
enabled explicitly with `HOMIE_CONVENTION_VERSION=4` or
`HOMIE_CONVENTION_VERSION=5`.

The purpose of Homie for ESP8266 is to simplify the development of connected
objects.
