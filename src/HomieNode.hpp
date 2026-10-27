#pragma once

#include <functional>
#include <memory>
#include <vector>
#include "Arduino.h"
#include "StreamingOperator.hpp"
#include "Homie/Utils/Helpers.hpp"
#include "Homie/Datatypes/Callbacks.hpp"
#include "Homie/Limits.hpp"
#include "HomieRange.hpp"
#include "SendingPromise.hpp"

class HomieNode;

namespace HomieInternals {
class HomieClass;
class Property;
class BootNormal;
class BootConfig;
class SendingPromise;

class PropertyInterface {
  friend ::HomieNode;

 public:
  PropertyInterface();

  PropertyInterface& settable(const PropertyInputHandler& inputHandler = [](const HomieRange& range, const String& value) { return false; });
  PropertyInterface& setName(const char* name);
  PropertyInterface& setUnit(const char* unit);
  PropertyInterface& setDatatype(const char* datatype);
  PropertyInterface& setFormat(const char* format);
  PropertyInterface& setRetained(const bool retained = true);

 private:
  PropertyInterface& setProperty(Property* property);

  Property* _property;
};

class Property {
  friend HomieNode;
  friend BootNormal;
  friend SendingPromise;

 public:
  explicit Property(const char* id)
    : _id(Helpers::cloneString(id)), _name(nullptr), _unit(nullptr), _datatype(nullptr), _format(nullptr), _retained(true), _settable(false) {}
  Property(const Property&) = delete;
  Property& operator=(const Property&) = delete;
  void settable(const PropertyInputHandler& inputHandler) { _settable = true;  _inputHandler = inputHandler; }
  void setName(const char* name) { _setMetadata(1, name); }
  void setUnit(const char* unit) { _setMetadata(2, unit); }
  void setDatatype(const char* datatype) { _setMetadata(3, datatype); }
  void setFormat(const char* format) { _setMetadata(4, format); }
  void setRetained(const bool retained = true) { _retained = retained; }


 private:
  const char* getId() const { return _id.get(); }
  const char* getName() const { return _name ? _name : ""; }
  const char* getUnit() const { return _unit ? _unit : ""; }
  const char* getDatatype() const { return _datatype ? _datatype : ""; }
  const char* getFormat() const { return _format ? _format : ""; }
  bool isRetained() const { return _retained; }
  bool isSettable() const { return _settable; }
  PropertyInputHandler getInputHandler() const { return _inputHandler; }
  void _setMetadata(size_t field, const char* value);
  std::unique_ptr<char[]> _id;
  const char* _name;
  const char* _unit;
  const char* _datatype;
  const char* _format;
  bool _retained;
  bool _settable;
  PropertyInputHandler _inputHandler;
};
}  // namespace HomieInternals

class HomieNode {
  friend HomieInternals::HomieClass;
  friend HomieInternals::BootNormal;
  friend HomieInternals::BootConfig;

 public:
  HomieNode(const char* id, const char* name, const char* type, bool range = false, uint16_t lower = 0, uint16_t upper = 0, const HomieInternals::NodeInputHandler& nodeInputHandler = [](const HomieRange& range, const String& property, const String& value) { return false; });
  virtual ~HomieNode();

  const char* getId() const { return _id.get(); }
  const char* getType() const { return _type; }
  const char* getName() const {return _name; }
  bool isRange() const { return _range; }
  uint16_t getLower() const { return _lower; }
  uint16_t getUpper() const { return _upper; }

  HomieInternals::PropertyInterface& advertise(const char* id);
  HomieInternals::SendingPromise& setProperty(const String& property) const;
  HomieInternals::Property* getProperty(const String& property) const;

  void setRunLoopDisconnected(bool runLoopDisconnected) {
    this->runLoopDisconnected = runLoopDisconnected;
  }

 protected:
  virtual void setup() {}
  virtual void loop() {}
  virtual void onReadyToOperate() {}
  virtual bool handleInput(const HomieRange& range, const String& property, const String& value);

 private:
  const std::vector<HomieInternals::Property*>& getProperties() const;

  static HomieNode* find(const char* id) {
    for (HomieNode* iNode : HomieNode::nodes) {
      if (strcmp(id, iNode->getId()) == 0) return iNode;
    }

    return 0;
  }


  std::unique_ptr<char[]> _id;
  const char* _name;
  const char* _type;
  bool _range;
  uint16_t _lower;
  uint16_t _upper;

  bool runLoopDisconnected;

  std::vector<HomieInternals::Property*> _properties;
  HomieInternals::NodeInputHandler _inputHandler;

  HomieInternals::PropertyInterface _propertyInterface;

  static std::vector<HomieNode*> nodes;
};
