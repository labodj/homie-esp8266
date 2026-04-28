#include "SendingPromise.hpp"
#include "Homie/Utils/Helpers.hpp"

using namespace HomieInternals;

namespace {
uint8_t decimalDigits(uint16_t value) {
  uint8_t digits = 1;
  while (value >= 10) {
    value = static_cast<uint16_t>(value / 10);
    ++digits;
  }
  return digits;
}
}  // namespace

SendingPromise::SendingPromise()
: _node(nullptr)
, _property(nullptr)
, _qos(0)
, _retained(false)
, _setRetained(true)
, _overwriteSetter(false)
, _range { .isRange = false, .index = 0 } {
}

SendingPromise& SendingPromise::reset() {
  _node = nullptr;
  _property = nullptr;
  _qos = 0;
  _retained = false;
  _setRetained = true;
  _overwriteSetter = false;
  _range = { .isRange = false, .index = 0 };
  return *this;
}

SendingPromise& SendingPromise::setQos(uint8_t qos) {
  _qos = qos;
  return *this;
}

SendingPromise& SendingPromise::setRetained(bool retained) {
  _retained = retained;
  return *this;
}

SendingPromise& SendingPromise::setSetRetained(bool retained) {
  _setRetained = retained;
  return *this;
}

SendingPromise& SendingPromise::overwriteSetter(bool overwrite) {
  _overwriteSetter = overwrite;
  return *this;
}

SendingPromise& SendingPromise::setRange(const HomieRange& range) {
  _range = range;
  return *this;
}

SendingPromise& SendingPromise::setRange(uint16_t rangeIndex) {
  HomieRange range;
  range.isRange = true;
  range.index = rangeIndex;
  _range = range;
  return *this;
}

uint16_t SendingPromise::send(const String& value) {
  if (!Interface::get().ready) {
    Interface::get().getLogger() << F("✖ setNodeProperty(): impossible now") << endl;
    return 0;
  }
  if (!_node || !_property) {
    Interface::get().getLogger() << F("✖ setNodeProperty(): missing node or property") << endl;
    return 0;
  }

  char rangeSuffix[1 + 5 + 1] = {0};  // separator + max uint16_t + NUL
  if (_range.isRange) {
    char rangeStr[5 + 1];  // max 65535
    utoa(_range.index, rangeStr, 10);
#if HOMIE_CONVENTION_V5
    rangeSuffix[0] = '-';
#else
    rangeSuffix[0] = '_';
#endif
    memcpy(rangeSuffix + 1, rangeStr, decimalDigits(_range.index) + 1);
    _range.isRange = false;                  //FIXME: This is a workaround. Problem is that Range is loaded from the property into SendingPromise, but the SendingPromise is global. (one SendingPromise for the HomieClass instance
    _range.index = 0;
  }

  const size_t topicLength = Helpers::mqttDeviceBaseTopicLength(
                               Interface::get().getConfig().get().mqtt.baseTopic,
                               Interface::get().getConfig().get().deviceId
                             )
                           + 1
                           + strlen(_node->getId())
                           + strlen(rangeSuffix)
                           + 1
                           + _property->length();
#if HOMIE_CONVENTION_V5
  const size_t requiredTopicLength = topicLength;
#else
  const size_t requiredTopicLength = _overwriteSetter ? topicLength + strlen_P(PSTR("/set")) : topicLength;
#endif

  if (requiredTopicLength + 1 > MAX_MQTT_TOPIC_LENGTH) {
    Interface::get().getLogger() << F("✖ setNodeProperty(): MQTT topic too long") << endl;
    return 0;
  }

  char topic[MAX_MQTT_TOPIC_LENGTH];
  Helpers::buildMqttDeviceBaseTopic(topic, Interface::get().getConfig().get().mqtt.baseTopic, Interface::get().getConfig().get().deviceId);
  strcat_P(topic, PSTR("/"));
  strcat(topic, _node->getId());
  strcat(topic, rangeSuffix);
  strcat_P(topic, PSTR("/"));
  strcat(topic, _property->c_str());

  uint16_t packetId;
#if HOMIE_CONVENTION_V5
  if (value.length() == 0) {
    // Homie v5 represents an actual empty string value with one NUL byte,
    // because an MQTT zero-length retained payload deletes the retained topic.
    const char emptyStringPayload = '\0';
    packetId = Interface::get().getMqttClient().publish(topic, _qos, _retained, &emptyStringPayload, 1);
  } else {
    packetId = Interface::get().getMqttClient().publish(topic, _qos, _retained, value.c_str());
  }
#else
  packetId = Interface::get().getMqttClient().publish(topic, _qos, _retained, value.c_str());
#endif

#if HOMIE_CONVENTION_V5
  if (_overwriteSetter) {
    Interface::get().getLogger() << F("! overwriteSetter(true) is ignored in Homie v5 mode; devices must not publish command topics") << endl;
  }
#else
  if (_overwriteSetter) {
    strcat_P(topic, PSTR("/set"));
    Interface::get().getMqttClient().publish(topic, 2, _setRetained, value.c_str());
  }
#endif

  return packetId;
}

SendingPromise& SendingPromise::setNode(const HomieNode& node) {
  _node = &node;
  return *this;
}

SendingPromise& SendingPromise::setProperty(const String& property) {
  _property = &property;
  return *this;
}

const HomieNode* SendingPromise::getNode() const {
  return _node;
}

const String* SendingPromise::getProperty() const {
  return _property;
}

uint8_t SendingPromise::getQos() const {
  return _qos;
}

HomieRange SendingPromise::getRange() const {
  return _range;
}

bool SendingPromise::isRetained() const {
  return _retained;
}

bool SendingPromise::doesOverwriteSetter() const {
  return _overwriteSetter;
}
