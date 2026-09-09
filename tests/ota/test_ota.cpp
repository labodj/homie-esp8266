// Exercise the production OTA implementation with deterministic Update faults.
// No broker, flash hardware, or replacement copy of the OTA state machine.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <libb64/cdecode.h>

#define F(s) s
#define PSTR(s) s
#define strcmp_P strcmp
using __FlashStringHelper = char;
using std::endl;
constexpr size_t OTA_FLASH_WRITE_SLICE_SIZE = 4096;
constexpr uint32_t HOMIE_OTA_TIMEOUT_MS = 60000;
constexpr size_t HOMIE_OTA_STATUS_INFO_MAX_LENGTH = 48;
enum { UPDATE_ERROR_OK, UPDATE_ERROR_WRITE, UPDATE_ERROR_ERASE, UPDATE_ERROR_READ,
       UPDATE_ERROR_SPACE, UPDATE_ERROR_SIZE, UPDATE_ERROR_STREAM, UPDATE_ERROR_MD5,
       UPDATE_ERROR_MAGIC_BYTE, UPDATE_ERROR_NEW_FLASH_CONFIG };
const char* checksum = "11111111111111111111111111111111";
const char* otherChecksum = "22222222222222222222222222222222";
uint32_t clockMs = 1;
struct { size_t getFreeHeap() { return 32768; } } ESP;
std::atomic<bool> failNextArrayAllocation{false};
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (failNextArrayAllocation.exchange(false)) return nullptr;
  try { return ::operator new[](size); } catch (const std::bad_alloc&) { return nullptr; }
}
uint32_t millis() { return clockMs; }
void yield() {}
size_t strlcpy(char* dst, const char* src, size_t size) {
  if (size) { std::strncpy(dst, src, size - 1); dst[size - 1] = '\0'; }
  return std::strlen(src);
}
struct String : std::string {
  using std::string::string;
  explicit String(size_t value) : std::string(std::to_string(value)) {}
  void concat(const char* value) { append(value); }
  void concat(size_t value) { append(std::to_string(value)); }
};
struct QuietLogger {
  template <class T> QuietLogger& operator<<(const T&) { return *this; }
  QuietLogger& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }
};
struct Config { const char* deviceId = "test"; struct { bool enabled = true; } ota; };
struct ConfigHolder { Config value; Config& get() { return value; } };
struct Interface {
  ConfigHolder config;
  QuietLogger logger;
  static Interface& get() { static Interface instance; return instance; }
  ConfigHolder& getConfig() { return config; }
  QuietLogger& getLogger() { return logger; }
};
namespace Helpers {
bool validateMd5(const char* text) {
  return std::strlen(text) == 32 && std::strspn(text, "0123456789abcdef") == 32;
}
}
namespace espMqttClientTypes { struct MessageProperties { bool dup = false; bool retain = false; }; }
std::mutex asyncMutex;
struct AsyncStateCriticalGuard { std::lock_guard<std::mutex> lock{asyncMutex}; };
void setFlag(volatile bool& value) { AsyncStateCriticalGuard lock; value = true; }
bool takeFlag(volatile bool& value) { AsyncStateCriticalGuard lock; bool result = value; value = false; return result; }

struct FakeUpdate {
  bool running = false, beginFailure = false, writeFailure = false, endFailure = false, md5Failure = false;
  uint8_t error = 0, injectedError = 0;
  size_t size = 0;
  unsigned beginCalls = 0, endCalls = 0, commits = 0;
  std::atomic<unsigned> abortCalls{0};
  std::string expected;
  std::vector<uint8_t> bytes;
  std::function<void()> writing;
  bool isRunning() const { return running; }
  bool begin(size_t value) {
    ++beginCalls;
    if (running) return false;  // Arduino's already-running path leaves error=0.
    error = injectedError;
    expected.clear();  // Both supported Arduino cores reset setMD5 in begin().
    if (beginFailure) return false;
    size = value;
    bytes.clear();
    running = true;
    return true;
  }
  bool setMD5(const char* value) {
    if (md5Failure) return false;
    expected = value;
    return true;
  }
  size_t write(uint8_t* data, size_t length) {
    if (writing) writing();
    if (writeFailure) { error = injectedError; return 0; }
    assert(running && bytes.size() + length <= size);
    bytes.insert(bytes.end(), data, data + length);
    return length;
  }
  void abort() { ++abortCalls; running = false; }
  bool end(bool allowShort = false) {
    ++endCalls;
    if (!running) return false;
    if (endFailure) {
      error = injectedError;
#ifdef ESP8266
      running = false;  // ESP8266 end() resets its state on failure.
#endif
      return false;
    }
    if ((!allowShort && bytes.size() != size) || (!expected.empty() && expected != checksum)) {
      error = UPDATE_ERROR_MD5;
      running = false;
      return false;
    }
    ++commits;
    running = false;
    return true;
  }
  uint8_t getError() const { return error; }
  void reset() {
    running = beginFailure = writeFailure = endFailure = md5Failure = false;
    error = injectedError = 0;
    size = beginCalls = endCalls = commits = 0;
    abortCalls = 0;
    expected.clear(); bytes.clear(); writing = nullptr;
  }
} Update;

