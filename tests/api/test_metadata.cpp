// Exercise the production declarations and definitions with host transports.
#include <cassert>
#include <new>
#include <stdexcept>
#include <utility>
#include "../../src/HomieNode.hpp"
#include "../../src/HomieSetting.hpp"

bool failArrayAllocation = false;
size_t arrayAllocations = 0, lastArraySize = 0;
void* lastArrayPointer = nullptr;
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (failArrayAllocation) { failArrayAllocation = false; return nullptr; }
  try {
    void* result = ::operator new[](size);
    ++arrayAllocations;
    lastArraySize = size;
    lastArrayPointer = result;
    return result;
  } catch (...) { return nullptr; }
}

using namespace HomieInternals;
void Helpers::abort(const String& message) { throw std::runtime_error(message); }
#include "../../src/Homie/Utils/HelpersStrings.ipp"

struct { void _checkBeforeSetup(const String&) {} } Homie;
std::vector<HomieNode*> HomieNode::nodes;
bool HomieNode::handleInput(const HomieRange&, const String&, const String&) { return false; }
#include "../../src/HomieNodeMetadata.ipp"
#include "../../src/HomieSetting.cpp"

namespace HomieInternals {
class BootNormal {
 public:
  static Property& property(HomieNode& node) { return *node._properties.at(0); }
  static void check(const Property& p) {
    assert(std::strcmp(p.getId(), "temperature") == 0);
    assert(std::strcmp(p.getName(), "Degrees") == 0);
    assert(std::strcmp(p.getUnit(), "C") == 0);
    assert(std::strcmp(p.getDatatype(), "float") == 0);
    assert(std::strcmp(p.getFormat(), "-40:125") == 0);
  }
  static const char* name(const Property& p) { return p.getName(); }
  static const char* unit(const Property& p) { return p.getUnit(); }
  static const char* format(const Property& p) { return p.getFormat(); }
  static const char* id(const Property& p) { return p.getId(); }
  static void checkPacked(const Property& p) {
    assert(lastArrayPointer == p._id.get());
    const char* next = p._id.get() + std::strlen(p.getId()) + 1;
    for (const char* field : {p._name, p._unit, p._datatype, p._format}) {
      if (!field) continue;
      assert(field == next);
      next += std::strlen(field) + 1;
    }
    assert(lastArraySize == static_cast<size_t>(next - p._id.get()));
  }
};
}

struct MetadataSetting : IHomieSetting {
  MetadataSetting(const char* name, const char* description) : IHomieSetting(name, description) {}
};

