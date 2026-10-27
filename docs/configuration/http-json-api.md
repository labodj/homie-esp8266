# HTTP JSON API

When in `configuration` mode, the device exposes an HTTP JSON API for
configuration. When you send a valid configuration to the `/config`
endpoint, the configuration file is stored in the filesystem at
`/homie/config.json`.

If you prefer not to edit JSON by hand, use the built-in
[setup page](../advanced-usage/ui-bundle.md):

1. Connect to the `<brand>-<hardware ID>` hotspot spawned in `configuration` mode
   (normally `Homie-xxxxxxxxxxxx`).
2. Open `http://192.168.123.1` if the captive portal does not open automatically.
3. Enter the device identity, Wi-Fi and MQTT configuration, confirm the ID is
   unique, then test and save. Connection errors leave setup available.

No separate UI bundle or Internet connection is needed. After setup, the device
restarts in normal mode with its hotspot and web server closed.

Alternatively, you can use this `curl` command to send the configuration to the
device. You must connect to the device in `configuration` mode, while it is
acting as an access point. This method will not work outside `configuration`
mode:

```shell
curl -X PUT http://192.168.123.1/config --header "Content-Type: application/json" -d @config.json
```

This will send the `./config.json` file to the device.

## Error handling

When everything went fine, a `2xx` HTTP code is returned, such as `200 OK`,
`202 Accepted`, `204 No Content` and so on. If anything goes wrong, a return
code != 2xx will be returned, with a JSON `error` field indicating the error,
such as `500 Internal Server error`, `400 Bad request` and so on.

## Endpoints

**API base address:** `http://192.168.123.1`

??? summary "GET `/heart`"
    This is useful to ensure we are connected to the device AP.

    ## Response

    `204 No Content`

---

??? summary "GET `/device-info`"
    Get some information on the device.

    ## Response

    `200 OK (application/json)`

    ```json
    {
      "hardware_device_id": "52a8fa5d",
      "device_id": "my-device",
      "homie_esp8266_version": "4.1.0",
      "homie_convention_version": 3,
      "firmware": {
        "name": "awesome-device",
        "version": "1.0.0"
      },
      "nodes": [
        {
          "id": "light",
          "type": "light"
        }
      ],
      "settings": [
        {
          "name": "timeout",
          "description": "Timeout in seconds",
          "type": "ulong",
          "required": false,
          "default": 10
        }
      ]
    }
    ```

    `type` can be one of the following:

    * `bool`: a boolean
    * `ulong`: an unsigned long
    * `long`: a long
    * `double`: a double
    * `string`: a string

    !!! note "Note about settings"
        If a setting is not required, the `default` field will always be set.

    `device_id` is the saved logical ID when the configuration is valid, or the
    hardware ID for a blank device. `hardware_device_id` always identifies the chip.

---

??? summary "GET `/networks`"
    Retrieve the Wi-Fi networks the device can see.

    ## Response

    !!! success "In case of success"
        `200 OK (application/json)`

        ```json
        {
          "networks": [
            { "ssid": "Network_2", "rssi": -82, "encryption": "wep" },
            { "ssid": "Network_1", "rssi": -57, "encryption": "wpa" },
            { "ssid": "Network_3", "rssi": -65, "encryption": "wpa2" },
            { "ssid": "Network_5", "rssi": -94, "encryption": "none" },
            { "ssid": "Network_4", "rssi": -89, "encryption": "auto" },
            { "ssid": "Network_6", "rssi": -71, "encryption": "wpa3" }
          ]
        }
        ```

        `encryption` can be `wep`, `wpa`, `wpa2`, `wpa3`, `none`,
        `auto`, `wapi`, `owe` or `dpp`, depending on the Wi-Fi mode reported
        by the platform.

    !!! failure "In case the initial Wi-Fi scan is not finished on the device"
        `503 Service Unavailable (application/json)`

        ```json
        {
          "error": "Initial Wi-Fi scan not finished yet"
        }
        ```

---

??? summary "GET `/config`"
    Return `{"configured": false, "config": {}}` for a blank device, or the saved
    configuration with `configured: true`. Wi-Fi/MQTT passwords and MQTT usernames
    are removed. Responses are not cached. This endpoint exists only in setup mode.

---