namespace HomieInternals {
// Only hardware/MQTT plumbing is replaced; method definitions come from src/.
struct BootNormal {
  std::atomic<bool> _otaOngoing{false}, _flaggedForReboot{false};
  volatile bool _otaDisconnected = false;
  bool _otaUpdateStarted = false, _otaIsBase64 = false;
  uint32_t _otaLastChunkAt = 0;
  char _otaRequestedChecksum[33]{}, _fwChecksum[33] = "00000000000000000000000000000000";
  size_t _otaBase64Pads = 0, _otaSizeTotal = 0, _otaSizeDone = 0, _otaPayloadTotal = 0, _otaPayloadProcessed = 0;
  uint16_t _otaProgressPublishCounter = 0;
  base64_decodestate _otaBase64State{};
  std::unique_ptr<char[]> _otaDecodeBuffer;
  size_t _otaDecodeBufferCapacity = 0;
  volatile bool _otaStartedPending = false, _otaFailedPending = false, _otaSuccessfulPending = false, _otaProgressPending = false;
  volatile size_t _otaProgressSizeDone = 0, _otaProgressSizeTotal = 0;
  int status = 0;
  std::string info;
  void _queueOtaStatus(int code, const char* text = nullptr) { status = code; info = text ? text : ""; }
  void _resetOtaTransferState(bool = false);
  void _failOtaUpdate(int, const char*, const __FlashStringHelper*);
  void _abortOtaUpdateOnDisconnect();
  void _serviceOta();
  void _endOtaUpdate(bool, uint8_t = UPDATE_ERROR_OK, const char* = "VALIDATE");
  bool _writeOtaPayload(const uint8_t*, size_t);
  bool __handleOTAUpdates(char*, const uint8_t*, const espMqttClientTypes::MessageProperties&, size_t, size_t, size_t, char* const*, uint8_t);
};
}
using HomieInternals::BootNormal;
#ifndef OTA_IMPLEMENTATION
#define OTA_IMPLEMENTATION "../../src/Homie/Boot/BootNormalOta.ipp"
#endif
#include OTA_IMPLEMENTATION

