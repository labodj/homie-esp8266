# Deep sleep

Before deep sleep, make sure all messages have been sent, including your
`HomieNode` property values and the `$online → false` update. Call
`Homie.prepareToSleep()` for that shutdown sequence.

The `MQTT_READY` event is emitted before the `loop()` method of each
`HomieNode` is called, so postpone `Homie.prepareToSleep()` until node
properties have been submitted over MQTT. The timer in this example provides
that short delay. Once `Homie.prepareToSleep()` disconnects cleanly, the
`READY_TO_SLEEP` event lets you call `Homie.doDeepSleep()`.

```c++
#include <Homie.h>
#include "Timer.h"

Timer t;

void prepareSleep() {
  Homie.prepareToSleep();
}

void onHomieEvent(const HomieEvent& event) {
  switch(event.type) {
    case HomieEventType::MQTT_READY:
      Homie.getLogger() << "MQTT connected, preparing for deep sleep after 100 ms..." << endl;
      t.after(100, prepareSleep);
      break;
    case HomieEventType::READY_TO_SLEEP:
      Homie.getLogger() << "Ready to sleep" << endl;
      Homie.doDeepSleep();
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial << endl << endl;
  Homie.onEvent(onHomieEvent);
  Homie.setup();
}

void loop() {
  Homie.loop();
  t.update();
}
```
