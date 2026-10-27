#include "../../src/HomieNode.hpp"

uint16_t publish(HomieNode& node) {
  return node.setProperty("state").setQos(1).send("true");
}
