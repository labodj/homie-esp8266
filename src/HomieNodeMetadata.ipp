PropertyInterface::PropertyInterface()
: _property(nullptr) {
}

PropertyInterface& PropertyInterface::settable(const PropertyInputHandler& inputHandler) {
  _property->settable(inputHandler);
  return *this;
}

PropertyInterface& PropertyInterface::setName(const char* name) {
  _property->setName(name);
  return *this;
}

PropertyInterface& PropertyInterface::setUnit(const char* unit) {
  _property->setUnit(unit);
  return *this;
}

PropertyInterface& PropertyInterface::setDatatype(const char* datatype) {
  _property->setDatatype(datatype);
  return *this;
}

PropertyInterface& PropertyInterface::setFormat(const char* format) {
  _property->setFormat(format);
  return *this;
}

PropertyInterface& PropertyInterface::setRetained(const bool retained) {
  _property->setRetained(retained);
  return *this;
}

PropertyInterface& PropertyInterface::setProperty(Property* property) {
  _property = property;
  return *this;
}

void Property::_setMetadata(size_t field, const char* value) {
  if (value && !*value) value = nullptr;
  const char* strings[] = { _id.get(), _name, _unit, _datatype, _format };
  if (strings[field] == value) return;
  strings[field] = value;
  auto storage = Helpers::cloneStrings(strings, strings, 5);
  _id.swap(storage);
  _name = strings[1];
  _unit = strings[2];
  _datatype = strings[3];
  _format = strings[4];
}

HomieNode::HomieNode(const char* id, const char* name, const char* type, bool range, uint16_t lower, uint16_t upper, const NodeInputHandler& inputHandler)
: _id()
, _name(nullptr)
, _type(nullptr)
, _range(range)
, _lower(lower)
, _upper(upper)
, runLoopDisconnected(false)
, _properties()
, _inputHandler(inputHandler) {
  if (!id || !type || strlen(id) + 1 > MAX_NODE_ID_LENGTH || strlen(type) + 1 > MAX_NODE_TYPE_LENGTH) {
    Helpers::abort(F("✖ HomieNode(): either the id or type string is too long"));
    return;  // never reached, here for clarity
  }
  Homie._checkBeforeSetup(F("HomieNode::HomieNode"));

  const char* strings[] = { id, name, type };
  _id = Helpers::cloneStrings(strings, strings, 3);
  _name = strings[1];
  _type = strings[2];
  HomieNode::nodes.push_back(this);
}

HomieNode::~HomieNode() {
    Helpers::abort(F("✖✖ ~HomieNode(): Destruction of HomieNode object not possible\n  Hint: Don't create HomieNode objects as a local variable (e.g. in setup())"));
    return;  // never reached, here for clarity
}

PropertyInterface& HomieNode::advertise(const char* id) {
  if (!id || strlen(id) + 1 > MAX_NODE_PROPERTY_LENGTH) {
    Helpers::abort(F("✖ HomieNode::advertise(): property id string is too long"));
    return _propertyInterface;  // never reached, here for clarity
  }

  Property* propertyObject = new (std::nothrow) Property(id);
  if (!propertyObject || !propertyObject->getId()) {
    Helpers::abort(F("✖ HomieNode::advertise(): cannot allocate property"));
    return _propertyInterface;  // never reached, here for clarity
  }

  _properties.push_back(propertyObject);

  return _propertyInterface.setProperty(propertyObject);
}
