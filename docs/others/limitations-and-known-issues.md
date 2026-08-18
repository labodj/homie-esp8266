# Limitations and known issues

## SSL support

The asynchronous `espMqttClient` backend does not support MQTT TLS. Builds with
`ASYNC_TCP_SSL_ENABLED=1` and runtime configurations with `mqtt.ssl=true` are
rejected explicitly.

## ADC readings

[A known ESP8266 Arduino issue](https://github.com/esp8266/Arduino/issues/1634)
can make Wi-Fi disconnect when `analogRead()` is polled too frequently. As a
workaround, poll the ADC no more than once every 3 ms.

## Wi-Fi connection

If you encounter any issues with the Wi-Fi, try changing the flash size build
parameter, or try to erase the flash. See
[#158](https://github.com/homieiot/homie-esp8266/issues/158) for more
information.