int main() {
  char id[] = "sensor", name[] = "Room sensor", type[] = "temperature";
  // HomieNode deliberately forbids destruction; its registration lives forever.
  alignas(HomieNode) static unsigned char storage[sizeof(HomieNode)];
  size_t allocations = arrayAllocations;
  HomieNode& node = *new (storage) HomieNode(id, name, type);
  assert(arrayAllocations == allocations + 1);
  assert(lastArraySize == sizeof(id) + sizeof(name) + sizeof(type));
  assert(node.getName() == node.getId() + sizeof(id));
  assert(node.getType() == node.getName() + sizeof(name));
  id[0] = name[0] = type[0] = 'x';
  assert(std::strcmp(node.getId(), "sensor") == 0);
  assert(std::strcmp(node.getName(), "Room sensor") == 0);
  assert(std::strcmp(node.getType(), "temperature") == 0);
  alignas(HomieNode) static unsigned char unnamedStorage[sizeof(HomieNode)];
  HomieNode& unnamed = *new (unnamedStorage) HomieNode("n", nullptr, "sensor");
  assert(!unnamed.getName());
  assert(std::strcmp(unnamed.getId(), "n") == 0 && std::strcmp(unnamed.getType(), "sensor") == 0);

  {
    String propertyId = "temperature", propertyName = "Degrees", unit = "C";
    String datatype = "float", format = "-40:125";
    node.advertise(propertyId.c_str()).setName(propertyName.c_str()).setUnit(unit.c_str())
      .setDatatype(datatype.c_str()).setFormat(format.c_str());
  }
  Property& property = BootNormal::property(node);
  BootNormal::check(property);
  BootNormal::checkPacked(property);
  property.setName(BootNormal::name(property) + 1);
  assert(std::strcmp(BootNormal::name(property), "egrees") == 0);
  BootNormal::checkPacked(property);
  const char* currentName = BootNormal::name(property);
  allocations = arrayAllocations;
  property.setName(currentName);
  assert(arrayAllocations == allocations && BootNormal::name(property) == currentName);
  property.setUnit(nullptr);
  assert(std::strcmp(BootNormal::unit(property), "") == 0);
  BootNormal::checkPacked(property);
  allocations = arrayAllocations;
  property.setUnit("");
  assert(arrayAllocations == allocations);
  const char* currentId = BootNormal::id(property);
  failArrayAllocation = true;
  bool aborted = false;
  try { property.setName("replacement"); } catch (const std::runtime_error&) { aborted = true; }
  assert(aborted && std::strcmp(BootNormal::name(property), "egrees") == 0);
  assert(BootNormal::id(property) == currentId);
  assert(std::strcmp(BootNormal::format(property), "-40:125") == 0);
  BootNormal::checkPacked(property);
  property.setName(BootNormal::format(property) + 1);
  assert(std::strcmp(BootNormal::name(property), "40:125") == 0);
  assert(std::strcmp(BootNormal::format(property), "-40:125") == 0);
  property.setFormat(BootNormal::name(property));
  assert(std::strcmp(BootNormal::format(property), "40:125") == 0);
  BootNormal::checkPacked(property);
  property.setUnit(BootNormal::id(property));
  assert(std::strcmp(BootNormal::unit(property), "temperature") == 0);
  assert(std::strcmp(BootNormal::id(property), "temperature") == 0);
  BootNormal::checkPacked(property);
  property.setUnit(nullptr);
  for (unsigned i = 0; i < 100; ++i) {
    {
      const String temporary = "name-" + std::to_string(i);
      property.setName(temporary.c_str());
    }
    assert(String(BootNormal::name(property)) == "name-" + std::to_string(i));
    assert(std::strcmp(BootNormal::id(property), "temperature") == 0);
    assert(std::strcmp(BootNormal::format(property), "40:125") == 0);
    BootNormal::checkPacked(property);
  }

  char settingName[] = "offset", description[] = "Sensor offset";
  allocations = arrayAllocations;
  HomieSetting<bool> setting(settingName, description);
  assert(arrayAllocations == allocations + 1);
  assert(lastArraySize == sizeof(settingName) + sizeof(description));
  assert(setting.getDescription() == setting.getName() + sizeof(settingName));
  settingName[0] = description[0] = 'x';
  assert(std::strcmp(setting.getName(), "offset") == 0);
  assert(std::strcmp(setting.getDescription(), "Sensor offset") == 0);
  const char* nullable[] = {nullptr, "", "value"};
  for (const char* settingId : nullable) {
    for (const char* settingDescription : nullable) {
      allocations = arrayAllocations;
      MetadataSetting optional(settingId, settingDescription);
      assert(arrayAllocations == allocations + ((settingId || settingDescription) ? 1 : 0));
      assert(settingId ? optional.getName() && String(optional.getName()) == settingId : !optional.getName());
      assert(settingDescription ? optional.getDescription() && String(optional.getDescription()) == settingDescription : !optional.getDescription());
    }
  }
  MetadataSetting original("moving", "description");
  MetadataSetting moved(std::move(original));
  assert(!original.getName() && !original.getDescription());
  assert(String(moved.getName()) == "moving" && String(moved.getDescription()) == "description");
  MetadataSetting assigned(nullptr, nullptr);
  assigned = std::move(moved);
  assert(!moved.getName() && !moved.getDescription());
  assert(String(assigned.getName()) == "moving" && String(assigned.getDescription()) == "description");

  const char* strings[] = {nullptr, "", "abc", nullptr};
  auto packed = Helpers::cloneStrings(strings, strings, 4);
  assert(lastArraySize == 5 && !strings[0] && !strings[3]);
  assert(strings[1] == packed.get() && strings[2] == packed.get() + 1);
  assert(std::strcmp(strings[2], "abc") == 0);
  const char* empty[] = {nullptr, nullptr};
  allocations = arrayAllocations;
  assert(!Helpers::cloneStrings(empty, empty, 2) && arrayAllocations == allocations);
  auto copy = Helpers::cloneString(String("temporary"));
  assert(std::strcmp(copy.get(), "temporary") == 0);
  const String embeddedNull("a\0b", 3);
  copy = Helpers::cloneString(embeddedNull);
  assert(std::memcmp(copy.get(), "a\0b", 4) == 0);
  assert(!Helpers::cloneString(static_cast<const char*>(nullptr)));
}
