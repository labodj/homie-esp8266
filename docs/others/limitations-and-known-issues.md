# Limitations and known issues

## SSL support

In Homie for ESP8266 v1.x, SSL was possible but it was not reliable. Due to the
asynchronous nature of the v2.x, SSL is not completely available anymore. Only
MQTT connections can be encrypted with SSL.

## ADC readings

[A known ESP8266 Arduino issue](https://github.com/esp8266/Arduino/issues/1634)
can make Wi-Fi disconnect when `analogRead()` is polled too frequently. As a
workaround, poll the ADC no more than once every 3 ms.

## Wi-Fi connection

If you encounter any issues with the Wi-Fi, try changing the flash size build
parameter, or try to erase the flash. See
[#158](https://github.com/homieiot/homie-esp8266/issues/158) for more
information.
