# OTA updates

Homie for ESP8266 supports over-the-air (OTA) updates when OTA is enabled in the
configuration and a compatible OTA entity is set up.

The repository includes an OTA helper script:

[![GitHub logo](../assets/github.png) ota_updater.py](https://github.com/labodj/homie-esp8266/blob/develop/scripts/ota_updater/ota_updater.py)
[![GitHub logo](../assets/github.png) OTA updater README](https://github.com/labodj/homie-esp8266/blob/develop/scripts/ota_updater/README.md)

It works this way:

1. During startup, the Homie for ESP8266 device reports the current firmware MD5
   to `$fw/checksum`, in addition to `$fw/name` and `$fw/version`. The OTA
   entity may or may not use this information to automatically schedule OTA
   updates.
2. The OTA entity publishes the latest available firmware payload to
   `$implementation/ota/firmware/<md5 checksum>`, either as binary or as a
   Base64 encoded string, with MQTT retain disabled.
   - If OTA is disabled, Homie for ESP8266 reports `403` to
     `$implementation/ota/status` and aborts the OTA
   - If OTA is enabled and the latest available checksum is the same as what is
     currently running, Homie for ESP8266 reports `304` and aborts the OTA
   - If the checksum is not a valid MD5, Homie for ESP8266 reports
     `400 BAD_CHECKSUM` to `$implementation/ota/status` and aborts the OTA

3. Homie starts to flash the firmware
   - The firmware is updating. Homie for ESP8266 reports progress with
     `206 <bytes written>/<bytes total>`
   - When all bytes are flashed, the firmware is verified against the requested
     MD5. Homie for ESP8266 then reports `200` on success, `400` if the
     firmware is invalid, or `500` if there is an internal error.

4. On this fork, the maintained OTA helper publishes firmware with MQTT QoS 1.
   - Duplicate retransmissions of the same firmware are tolerated safely
   - Overlapping retransmitted chunks are trimmed before they reach flash
   - If MQTT disconnects during OTA, the update is aborted cleanly and must be
     retried

5. Homie for ESP8266 reboots on success as soon as the device is idle.

## Recovery and diagnostics

Every failed transfer releases the flash updater and its temporary decode buffer,
so a new attempt does not require a reboot. A disconnected transfer is discarded;
it is not resumed across MQTT connections. After a successful update, another
firmware cannot overwrite the staged image while the device waits to reboot.

An incomplete transfer also expires after 60 seconds without new payload bytes.
This is an inactivity timeout, not a limit on the total upload duration. Override
`HOMIE_OTA_TIMEOUT_MS` at compile time when a deployment needs a longer interval.
Duplicate fragments do not extend the timeout.

Failures from the Arduino Update API include the operation and platform error
number, for example `500 INTERNAL_ERROR 0 BEGIN` or `500 FLASH_ERROR 1 WRITE`.
The serial log also reports byte counters and free heap. A zero error number
means the API returned failure without recording a more specific cause; it is
not evidence that flash hardware is faulty.

The helper ignores retained OTA status messages replayed by the broker. They
belong to an earlier transfer and cannot confirm or reject a new upload. Live
status messages are still processed, and success is verified using the firmware
checksum after the device returns online.

Older installed firmware does not gain the recovery fix until it is updated.
If its updater is already stuck, a reboot or serial flash may still be necessary
to install the corrected firmware. This does not justify a factory reset.

### Development tests

After building the ESP32 pioarduino example, run `make ota-test` from the repository
root. It uses the installed Arduino libb64 decoder; `OTA_CORE_DIR` can select a
different core directory. The tests execute the private production OTA code with
a simulated flash updater, inject failures and disconnections, and exercise
Base64 chunk boundaries under AddressSanitizer and UndefinedBehaviorSanitizer.
Both ESP32 and ESP8266 cleanup branches and signed/unsigned `char` are checked.
Firmware compilation and on-device validation remain separate checks.

For devices built with `HOMIE_CONVENTION_VERSION=5`, run the helper with
`--homie-version 5`. The helper will publish to `homie/5/<device-id>/...` when
the default base topic is used:

```bash
python scripts/homie_ota.py \
  --homie-version 5 \
  -i kitchen-light \
  firmware.bin
```

If you use a custom Homie v5 domain, pass the domain as `--base-topic`; the
helper appends the required `/5/` segment unless it is already present.
For repeated deployments, put broker and Homie defaults in a JSON or TOML file
and pass `--config bridge-ota.json`. CLI arguments override the config file, and
`broker.password_env` / `broker.username_env` keep credentials in environment
variables instead of command history.

See [Homie implementation specifics](homie-implementation-specifics.md) for more
details on status codes.

## OTA entities projects

See [Community projects](community-projects.md).

## Configuration updates

In `normal` mode, you can get the current `config.json`, published on
`$implementation/config` with `wifi.password`, `mqtt.username` and
`mqtt.password` stripped. You can update the configuration on the fly by
publishing incremental JSON updates to `$implementation/config/set`. For
example, given the following `config.json`:

```json
{
  "name": "Kitchen light",
  "wifi": {
    "ssid": "Network_1",
    "password": "I'm a Wi-Fi password!"
  },
  "mqtt": {
    "host": "192.168.1.20",
    "port": 1883
  },
  "ota": {
    "enabled": false
  },
  "settings": {}
}
```

You can update the name and Wi-Fi password by sending the following incremental
JSON:

```json
{
  "name": "Living room light",
  "wifi": {
    "password": "I'm a new Wi-Fi password!"
  }
}
```
