# UI bundle

An unconfigured device starts a Wi-Fi hotspot and serves a built-in setup page.
Connect to the `<brand>-<hardware ID>` network (normally `Homie-xxxxxxxxxxxx`)
and open `http://192.168.123.1` if the captive portal does not open automatically.
The page works offline and is included in the firmware: no filesystem upload,
external configurator or Internet connection is required.

Enter the device name, device ID, Wi-Fi and MQTT details, and any firmware settings.
When replacing a bridge, enter its existing ID and disconnect the old bridge
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
removes its saved credentials. Fields outside the form, including static IP and
custom settings, are preserved unless explicitly changed.
Use the [JSON API](../configuration/http-json-api.md) for configurations
with advanced fields such as static IP addresses.
See [Resetting](resetting.md) for explicit factory reset and custom trigger times.

For firmware that must not contain any onboarding code or page, compile with
`HOMIE_CONFIG=0`. Such firmware cannot configure a blank device through its hotspot.

## Optional custom page

An existing `/homie/ui_bundle.gz` in the selected filesystem overrides the
built-in page. This is optional and remains useful for a custom interface.
Remove that file to return to the built-in page. See the
[data/homie folder](https://github.com/labodj/homie-esp8266/tree/develop/data/homie).

## Failure recovery and tests

Configuration writes are bounded, read back and promoted using a temporary file
and the previous configuration as backup. Boot restores the backup if promotion
was interrupted or the primary file is invalid. Recovery opens configuration
mode with the recovered ID and settings, allowing credentials to be reviewed
instead of silently trying an older network. A write error returns HTTP 500
and does not schedule a restart. A reset of the application alone preserves the
filesystem; a factory reset removes the primary, temporary and backup files.
This is application-level recovery, not a guarantee against damage to filesystem
metadata: actual power-cut tests on SPIFFS and LittleFS remain necessary. The
existing mount-failure formatting policy is unchanged and can erase damaged
storage; a backup in the same filesystem cannot protect against that case.

Joining Wi-Fi may change the hotspot channel and briefly disconnect a phone.
Stay connected or reconnect to the same hotspot; the page keeps polling the test.
Unsaved passwords are not stored in browser storage. If a save response is lost,
check the usual network before retrying: the device may already have saved and
restarted. Do not factory-reset a device merely to recover a missing response.

Run `make config-test ARDUINOJSON_DIR=/path/to/ArduinoJson` for the actual inline
page, production HTTP/test loop and storage helper with simulated failures and
address/undefined-behavior sanitizers. These tests never join a real Wi-Fi network
or contact an installation broker. They do not replace phone and board tests.
