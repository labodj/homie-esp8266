#pragma once

#include "Arduino.h"
#include "StreamingOperator.hpp"
#include "Homie/Datatypes/Interface.hpp"
#include "HomieRange.hpp"

class HomieNode;

namespace HomieInternals {
class SendingPromise {
  friend ::HomieNode;

 public:
  SendingPromise();
  SendingPromise& setQos(uint8_t qos);
  // Kept for source compatibility. The advertised property metadata decides the
  // effective MQTT retain flag.
  SendingPromise& setRetained(bool retained);
  SendingPromise& overwriteSetter(bool overwrite);
  SendingPromise& setRange(const HomieRange& range);
  SendingPromise& setRange(uint16_t rangeIndex);
  uint16_t send(const String& value);

 private:
  // One SendingPromise instance is reused globally; reset prevents a previous
  // send's qos, range or command-topic state from leaking into the next publish.
  SendingPromise& reset();
  SendingPromise& setNode(const HomieNode& node);
  SendingPromise& setProperty(const String& property);

  const HomieNode* _node;
  const String* _property;
  uint8_t _qos;
  bool _overwriteSetter;
  HomieRange _range;
};
}  // namespace HomieInternals
