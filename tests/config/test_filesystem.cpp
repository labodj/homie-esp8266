// Compile the production mount policy against flash and filesystem faults.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#define F(value) value
#define HOMIE_FS_NAME "test"
using std::endl;

std::vector<uint8_t> flash(1024, 0xff);
int readFailure = -1, reads = 0;
bool partitionPresent = true;
void yield() {}
bool readFlash(size_t offset, void* data, size_t size) {
  ++reads;
  assert(offset <= flash.size() && size <= flash.size() - offset);
  if (readFailure >= 0 && offset >= static_cast<size_t>(readFailure)) return false;
  memcpy(data, flash.data() + offset, size);
  return true;
}
#ifdef ESP32
constexpr int ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 130;
constexpr int ESP_PARTITION_SUBTYPE_ANY = 255, ESP_OK = 0;
struct esp_partition_t { size_t size = 1024; } partition;
const esp_partition_t* esp_partition_find_first(int type, int subtype, const char* label) {
  assert(type == ESP_PARTITION_TYPE_DATA);
#if HOMIE_USE_LITTLEFS
  assert(subtype == ESP_PARTITION_SUBTYPE_ANY && std::string(label) == "spiffs");
#else
  assert(subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS && label == nullptr);
#endif
  return partitionPresent ? &partition : nullptr;
}
int esp_partition_read_raw(const esp_partition_t*, size_t offset, void* data, size_t size) {
  return readFlash(offset, data, size) ? ESP_OK : -1;
}
#else
constexpr uint32_t FS_start = 0x40201000;
uint32_t FS_end = FS_start + 1024;
#define FS_PHYS_ADDR (FS_start - 0x40200000)
#define FS_PHYS_SIZE (FS_end - FS_start)
struct {
  uint32_t getFlashChipRealSize() { return partitionPresent ? FS_PHYS_ADDR + flash.size() : 0; }
  bool flashRead(uint32_t address, uint32_t* data, size_t size) {
    assert(address >= FS_PHYS_ADDR);
    return readFlash(address - FS_PHYS_ADDR, data, size);
  }
} ESP;
#endif
struct FSConfig { bool autoFormat; explicit FSConfig(bool enabled) : autoFormat(enabled) {} };
using SPIFFSConfig = FSConfig;
using LittleFSConfig = FSConfig;
struct FSInfo {};
struct Filesystem {
  bool mountable = false, formattedMountable = true, formatOk = true, configureOk = true;
  bool autoFormat = true;
  int formats = 0, mounts = 0;
  bool setConfig(const FSConfig& config) { autoFormat = config.autoFormat; return configureOk; }
  bool info(FSInfo&) { return mountable; }
  bool format() {
    ++formats;
    if (!formatOk) return false;
    std::fill(flash.begin(), flash.end(), 0);
    mountable = formattedMountable;
    return true;
  }
  bool begin(bool formatOnFail) {
    ++mounts;
    return mountable || (formatOnFail && format() && mountable);
  }
  bool begin() { return begin(autoFormat); }
} HOMIE_FS;
namespace HomieInternals {
struct Config {
  bool _filesystemBegan = false, migrated = false;
  bool _filesystemBegin();
  bool _mountSelectedFilesystem(bool formatOnFail);
  bool _filesystemIsErased();
  bool _migrateSpiffsToLittleFs() { return migrated; }
};
struct Interface {
  std::ostringstream logger;
  static Interface& get() { static Interface instance; return instance; }
  std::ostringstream& getLogger() { return logger; }
};
}
using namespace HomieInternals;
#include "../../src/Homie/ConfigFilesystem.ipp"

void reset() {
  flash.assign(1024, 0xff); HOMIE_FS = Filesystem{};
  reads = 0; readFailure = -1; partitionPresent = true;
#ifdef ESP32
  partition.size = flash.size();
#else
  FS_end = FS_start + flash.size();
#endif
}
int main() {
  // A damaged filesystem may retain the ID far beyond its first metadata block.
  for (size_t offset : {size_t(0), size_t(257), size_t(1023)}) {
    reset(); flash[offset] = 'j'; const auto before = flash;
    Config config;
    assert(!config._filesystemBegin()); assert(HOMIE_FS.formats == 0 && flash == before);
    assert(!config._filesystemBegin()); assert(HOMIE_FS.formats == 0 && flash == before);
  }
  reset(); Config blank;
  assert(blank._filesystemBegin()); assert(HOMIE_FS.formats == 1);
  const int mounted = HOMIE_FS.mounts;
  assert(blank._filesystemBegin()); assert(HOMIE_FS.mounts == mounted);

  reset(); const std::string identity = "device_id=j4";
  std::copy(identity.begin(), identity.end(), flash.begin() + 900);
  const auto preserved = flash; Config damaged;
  assert(!damaged._filesystemBegin() && flash == preserved && HOMIE_FS.formats == 0);
  HOMIE_FS.mountable = true;
  assert(damaged._filesystemBegin() && flash == preserved && HOMIE_FS.formats == 0);

  reset(); flash[100] = 'j'; HOMIE_FS.mountable = true; Config valid;
  assert(valid._filesystemBegin()); assert(HOMIE_FS.formats == 0 && reads == 0 && flash[100] == 'j');

  for (int offset : {0, 256, 768}) {
    reset(); readFailure = offset; const auto before = flash; Config config;
    assert(!config._filesystemBegin()); assert(HOMIE_FS.formats == 0 && flash == before);
  }
  reset(); partitionPresent = false; Config missing;
  assert(!missing._filesystemBegin()); assert(HOMIE_FS.formats == 0);

  reset(); Config emptyPartition;
#ifdef ESP32
  partition.size = 0;
#else
  FS_end = FS_start;
#endif
  assert(!emptyPartition._filesystemBegin()); assert(HOMIE_FS.formats == 0);
#ifdef ESP8266
  reset(); HOMIE_FS.configureOk = false; Config unsafeConfig;
  assert(!unsafeConfig._filesystemBegin()); assert(HOMIE_FS.formats == 0 && HOMIE_FS.mounts == 0);
  reset(); HOMIE_FS.configureOk = false; HOMIE_FS.mountable = true; Config alreadyMounted;
  assert(alreadyMounted._filesystemBegin()); assert(HOMIE_FS.formats == 0 && HOMIE_FS.mounts == 0);
#endif

  reset(); HOMIE_FS.formatOk = false; Config formatFailure;
  assert(!formatFailure._filesystemBegin()); assert(HOMIE_FS.formats == 1);
  reset(); HOMIE_FS.formattedMountable = false; Config mountFailure;
  assert(!mountFailure._filesystemBegin()); assert(HOMIE_FS.formats == 1);
  const auto after = flash;
  assert(!mountFailure._filesystemBegin()); assert(HOMIE_FS.formats == 1 && flash == after);

  reset(); flash[100] = 'j'; Config migration; migration.migrated = true;
  assert(migration._filesystemBegin()); assert(HOMIE_FS.formats == 0 && flash[100] == 'j');
  std::cout << "Filesystem: blank initialization, existing data, mount/read/format failures and migration passed\n";
}
