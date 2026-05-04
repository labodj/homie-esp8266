# Miscellaneous

## Know if the device is configured / connected

If you need to run code in the Arduino `loop()` function, it can be useful to
know whether the device is configured (`normal` mode) and whether the network
connection is up.

```c++
void loop() {
  if (Homie.isConfigured()) {
    // The device is configured, in normal mode
    if (Homie.isConnected()) {
      // The device is connected
    } else {
      // The device is not connected
    }
  } else {
    // The device is not configured, in either configuration or standalone mode
  }
}
```

## Organize the sketch in a safe order

The example above demonstrates `Homie.isConnected()`, but most sketches should
avoid adding application work directly to `loop()`. ESP8266 is a
resource-constrained MCU, and Homie manages several runtime tasks. Interfering
with those tasks can lead to crashes. A safer Homie sketch shape is:

```c++
#include <Homie.h>

void setupHandler() {
  // Code which should run AFTER Wi-Fi is connected.
}

void loopHandler() {
  // Code which should run in normal loop(), after setupHandler() finished.
}

void setup() {
  Serial.begin(115200);
  Serial << endl << endl;

  // The underscore is not a typo. See Magic bytes.
  Homie_setFirmware("bare-minimum", "1.0.0");
  Homie.setSetupFunction(setupHandler).setLoopFunction(loopHandler);

  // Code which should run BEFORE Wi-Fi is connected.

  Homie.setup();
}

void loop() {
  Homie.loop();
}
```

This keeps the regular `setup()` / `loop()` structure while letting Homie decide
when your application setup and loop handlers can run safely. Avoid blocking
delays in those handlers as well.

## Why is `setupHandler()` needed in addition to `setup()` in Homie?

`setup()` starts execution immediately after power on or wake-up.
`setupHandler()` starts after Wi-Fi has established the connection.

## Why is `loopHandler()` needed in addition to `loop()` in Homie?

`loop()` starts after `setup()` is completed. `loopHandler()` is the safer place
for regular Homie work that should run only after network setup.

## Why is this understanding important?

1. Once your code in `setupHandler()` and `loopHandler()` is running,
   `Homie.isConnected()` is expected to be true unless the device moves out of
   Wi-Fi coverage.
2. The regular Arduino `loop()` often starts before Wi-Fi is connected. The
   Wi-Fi connected event can add significant MCU load because Wi-Fi tasks and
   initial MQTT reports happen around the same time.

Heavy commands, such as large initializations, long calculations, or filesystem
work, should run in one of two places:

- early in `setup()`, before Wi-Fi is connected
- after Wi-Fi is connected, using a non-blocking wait of a few seconds before
  starting the heavy work

## Access the configuration

You can get access to the configuration of the device. The representation of the
configuration is:

```c++
struct ConfigStruct {
  char* name;
  char* deviceId;

  struct WiFi {
    char* ssid;
    char* password;
  } wifi;

  struct MQTT {
    struct Server {
      char* host;
      uint16_t port;
    } server;
    char* baseTopic;
    bool auth;
    char* username;
    char* password;
  } mqtt;

  struct OTA {
    bool enabled;
  } ota;
};
```

For example, to access the Wi-Fi SSID, you would do:

```c++
Homie.getConfiguration().wifi.ssid;
```

## Get access to the MQTT client

You can get access to the underlying MQTT client. For example, to disconnect
from the broker:

```c++
Homie.getMqttClient().disconnect();
```

## Queue MQTT reports while Wi-Fi is not connected

```c++
#include <cppQueue.h>

HomieNode myNode("q-test", "test");

void setupHandler() {
  myNode.advertise("alert")
    .setName("Alert")
    .setDatatype("string")
    .setRetained(false);
}

// Queue postponed MQTT messages while Wi-Fi is not connected yet.
typedef struct strRec {
  char property[10];
  char msg[90];
} Rec;
// RAM is limited. Keep a reasonable queue length.
Queue msg_q(sizeof(Rec), 7, FIFO, true);

// Other code, including setup() and loop().

```

When Wi-Fi is not expected to be connected, queue the report:

```c++
Rec r = {"alert", "Low free storage space on SPIFFS."};
msg_q.push(&r);
```

To flush queued messages, add this to `loopHandler()`:

```c++
while (!msg_q.isEmpty() && Homie.isConnected()) {
  Rec r;
  msg_q.pop(&r);
  myNode.setProperty(r.property).send(r.msg);
}
```

Each queued property must be advertised before it can be sent. Configure
retention on `advertise()`, not on the queued send operation.

It can be useful to use the queue consistently, not only when Wi-Fi is down. This
keeps MQTT publishing predictable and reduces work in timing-sensitive paths.
