Homie for ESP8266 is an Arduino implementation of [Homie](https://github.com/homieiot/convention), a thin and simple MQTT convention for the IoT. More than that, it's also a full-featured framework to get started with your IoT project very quickly. Simply put, you don't have to manage yourself the connection/reconnection to Wi-Fi or MQTT. You don't even have to hard-code credentials in your sketch: this can be done using a simple JSON API. Everything is handled internally by Homie for ESP8266.

This maintained fork keeps the original Homie 3.0.1 API while adding support
and maintenance work for current ESP32 and ESP8266 Arduino environments. Homie
3.0.1 remains the default advertised MQTT convention. Homie 4.0.0 and Homie 5.0
discovery metadata can be enabled explicitly with `HOMIE_CONVENTION_VERSION=4`
or `HOMIE_CONVENTION_VERSION=5`.

You guessed it, the purpose of Homie for ESP8266 is to simplify the development of connected objects.
