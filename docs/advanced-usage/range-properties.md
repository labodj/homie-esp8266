# Range properties

In the previous examples, node properties were advertised one by one, such as
`temperature` or `unit`. A LED strip may expose many similar properties, one for
each LED. Range properties let you model that repeated shape without declaring
each property separately.

```c++
HomieNode stripNode("strip", "strip");

bool ledHandler(const HomieRange& range, const String& value) {
  Homie.getLogger() << "LED " << range.index << " set to " << value << endl;

  // Now, let's update the actual state of the given led
  stripNode.setProperty("led").setRange(range).send(value);
}

void setup() {
  stripNode.advertiseRange("led", 1, 100).settable(ledHandler);
  // before Homie.setup()
}
```

On the MQTT broker you will see the following message show up:

```text
topic                               message
--------------------------------------------------------
homie/<device id>/strip/$type       strip
homie/<device id>/strip/$properties led[1-100]:settable
```

You can then publish the value `on` to topic `homie/<device id>/strip/led_1/set`
to turn on led number 1.

See the following example for a concrete use case:

[![GitHub logo](../assets/github.png) LedStrip](https://github.com/labodj/homie-esp8266/blob/develop/examples/LedStrip/LedStrip.ino)
