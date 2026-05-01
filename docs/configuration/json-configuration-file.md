# JSON configuration file

To configure your device, you have two choices: manually flashing the
configuration file to the configured filesystem at `/homie/config.json` (SPIFFS
by default, or LittleFS when `HOMIE_USE_LITTLEFS=1`), so you can bypass the
`configuration` mode, or send it through the [HTTP JSON API](http-json-api.md).
When switching an already provisioned device to LittleFS, use a temporary build
with `HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS=1` to migrate this file from SPIFFS on
first boot.

Use this JSON shape when provisioning the device:

```json
{
  "name": "The kitchen light",
  "device_id": "kitchen-light",
  "device_stats_interval": 60,
  "wifi": {
    "ssid": "Network_1",
    "password": "I'm a Wi-Fi password!",
    "bssid": "DE:AD:BE:EF:BA:BE",
    "channel": 1,
    "ip": "192.168.1.5",
    "mask": "255.255.255.0",
    "gw": "192.168.1.1",
    "dns1": "8.8.8.8",
    "dns2": "8.8.4.4"
  },
  "mqtt": {
    "host": "192.168.1.10",
    "port": 1883,
    "base_topic": "devices/",
    "auth": true,
    "username": "user",
    "password": "pass",
    "ssl": true,
    "ssl_fingerprint": "a27992d3420c89f293d351378ba5f5675f74fe3c"
  },
  "ota": {
    "enabled": true
  },
  "settings": {
    "percentage": 55
  }
}
```

The above JSON contains every field that can be customized.

Here are the rules:

- `name`, `wifi.ssid`, `wifi.password`, `mqtt.host` and `ota.enabled` are
  mandatory
- `wifi.password` can be `null` if connecting to an open network
- If `mqtt.auth` is `true`, `mqtt.username` and `mqtt.password` must be provided
- `bssid`, `channel`, `ip`, `mask`, `gw`, `dns1`, `dns2` are optional and only
  needed when targeting a specific AP or configuring a static IP address.
  These fields have a few rules:
  - `bssid` and `channel` must be defined together and these settings are
    independent of static IP settings
  - to define static IP, `ip` (IP address), `mask` (netmask) and `gw` (gateway)
    settings must be defined at the same time
  - to define the secondary DNS server `dns2`, define `dns1` as well. Setting
    DNS without `ip`, `mask` and `gw` does not affect the configuration; the DNS
    server is provided by DHCP. DNS servers are optional.
- `ssl_fingerprint` can optionally be defined if `ssl` is enabled. The public
  key of the MQTT server is then verified against the fingerprint.
- Homie's limit for MQTT broker username and password is 32 characters. To
  increase this limit, change `MAX_MQTT_CREDS_LENGTH` in `Limits.hpp`

Default values if not provided:

- `device_id`: the hardware device ID (eg. `1a2b3c4d5e6f`)
- `device_stats_interval`: 60 seconds
- `mqtt.port`: `1883`
- `mqtt.base_topic`: `homie/`
- `mqtt.auth`: `false`

The `mqtt.host` field can be either an IP address or a hostname.

!!! tip "OTA"
    Homie-esp8266 supports over-the-air updates if you enable OTA in
    configuration (`ota.enabled: true`). For more details, see
    [OTA updates](../others/ota-configuration-updates.md).
