Script: OTA updater
===================

This script will allow you to send an OTA update to your device.

## Installation

```bash
python3 -m pip install -r requirements.txt
```

## Usage

```text
usage: ota_updater.py [-h] [-l BROKER_HOST] [-p BROKER_PORT]
                      [-u BROKER_USERNAME] [-d BROKER_PASSWORD]
                      [-t BASE_TOPIC] -i DEVICE_ID
                      [--broker-tls-cacert BROKER_TLS_CACERT]
                      [--timeout TIMEOUT]
                      firmware

Send an OTA firmware update to a Homie device that implements the
homie-esp8266 OTA MQTT contract.

positional arguments:
  firmware              path to the firmware to be sent to the device

arguments:
  -h, --help            show this help message and exit
  -l BROKER_HOST, --broker-host BROKER_HOST
                        host name or ip address of the mqtt broker
  -p BROKER_PORT, --broker-port BROKER_PORT
                        port of the mqtt broker
  -u BROKER_USERNAME, --broker-username BROKER_USERNAME
                        username used to authenticate with the mqtt broker
  -d BROKER_PASSWORD, --broker-password BROKER_PASSWORD
                        password used to authenticate with the mqtt broker
  -t BASE_TOPIC, --base-topic BASE_TOPIC
                        base topic of the homie devices on the broker
  -i DEVICE_ID, --device-id DEVICE_ID
                        homie device id
  --broker-tls-cacert BROKER_TLS_CACERT
                        CA certificate bundle used to validate TLS
                        connections. If set, TLS is enabled on the broker
                        connection.
  --timeout TIMEOUT     maximum time in seconds to wait for the OTA workflow
                        to complete
```

* `BROKER_HOST` and `BROKER_PORT` defaults to 127.0.0.1 and 1883 respectively if not set.
* `BROKER_USERNAME` and `BROKER_PASSWORD` are optional.
* `BASE_TOPIC` has to end with a slash, defaults to `homie/` if not set.
* `--timeout` defaults to `300` seconds.
* The script exits with code `0` on success or when the device is already up to date, and with a non-zero code on failure.
* The helper is compatible with the maintained `homie-esp8266` OTA status codes, including `400 BAD_*` and `500 FLASH_ERROR`.

### Example:

```bash
python3 ota_updater.py -l localhost -u admin -d secure -t "homie/" -i "device-id" /path/to/firmware.bin
```
