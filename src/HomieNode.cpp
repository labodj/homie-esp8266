#include "HomieNode.hpp"
#include "Homie.hpp"

#include <new>

using namespace HomieInternals;

std::vector<HomieNode*> HomieNode::nodes;

#include "HomieNodeMetadata.ipp"

SendingPromise& HomieNode::setProperty(const String& property) const {
  Property* iProperty = this->getProperty(property);
  if (iProperty && iProperty->isRetained()) {
    return Interface::get().getSendingPromise().reset().setNode(*this).setProperty(property).setQos(1);
  }

#if HOMIE_CONVENTION_V5
  return Interface::get().getSendingPromise().reset().setNode(*this).setProperty(property).setQos(0);
#else
  return Interface::get().getSendingPromise().reset().setNode(*this).setProperty(property).setQos(1);
#endif
}

Property* HomieNode::getProperty(const String& property) const {
  for (Property* iProperty : getProperties()) {
    if (strcmp(iProperty->getId(), property.c_str()) == 0)
       return iProperty;
  }
  return NULL;
}

bool HomieNode::handleInput(const HomieRange& range, const String& property, const String& value) {
  return _inputHandler(range, property, value);
}

const std::vector<HomieInternals::Property*>& HomieNode::getProperties() const {
  return _properties;
}