??? summary "PUT `/config/test` and GET `/config/status`"
    Submit the proposed configuration as JSON to start an asynchronous connection
    test (`202 Accepted`). Poll `GET /config/status`: `status` is `idle`, `wifi`,
    `mqtt`, `ready`, `failed` or `saved`. On failure, `error` explains the problem;
    nothing is saved and the hotspot remains available. Wi-Fi has a 30-second
    deadline and MQTT a 15-second deadline. Duplicate active tests return HTTP 409.

    Tests use the proposed static IP/DNS/BSSID settings too. MQTT authentication
    is checked with a separate `setup-<hardware ID>` client ID, without
    publishing, subscribing or evicting an existing device. Brokers with client-ID
    restrictions must permit this setup ID. Authentication success is not proof of
    MQTT topic permissions or normal firmware readiness.

    Once ready, send the same proposed configuration to `PUT /config` to persist
    it. Changing values requires a new test. The built-in page follows this flow.

---

??? summary "PUT `/config`"
    Save the config to the device. Existing configuration is merged with supplied
    fields: omitted fields are preserved, `null` deletes a field and an explicit
    empty Wi-Fi password selects an open network. Passwords omitted by the web
    form remain on the device.

    Direct PUT without starting a test remains supported for compatibility with
    external provisioning tools; it does not verify connectivity. After a test
    starts, only a ready, unchanged candidate can be saved. Failed persistence
    returns HTTP 500 without rebooting; in-flight or changed candidates return 409.
    Success means the bytes were verified and stored, not that normal-mode MQTT
    advertisements or bridge operation have been verified.

    ## Request body

    `(application/json)`

    See [JSON configuration file](json-configuration-file.md).

    ## Response

    !!! success "In case of success"
        `200 OK (application/json)`

        ```json
        {
          "success": true
        }
        ```

    !!! failure "In case of error in the payload"
        `400 Bad Request (application/json)`

        ```json
        {
          "success": false,
          "error": "Reason why the payload is invalid"
        }
        ```

    !!! failure "In case the device already received a valid configuration and is waiting for reboot"
        `403 Forbidden (application/json)`

        ```json
        {
          "success": false,
          "error": "Device already configured"
        }
        ```

---

??? summary "PUT `/wifi/connect`"
    Initiates the connection of the device to the Wi-Fi network while in
    configuration mode. This request is not synchronous; obtain the result with
    `GET /wifi/status`.

    ## Request body

    `(application/json)`

    ```json
    {
      "ssid": "My_SSID",
      "password": "my-passw0rd"
    }
    ```

    ## Response

    !!! success "In case of success"
        `202 Accepted (application/json)`

        ```json
        {
          "success": true
        }
        ```

    !!! failure "In case of error in the payload"
        `400 Bad Request (application/json)`

        ```json
        {
          "success": false,
          "error": "Reason why the payload is invalid"
        }
        ```

---

??? summary "GET `/wifi/status`"
    Returns the current Wi-Fi connection status.

    Helpful when monitoring Wi-Fi connectivity after `PUT /wifi/connect`.

    ## Response

    `200 OK (application/json)`

    ```json
    {
      "status": "connected"
    }
    ```

    `status` might be one of the following:

    * `idle`
    * `connect_failed`
    * `connection_lost`
    * `no_ssid_available`
    * `connected` along with a `local_ip` field
    * `disconnected`

---

??? summary "PUT `/proxy/control`"
    Enable or disable the transparent proxy between AP and Station networks.

    All requests that do not collide with existing API paths are bridged to the
    destination according to the `Host` HTTP header. The destination host is
    called using the Wi-Fi connection established after `PUT /wifi/connect`, and
    all contents are bridged back to the connection made to the AP side.

    This feature can be used to help captive portals perform cloud API calls
    during device enrollment using the ESP8266 Wi-Fi AP connection without
    patching the Homie firmware. By using the transparent proxy, all operations
    can be performed by custom JavaScript running in the browser, from the
    configured filesystem location `/data/homie/ui_bundle.gz`. SPIFFS is used by
    default.

    HTTPS is not supported.

    **Important**: Keep HTTP requests and responses as small as possible because
    all contents are transported through limited RAM.

    ## Request body

    `(application/json)`

    ```json
    {
      "enable": true
    }
    ```

    ## Response

    ??? success "In case of success"
        `200 OK (application/json)`

        ```json
        {
          "success": true
        }
        ```

    ??? failure "In case of error in the payload"
        `400 Bad Request (application/json)`

        ```json
        {
          "success": false,
          "error": "Reason why the payload is invalid"
        }
        ```
