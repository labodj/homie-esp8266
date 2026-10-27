# UI bundle

An unconfigured device starts a Wi-Fi hotspot and serves a built-in setup page.
Connect to the `<brand>-<hardware ID>` network (normally `Homie-xxxxxxxxxxxx`)
and open `http://192.168.123.1` if the captive portal does not open automatically.
The page works offline and is included in the firmware: no filesystem upload,
external configurator or Internet connection is required.

Enter the device name, device ID, Wi-Fi and MQTT details, and any firmware settings.
When replacing a device, enter its existing ID and disconnect the old device
before saving to avoid two devices using the same MQTT identity.

The page first tests Wi-Fi (up to 30 seconds) and MQTT authentication (up to
15 seconds). A failed test leaves the hotspot running, keeps the entered fields
and does not change the stored configuration. MQTT checks use a separate setup
client ID and publish no commands; they do not prove topic permissions or normal
firmware readiness. If your broker restricts client IDs, allow the setup ID too.

After successful tests, saving a valid configuration restarts the device in normal mode. The hotspot,
DNS captive portal and HTTP server run **only in configuration mode**. They do
not remain active after onboarding; losing Wi-Fi alone does not reopen setup.

To reopen setup deliberately without erasing the existing configuration, hold
the configured `BOOT`/`FLASH` trigger for 5 seconds and release before 15 seconds.
Alternatively use `Homie.setHomieBootModeOnNextBoot(HomieBootMode::CONFIGURATION)`
and restart the device. The page preloads the saved identity and non-secret fields. Unchanged
passwords and optional settings are preserved on the device; saved passwords
and MQTT usernames are never returned to the browser. Select the explicit open
network checkbox to clear the Wi-Fi password. Disabling MQTT authentication
removes its saved credentials. Fields outside the form, including custom
settings, are preserved unless explicitly changed.
Clearing a saved optional firmware setting restores its sketch-defined default.
The [JSON API](../configuration/http-json-api.md) remains available for automated
provisioning and custom configurators.
See [Resetting](resetting.md) for explicit factory reset and custom trigger times.

For firmware that must not contain any onboarding code or page, compile with
`HOMIE_CONFIG=0`. Such firmware cannot configure a blank device through its hotspot.

## Optional custom page

The built-in page is generic Homie firmware UI. Its fields and defaults come
from the host sketch and stored configuration; device IDs follow the compiled
Homie convention (v3, v4 or v5), without application-specific restrictions.
`GET /device-info` advertises `homie_convention_version` for configurators.

Edit `src/Homie/Boot/ConfigUi.html` and run
`python tools/generate_config_ui.py` to rebuild the compressed flash asset.
`make config-test` checks that the shipped asset and content-security hashes
match the source. The page has a 7 KiB compressed transfer budget and no external
fonts, scripts, styles or browser-storage dependency.

An existing `/homie/ui_bundle.gz` in the selected filesystem overrides the
built-in page. This is optional and remains useful for a custom interface.
Remove that file to return to the built-in page. See the
[data/homie folder](https://github.com/labodj/homie-esp8266/tree/develop/data/homie).

## Advanced configuration

The **Advanced settings** panel is closed by default. Existing values are loaded
and preserved even when it stays closed. New devices use automatic addressing
(DHCP), no access-point binding, and a 60-second statistics interval.

- Enable **Use a static IPv4 address** to enter address, subnet mask and gateway.
  DNS is optional; a secondary DNS requires a primary. Turning this option off
  explicitly removes all five stored address/DNS fields and returns to DHCP.
- Enable **Bind to one access point** to enter its BSSID (MAC) and channel.
  Channel `0` lets the device scan automatically. Turning it off removes both
  fields and allows roaming. Editing SSID clears an old binding; it can then be
  explicitly set for the new network.
- **Statistics interval** accepts the API's range `0`–`65535` seconds. An empty
  value restores the default of 60 seconds; `0` publishes continuously.

Disabled groups do not block form validation. If an enabled field is invalid,
the panel opens so the browser can show and focus it. Advanced values pass the
same Wi-Fi/MQTT connection test and verified save as the basic fields.

## Failure recovery and tests

Configuration writes are bounded, read back and promoted using a temporary file
and the previous configuration as backup. Boot restores the backup if promotion
was interrupted or the primary file is invalid. Recovery opens configuration
mode with the recovered ID and settings, allowing credentials to be reviewed
instead of silently trying an older network. A write error returns HTTP 500
and does not schedule a restart. A reset of the application alone preserves the
filesystem; a factory reset removes the primary, temporary and backup files.
This is application-level recovery, not a guarantee against damage to filesystem
metadata: actual power-cut tests on SPIFFS and LittleFS remain necessary.

A failed mount never authorizes erasing existing data. The firmware checks the
entire filesystem partition and initializes it automatically only if every byte
is erased (`0xff`), so a new board still starts its hotspot and setup page without
a filesystem upload. Missing partitions, flash read failures and any existing
data block automatic formatting. ESP8266's implicit auto-format is disabled for
both SPIFFS and LittleFS, including the SPIFFS probe in opt-in migration builds.

If existing storage cannot be mounted, the hotspot and page remain available,
but the form stays disabled and the configuration API returns HTTP 503. This is
not treated as a new device and the page does not suggest replacing its logical
ID with the hardware ID. Recover the existing filesystem or restore its backup
using the matching firmware/filesystem format before configuring it. Do not
erase or upload a new filesystem image to clear this error. Retaining unreadable
flash does not guarantee that corrupt metadata or an already lost ID can be
recovered. The explicitly enabled SPIFFS-to-LittleFS migration remains a separate
operation that formats the partition after copying the configuration into RAM.

A valid backup that cannot be promoted is also a recovery error, not an empty
device. Configuration reads, tests and writes remain blocked until recovery
succeeds; the page must not propose a hardware ID in place of the stored ID.
Changing SSID in the form removes any previous BSSID/channel binding while
retaining the logical ID and other settings.

Joining Wi-Fi may change the hotspot channel and briefly disconnect a phone.
Stay connected or reconnect to the same hotspot; the page keeps polling the test.
Unsaved passwords are not stored in browser storage. If a save response is lost,
check the usual network before retrying: the device may already have saved and
restarted. Do not factory-reset a device merely to recover a missing response.

Run `make config-test ARDUINOJSON_DIR=/path/to/ArduinoJson` for the actual inline
page, production HTTP/test loop and storage helper with simulated failures and
address/undefined-behavior sanitizers. These tests never join a real Wi-Fi network
or contact an installation broker. They do not replace phone and board tests.
