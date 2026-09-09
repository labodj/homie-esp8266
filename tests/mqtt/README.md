# MQTT integration regression test

```sh
make mqtt-test MQTT_CLIENT_DIR=/path/to/installed/espMqttClient
```

Use the version pinned in `library.json`; CI and release validation install that
exact dependency automatically. A C++11 compiler and pthreads are required.

The test compiles Homie's production adapter and the original upstream MQTT
parser, client, async callbacks and transport. Only the TCP boundary is replaced.
Native helpers supply the real Linux mutex backend; the ESP8266 preprocessor
branch is used solely to load the async transport without emulating FreeRTOS.
Homie's ESP32 adapter itself is selected with `ESP32`.

Checks cover reentrant worker calls, a concurrent data callback during a held
foreground callback, nonblocking pump contention, immediate queued publishing,
connect/poll/disconnect callbacks and reconnect. The ESP8266 build checks that
the cooperative client remains unchanged. AddressSanitizer and
UndefinedBehaviorSanitizer failures terminate the test.

`make mqtt-message-test` also runs the actual private `BootNormalMqtt.ipp`
helpers: control ACKs with a full notification queue, duplicate/late ACKs,
disconnect reset, packet-ID wrap, and bounded payload allocation. Both buffer
modes are checked with 512-byte and 4096-byte limits, including rejection before
allocation and recovery after an oversized message. The adapter test verifies
that tracked publication waits for an active callback before registering its ID.

The host fixture seeds a valid empty receive-buffer pointer: upstream's empty
`memcpy` read otherwise violates the native nonnull precondition. This fixture
does not claim to fix that separate upstream issue. No device, broker or flash
is involved, so firmware builds and hardware OTA/latency tests remain necessary.

Defining `TEST_UNGUARDED` selects the upstream client without Homie's adapter;
the reentrant-pump assertion must fail. This is a negative control, not a test
configuration for CI.