const std::vector<uint8_t> binary{0xE9, 1, 2, 3};
void chunk(BootNormal& b, const uint8_t* data, size_t length, size_t index, size_t total,
           const char* md5 = checksum, bool retained = false) {
  char* levels[] = {const_cast<char*>("test"), const_cast<char*>("$implementation"),
                    const_cast<char*>("ota"), const_cast<char*>("firmware"), const_cast<char*>(md5)};
  espMqttClientTypes::MessageProperties props{};
  props.retain = retained;
  assert(b.__handleOTAUpdates(nullptr, data, props, length, index, total, levels, 5));
}
void send(BootNormal& b, const std::vector<uint8_t>& bytes, const char* md5 = checksum) {
  chunk(b, bytes.data(), bytes.size(), 0, bytes.size(), md5);
}
void retry(BootNormal& b) {
  assert(!b._otaOngoing && !Update.running && !b._flaggedForReboot);
  send(b, binary);
  assert(b.status == 200 && b._flaggedForReboot && Update.commits == 1);
}
void cleanupRegression() {
  BootNormal b;
  chunk(b, binary.data(), 2, 0, 4);
  chunk(b, binary.data() + 3, 1, 3, 4);  // Missing a byte after begin().
  assert(b.status >= 400);
  retry(b);
}
void checksumRegression() {
  BootNormal b;
  send(b, binary, otherChecksum);
  assert(b.status == 400 && b.info.find("BAD_CHECKSUM") == 0 && Update.commits == 0);
  retry(b);
}
void base64Fragmentation() {
  for (const std::string encoded : {"6Q==", "6QE=", "6QEC", "6QECAw=="}) {
    // All chunk partitions, including a one-character first chunk and padding-only tails.
    for (unsigned mask = 0; mask < (1U << (encoded.size() - 1)); ++mask) {
      Update.reset(); BootNormal b;
      size_t start = 0;
      for (size_t i = 1; i <= encoded.size(); ++i) {
        if (i == encoded.size() || (mask & (1U << (i - 1)))) {
          std::vector<uint8_t> part(encoded.begin() + start, encoded.begin() + i);
          chunk(b, part.data(), part.size(), start, encoded.size());
          start = i;
        }
      }
      assert(b.status == 200 && Update.commits == 1);
      const size_t expectedSize = encoded.size() == 8 ? 4 : (encoded[2] == '=' ? 1 : (encoded[3] == '=' ? 2 : 3));
      assert(Update.bytes == std::vector<uint8_t>(binary.begin(), binary.begin() + expectedSize));
    }
  }
}
void faults() {
  for (const std::string phase : {"BEGIN", "SET_MD5", "WRITE", "END"}) {
    Update.reset(); BootNormal b;
    Update.beginFailure = phase == "BEGIN";
    Update.md5Failure = phase == "SET_MD5";
    Update.writeFailure = phase == "WRITE";
    Update.endFailure = phase == "END";
    send(b, binary);
    assert(b.status >= 400 && b.info.find(phase) != std::string::npos && Update.commits == 0);
    Update.beginFailure = Update.md5Failure = Update.writeFailure = Update.endFailure = false;
    retry(b);
  }
  for (uint8_t error : {UPDATE_ERROR_WRITE, UPDATE_ERROR_ERASE, UPDATE_ERROR_READ, UPDATE_ERROR_SPACE}) {
    Update.reset(); BootNormal b; Update.writeFailure = true; Update.injectedError = error;
    send(b, binary);
    assert(b.status >= 400 && b.info.find(std::to_string(error) + " WRITE") != std::string::npos);
    Update.writeFailure = false; Update.injectedError = 0; retry(b);
  }
}
void malformedAndDuplicates() {
  for (std::string encoded : {"6Q=X", "6===", "6R==", "6QF=", "6Q!="}) {
    Update.reset(); BootNormal b;
    send(b, std::vector<uint8_t>(encoded.begin(), encoded.end()));
    assert(b.status == 400 && Update.commits == 0);
    retry(b);
  }
  Update.reset(); BootNormal b;
  chunk(b, binary.data(), 2, 0, 4);
  chunk(b, binary.data(), 2, 0, 4);
  chunk(b, binary.data() + 1, 3, 1, 4);
  assert(b.status == 200 && Update.bytes == binary && Update.commits == 1);
  send(b, binary);  // Duplicate after success must not write flash again.
  assert(b.status == 200 && Update.beginCalls == 1);
  send(b, binary, otherChecksum);
  assert(b.status == 503 && b.info == "REBOOT_PENDING" && Update.beginCalls == 1);

  for (unsigned fault = 0; fault < 5; ++fault) {
    Update.reset(); BootNormal c; chunk(c, binary.data(), 1, 0, 4);
    switch (fault) {
      case 0: chunk(c, binary.data(), 1, 1, 5); break;
      case 1: chunk(c, binary.data(), 1, 1, 4, otherChecksum); break;
      case 2: chunk(c, nullptr, 1, 1, 4); break;
      case 3: chunk(c, binary.data(), SIZE_MAX, 1, 4); break;
      case 4: chunk(c, binary.data(), 1, SIZE_MAX, 4); break;
    }
    assert(c.status >= 400); retry(c);
  }
}
void timeoutAndDisconnect() {
  for (bool wrap : {false, true}) {
    Update.reset(); BootNormal b; clockMs = wrap ? UINT32_MAX - 3 : 100;
    chunk(b, binary.data(), 1, 0, 4);
    clockMs += HOMIE_OTA_TIMEOUT_MS - 1;
    b._serviceOta(); assert(b._otaOngoing);
    chunk(b, binary.data(), 1, 0, 4); // Duplicate cannot keep a stalled transfer alive.
    ++clockMs; b._serviceOta(); assert(b.status == 408); retry(b);
  }
  Update.reset(); BootNormal b; chunk(b, binary.data(), 1, 0, 4);
  b._abortOtaUpdateOnDisconnect(); b._serviceOta(); assert(b.info == "DISCONNECTED"); retry(b);

  Update.reset(); BootNormal c; chunk(c, binary.data(), 1, 0, 4);
  c._abortOtaUpdateOnDisconnect(); send(c, binary); // Reconnect before loop consumed the event.
  assert(c.status == 200 && Update.commits == 1);
  c._abortOtaUpdateOnDisconnect(); c._serviceOta(); // Success must survive disconnect before reboot.
  assert(c._flaggedForReboot && c.status == 200);
}
void admission() {
  Update.reset(); BootNormal b;
  Update.running = true; send(b, binary);
  assert(b.status == 503 && b.info == "UPDATE_BUSY" && Update.running && Update.abortCalls == 0);
  Update.running = false;
  chunk(b, binary.data(), binary.size(), 0, binary.size(), checksum, true);
  assert(b.status == 400 && Update.beginCalls == 0);
  Interface::get().config.value.ota.enabled = false; send(b, binary); assert(b.status == 403);
  Interface::get().config.value.ota.enabled = true;
  send(b, binary, b._fwChecksum); assert(b.status == 304);
  send(b, binary, "invalid"); assert(b.status == 400);
  retry(b);
}
void allocationAndLargeWrites() {
  Update.reset(); BootNormal b;
  const std::string encoded = "6QECAw==";
  chunk(b, reinterpret_cast<const uint8_t*>(encoded.data()), 2, 0, encoded.size());
  failNextArrayAllocation = true;
  chunk(b, reinterpret_cast<const uint8_t*>(encoded.data() + 2), 6, 2, encoded.size());
  assert(b.status == 500 && b.info == "OUT_OF_MEMORY");
  retry(b);

  Update.reset(); BootNormal c;
  std::vector<uint8_t> large(20000, 0x3C); large[0] = 0xE9;
  send(c, large);
  assert(c.status == 200 && Update.bytes == large);

  Update.reset(); BootNormal d;
  assert(Update.begin(binary.size()));
  assert(Update.setMD5(checksum));
  assert(Update.write(const_cast<uint8_t*>(binary.data()), binary.size()) == binary.size());
  d._otaUpdateStarted = true; d._otaOngoing = true;
  d._failOtaUpdate(400, "BAD_CHUNK", "late rejection");
  assert(!Update.running && Update.commits == 0); // Cancellation must never commit even a complete image.
  retry(d);
}
void concurrentDisconnect() {
#ifdef ESP32
  Update.reset(); BootNormal b;
  std::mutex mutex; std::condition_variable cv; bool entered = false, resume = false;
  Update.writing = [&] {
    std::unique_lock<std::mutex> lock(mutex); entered = true; cv.notify_all();
    cv.wait(lock, [&] { return resume; });
  };
  std::thread receiver([&] { send(b, binary); });
  { std::unique_lock<std::mutex> lock(mutex); cv.wait(lock, [&] { return entered; }); }
  b._abortOtaUpdateOnDisconnect();
  std::thread service([&] { b._serviceOta(); });
  assert(Update.abortCalls == 0); // Cleanup cannot touch the updater during write().
  { std::lock_guard<std::mutex> lock(mutex); resume = true; }
  cv.notify_all(); receiver.join(); service.join();
  assert(Update.commits == 0 && b.info == "DISCONNECTED");
  Update.writing = nullptr; retry(b);
#endif
}

int main(int argc, char** argv) {
  const std::string test = argc > 1 ? argv[1] : "all";
  if (test == "cleanup" || test == "all") { Update.reset(); cleanupRegression(); }
  if (test == "md5" || test == "all") { Update.reset(); checksumRegression(); }
  if (test == "base64" || test == "all") base64Fragmentation();
  if (test == "all") { faults(); malformedAndDuplicates(); timeoutAndDisconnect(); admission(); allocationAndLargeWrites(); concurrentDisconnect(); }
  std::cout << "OTA " << test << " passed\n";
}
