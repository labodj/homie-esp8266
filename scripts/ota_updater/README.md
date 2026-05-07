Script: OTA updater
===================

This script will allow you to send an OTA update to your device.

Installation
------------

```bash
python3 -m pip install -r requirements.txt
```

Development checks
------------------

```bash
python3 -m pip install -r requirements-dev.txt
make python-check
```

Usage
-----

```text
usage: homie_ota.py [-h] [--config CONFIG] [-l BROKER_HOST] [-p BROKER_PORT]
                    [--broker-username-env BROKER_USERNAME_ENV]
                    [-u BROKER_USERNAME]
                    [--broker-password-env BROKER_PASSWORD_ENV]
                    [-d BROKER_PASSWORD] [-t BASE_TOPIC]
                    [--homie-version {3,4,5}] -i DEVICE_ID
                    [--broker-tls-cacert BROKER_TLS_CACERT]
                    [--broker-tls-certfile BROKER_TLS_CERTFILE]
                    [--broker-tls-keyfile BROKER_TLS_KEYFILE]
                    [--broker-tls-insecure] [--client-id CLIENT_ID]
                    [--expected-md5 EXPECTED_MD5] [--timeout TIMEOUT]
                    firmware

Send an OTA firmware update to a Homie device that implements the
homie-esp8266 OTA MQTT contract.

positional arguments:
  firmware              path to the firmware to be sent to the device

arguments:
  -h, --help            show this help message and exit
  --config CONFIG       optional JSON or TOML defaults file. CLI arguments
                        override values from the file.
  -l BROKER_HOST, --broker-host BROKER_HOST
                        host name or ip address of the mqtt broker
  -p BROKER_PORT, --broker-port BROKER_PORT
                        port of the mqtt broker
  --broker-username-env BROKER_USERNAME_ENV
                        environment variable containing the broker username
  -u BROKER_USERNAME, --broker-username BROKER_USERNAME
                        username used to authenticate with the mqtt broker
  --broker-password-env BROKER_PASSWORD_ENV
                        environment variable containing the broker password
  -d BROKER_PASSWORD, --broker-password BROKER_PASSWORD
                        password used to authenticate with the mqtt broker
  -t BASE_TOPIC, --base-topic BASE_TOPIC
                        base topic/domain of the Homie devices on the broker.
                        With --homie-version 5, the updater appends the
                        required /5/ segment unless it is already present.
  --homie-version {3,4,5}, --convention-version {3,4,5}
                        Homie convention generation used by the target
                        firmware. Use 5 for devices built with
                        HOMIE_CONVENTION_VERSION=5.
  -i DEVICE_ID, --device-id DEVICE_ID
                        homie device id
  --broker-tls-cacert BROKER_TLS_CACERT
                        CA certificate bundle used to validate TLS
                        connections. If set, TLS is enabled on the broker
                        connection.
  --broker-tls-certfile BROKER_TLS_CERTFILE
                        client certificate file used for mutual TLS
                        authentication
  --broker-tls-keyfile BROKER_TLS_KEYFILE
                        private key file used with --broker-tls-certfile
  --broker-tls-insecure
                        enable TLS but skip broker certificate verification.
                        Use only for temporary tests with private brokers.
  --client-id CLIENT_ID
                        MQTT client id used by the updater. Defaults to
                        homie-ota-updater-<device-id>.
  --expected-md5 EXPECTED_MD5
                        expected firmware MD5; aborts before publishing if the
                        file does not match
  --timeout TIMEOUT     maximum time in seconds to wait for the OTA workflow
                        to complete
```

Prefer `scripts/homie_ota.py` for new automation. It is the stable entry point
exported by the PlatformIO package; `scripts/ota_updater/ota_updater.py` remains
available for compatibility.

* `BROKER_HOST` and `BROKER_PORT` defaults to 127.0.0.1 and 1883 respectively
  if not set.
* `BROKER_USERNAME` and `BROKER_PASSWORD` are optional.
* `--broker-username-env` and `--broker-password-env` read credentials from the
  named environment variable at runtime.
* `BASE_TOPIC` is normalized with a trailing slash, defaults to `homie/` if not set.
* `--homie-version 5` publishes to the required v5 root such as
  `homie/5/<device-id>/...`.
* TLS is enabled automatically when any `--broker-tls-*` option is set.
* `--timeout` defaults to `300` seconds.
* `--expected-md5` is optional, but useful in scripted deployments where the
  firmware checksum is produced by a build step.
* The script exits with code `0` on success or when the device is already up to
  date, and with a non-zero code on failure.
* The helper is compatible with the maintained `homie-esp8266` OTA status codes,
  including `400 BAD_*` and `500 FLASH_ERROR`.

Example
-------

```bash
python3 ota_updater.py -l localhost -u admin -d secure -t "homie/" -i "device-id" /path/to/firmware.bin
```

For Homie v5 firmware:

```bash
python3 ota_updater.py --homie-version 5 -l localhost -i "device-id" /path/to/firmware.bin
```

With a reusable JSON config:

```json
{
  "broker": {
    "host": "mqtt.local",
    "port": 1883,
    "username": "homie",
    "password_env": "HOMIE_OTA_PASSWORD"
  },
  "homie": {
    "base_topic": "homie/5/",
    "version": "5"
  },
  "ota": {
    "timeout": 300
  }
}
```

```bash
python3 ../scripts/homie_ota.py --config bridge-ota.json -i device-id firmware.bin
```

TOML config files with the same section names are also accepted when running on
Python 3.11 or newer. CLI arguments always override values from the config file,
which keeps one-off tests easy without editing the saved defaults.
