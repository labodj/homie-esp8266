The Homie `$implementation` identifier is platform-dependent on this fork:

* `esp32` on ESP32 builds
* `esp8266` on ESP8266 builds

# Version

* `$implementation/version`: maintained fork version

# Reset

* `$implementation/reset`: You can publish a `true` to this topic to reset the device

# Configuration

* `$implementation/config`: The `config.json` is published there, with `wifi.password`, `mqtt.username` and `mqtt.password` fields stripped
* `$implementation/config/set`: You can update the `config.json` by sending incremental JSON on this topic

# OTA

* `$implementation/ota/enabled`: `true` if OTA is enabled, `false` otherwise
* `$implementation/ota/firmware/<md5 checksum>`: Send the firmware payload to this topic, where the last topic level is the hexadecimal MD5 checksum of the firmware image
* `$implementation/ota/status`: HTTP-like status code indicating the status of the OTA. Might be:

Code|Description
----|-----------
`200`|OTA successfully flashed
`202`|OTA request / checksum accepted
`206 465/349680`|OTA in progress. The data after the status code corresponds to `<bytes written>/<bytes total>`
`304`|The current firmware is already up-to-date
`400 BAD_FIRMWARE`|OTA error from your side. The identifier might be `BAD_FIRMWARE`, `BAD_CHECKSUM`, `NOT_ENOUGH_SPACE`, `NOT_REQUESTED`
`403`|OTA not enabled
`500 FLASH_ERROR`|OTA error in the device flash/update path. The identifier might be `FLASH_ERROR`

On this fork, the OTA handler is hardened for MQTT QoS 1 delivery:

* duplicate retransmissions of already-flashed payloads are ignored safely
* overlapping retransmitted chunks are trimmed before flashing
* out-of-sequence chunks fail explicitly instead of silently corrupting the update
* if MQTT disconnects during OTA, the update is aborted cleanly and must be retried

# Filesystem

SPIFFS remains the default storage backend for compatibility with existing
devices. LittleFS is selected only when `HOMIE_USE_LITTLEFS=1` is compiled into
the firmware. A temporary migration build can add
`HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS=1` to copy `/homie/config.json` and
`/homie/NEXTMODE` from SPIFFS to LittleFS on first boot.

The UI bundle is not migrated because SPIFFS and LittleFS share the same flash
area and the bundle can be too large to hold in RAM while the destination
filesystem is formatted.

# Statistics

The fork publishes the standard Homie statistics plus these additional retained
topics:

* `$stats/freeheap`: current free heap in bytes
* `$stats/uptimewifi`: seconds since Wi-Fi connectivity was established
* `$stats/uptimemqtt`: seconds since MQTT connectivity was established
* `$stats/mqttackdropped`: cumulative MQTT publish acknowledgement queue drops
* `$stats/mqttinbounddropped`: cumulative deferred inbound MQTT queue drops
