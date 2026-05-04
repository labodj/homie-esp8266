#include "SendingPromise.hpp"

#if HOMIE_STRICT_PROPERTY_VALIDATION
#include "Homie/Utils/ConventionValidation.hpp"
#endif
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
, _overwriteSetter(false)
, _range { .isRange = false, .index = 0 } {
}

SendingPromise& SendingPromise::reset() {
  _node = nullptr;
  _property = nullptr;
  _qos = 0;
  _overwriteSetter = false;
  _range = { .isRange = false, .index = 0 };
  return *this;
}

SendingPromise& SendingPromise::setQos(uint8_t qos) {
  _qos = qos;
  return *this;
}

SendingPromise& SendingPromise::setRetained(bool retained) {
  (void)retained;
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

  Property* propertyObject = _node->getProperty(*_property);
  if (!propertyObject) {
    Interface::get().getLogger() << F("✖ setNodeProperty(): property is not advertised") << endl;
    return 0;
  }

  const bool propertyRetained = propertyObject->isRetained();
#if HOMIE_STRICT_PROPERTY_VALIDATION
  const char* propertyDatatype = propertyObject->getDatatype();
  const char* propertyFormat = propertyObject->getFormat();
  if (!ConventionValidation::payloadMatches(propertyDatatype, propertyFormat, value.c_str(), value.length())) {
    Interface::get().getLogger() << F("✖ setNodeProperty(): payload does not match advertised Homie datatype/format") << endl;
    return 0;
  }
#endif

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
    // SendingPromise is a shared builder. Clear the transient range after use
    // so the next property publish cannot accidentally reuse it.
    _range.isRange = false;
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
  const size_t requiredTopicLength = topicLength;

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

  uint8_t qos = _qos;
  bool retained = propertyRetained;
#if HOMIE_CONVENTION_V5
  if (!retained) {
    qos = 0;
  }
#endif

  uint16_t packetId;
#if HOMIE_CONVENTION_V5
  if (value.length() == 0) {
    // Homie v5 represents an actual empty string value with one NUL byte,
    // because an MQTT zero-length retained payload deletes the retained topic.
    const char emptyStringPayload = '\0';
    packetId = Interface::get().getMqttClient().publish(topic, qos, retained, &emptyStringPayload, 1);
  } else {
    packetId = Interface::get().getMqttClient().publish(topic, qos, retained, value.c_str());
  }
#else
  packetId = Interface::get().getMqttClient().publish(topic, qos, retained, value.c_str());
#endif

  if (_overwriteSetter) {
    Interface::get().getLogger() << F("! overwriteSetter(true) is ignored; Homie devices must not publish command topics") << endl;
  }

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
