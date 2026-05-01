# Input handlers

There are four types of input handlers:

- Global input handler. This unique handler handles every changed settable
  property for all nodes.

```c++
bool globalInputHandler(
  const HomieNode& node,
  const HomieRange& range,
  const String& property,
  const String& value
) {

}

void setup() {
  Homie.setGlobalInputHandler(globalInputHandler); // before Homie.setup()
  // ...
}
```

- Node input handlers. This handler handles every changed settable property of a
  specific node.

```c++
bool nodeInputHandler(
  const HomieRange& range,
  const String& property,
  const String& value
) {

}

HomieNode node("id", "type", false, 0, 0, nodeInputHandler);
```

- Virtual callback from node input handler

You can create your own class derived from HomieNode that implements the virtual
method `bool HomieNode::handleInput(...)`. The default node input handler then
automatically calls your callback.

```c++
class RelaisNode : public HomieNode {
 public:
  RelaisNode(): HomieNode("Relais", "switch8");

 protected:
  virtual bool handleInput(
    const HomieRange& range,
    const String& property,
    const String& value
  ) {

  }
};
```

- Property input handlers. This handler handles changes for a specific settable
  property of a specific node.

```c++
bool propertyInputHandler(const HomieRange& range, const String& value) {

}

HomieNode node("id", "type");

void setup() {
  // before Homie.setup()
  node.advertise("property").settable(propertyInputHandler);
  // ...
}
```

Input handlers return a boolean. An input handler can decide whether it handled
the message or whether the message should continue to the next input handler. If
an input handler returns `true`, propagation stops. If it returns `false`,
propagation continues. The order is global handler, then node handler, then
property handler.

For example, imagine you defined three input handlers: the global one, the node
one, and the property one. If the global input handler returns `false`, the node
input handler is called. If the node input handler returns `true`, propagation
stops and the property input handler is not called. You can think of the
handlers as middleware.

!!! warning
    Homie uses [ESPAsyncTCP](https://github.com/me-no-dev/ESPAsyncTCP) for
    network communication, which uses asynchronous callbacks from the ESP8266
    framework for incoming network packets. The input handler therefore runs in a
    different task than the `loopHandler()`, so the network task may interrupt
    your loop at any time.
