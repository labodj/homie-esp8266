// Private OTA implementation shared by BootNormal.cpp and the native fault-injection tests.
// Included after BootNormal and the async-state helpers have been declared.

#ifdef ESP32
#include <mutex>
#endif

namespace {
bool readOtaFlag(const volatile bool& flag) {
  AsyncStateCriticalGuard lock;
  return flag;
}

// Update is a singleton. Serialize its use between receive callbacks and loop
// cleanup, but never hold the async spinlock or call MQTT/user callbacks here.
#ifdef ESP32
std::mutex otaUpdateMutex;
struct OtaUpdateGuard {
  std::lock_guard<std::mutex> lock{otaUpdateMutex};
  explicit operator bool() const { return true; }
};
#else
// ESP8266 callbacks are cooperative. Do not reenter Update if a yield invokes
// another callback; loop cleanup can simply retry on its next iteration.
bool otaUpdateBusy = false;
struct OtaUpdateGuard {
  bool acquired = !otaUpdateBusy;
  OtaUpdateGuard() { if (acquired) otaUpdateBusy = true; }
  ~OtaUpdateGuard() { if (acquired) otaUpdateBusy = false; }
  explicit operator bool() const { return acquired; }
};
#endif
static_assert(HOMIE_OTA_TIMEOUT_MS > 0 && HOMIE_OTA_TIMEOUT_MS <= 0x7fffffffUL,
              "HOMIE_OTA_TIMEOUT_MS must fit in a positive signed 32-bit interval");
}  // namespace

void BootNormal::_resetOtaTransferState(bool preserveRequestedChecksum) {
  if (_otaUpdateStarted && !preserveRequestedChecksum) {
#ifdef ESP32
    Update.abort();
#else
    // ESP8266 has no abort(). end(false) alone can COMMIT an already complete
    // image. A deliberately non-hex digest guarantees cancellation in that case.
    Update.setMD5("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    Update.end(false);
#endif
  }
  _otaUpdateStarted = false;
  if (!preserveRequestedChecksum) _otaRequestedChecksum[0] = '\0';
  _otaIsBase64 = false;
  _otaBase64Pads = 0;
  _otaSizeTotal = 0;
  _otaSizeDone = 0;
  _otaPayloadTotal = 0;
  _otaPayloadProcessed = 0;
  _otaProgressPublishCounter = 0;
  _otaLastChunkAt = 0;
  _otaDecodeBuffer.reset();
  _otaDecodeBufferCapacity = 0;
  _otaOngoing.store(false);
}

void BootNormal::_failOtaUpdate(int status, const char* info, const __FlashStringHelper* reason) {
  Interface::get().getLogger() << F("OTA failed (") << status << F(" ") << info << F(")") << endl;
  Interface::get().getLogger() << reason << endl;
  // Release flash resources before making the failure visible to another attempt.
  _resetOtaTransferState();
  setFlag(_otaFailedPending);
  _queueOtaStatus(status, info);
}

void BootNormal::_abortOtaUpdateOnDisconnect() {
  // May be called while the receive callback is writing flash on another task.
  // Only the guarded service/receive paths are allowed to touch Update.
  setFlag(_otaDisconnected);
}

void BootNormal::_serviceOta() {
  if (!_otaOngoing.load()) return;
  OtaUpdateGuard guard;
  if (!guard) return;
  if (takeFlag(_otaDisconnected)) {
    if (_otaOngoing.load()) _failOtaUpdate(500, "DISCONNECTED", F("MQTT disconnected during OTA"));
  } else if (_otaOngoing.load() && uint32_t(millis() - _otaLastChunkAt) >= HOMIE_OTA_TIMEOUT_MS) {
    _failOtaUpdate(408, "TIMEOUT", F("No new OTA bytes before the inactivity timeout"));
  }
}

void BootNormal::_endOtaUpdate(bool success, uint8_t update_error, const char* phase) {
  if (success) {
    Interface::get().getLogger() << F("OTA succeeded") << endl;
    _resetOtaTransferState(true);
    setFlag(_otaSuccessfulPending);
    _queueOtaStatus(200);
    _flaggedForReboot.store(true);
    return;
  }

  Interface::get().getLogger() << F("OTA phase=") << phase << F(" error=") << unsigned(update_error)
                               << F(" raw=") << _otaPayloadProcessed << F("/") << _otaPayloadTotal
                               << F(" written=") << _otaSizeDone << F("/") << _otaSizeTotal
                               << F(" free_heap=") << ESP.getFreeHeap() << endl;
  int code = 500;
  const char* reason = "INTERNAL_ERROR";
  switch (update_error) {
    case UPDATE_ERROR_SIZE:
    case UPDATE_ERROR_MAGIC_BYTE:
#ifdef ESP8266
    case UPDATE_ERROR_NEW_FLASH_CONFIG:
#endif
      code = 400;
      reason = "BAD_FIRMWARE";
      break;
    case UPDATE_ERROR_MD5:
      code = 400;
      reason = "BAD_CHECKSUM";
      break;
    case UPDATE_ERROR_SPACE:
      code = 400;
      reason = "NOT_ENOUGH_SPACE";
      break;
    case UPDATE_ERROR_WRITE:
    case UPDATE_ERROR_ERASE:
    case UPDATE_ERROR_READ:
      reason = "FLASH_ERROR";
      break;
  }
  char info[HOMIE_OTA_STATUS_INFO_MAX_LENGTH];
  snprintf(info, sizeof(info), "%s %u %s", reason, unsigned(update_error), phase);
  _failOtaUpdate(code, info, F("Update API failed; status includes error number and phase"));
}

bool BootNormal::_writeOtaPayload(const uint8_t* payload, size_t length) {
  size_t writtenTotal = 0;
  while (writtenTotal < length) {
    if (readOtaFlag(_otaDisconnected)) return false;
    const size_t remaining = length - writtenTotal;
    const size_t chunkLength = remaining < OTA_FLASH_WRITE_SLICE_SIZE ? remaining : OTA_FLASH_WRITE_SLICE_SIZE;
    // Arduino's Update API is not const-correct but does not modify this buffer.
    const size_t written = Update.write(const_cast<uint8_t*>(payload + writtenTotal), chunkLength);
    if (written != chunkLength) return false;
    writtenTotal += written;
    yield();
  }
  return !readOtaFlag(_otaDisconnected);
}

bool HomieInternals::BootNormal::__handleOTAUpdates(char* topic, const uint8_t* payload, const espMqttClientTypes::MessageProperties& properties, size_t len, size_t index, size_t total, char* const* topicLevels, uint8_t topicLevelsCount) {
  (void)topic;
  if (topicLevelsCount != 5
      || strcmp(topicLevels[0], Interface::get().getConfig().get().deviceId) != 0
      || strcmp_P(topicLevels[1], PSTR("$implementation")) != 0
      || strcmp_P(topicLevels[2], PSTR("ota")) != 0
      || strcmp_P(topicLevels[3], PSTR("firmware")) != 0) return false;

  OtaUpdateGuard guard;
  if (!guard) {
    _queueOtaStatus(503, "BUSY");
    return true;
  }
  // A reconnect must not resume the preceding MQTT connection's partial image.
  if (takeFlag(_otaDisconnected) && _otaOngoing.load()) {
    _failOtaUpdate(500, "DISCONNECTED", F("Discarding OTA state from the previous connection"));
  }
  const char* firmwareMd5 = topicLevels[4];
  if (_flaggedForReboot.load()) {
    if (index == 0) {
      _queueOtaStatus(strcmp(firmwareMd5, _otaRequestedChecksum) == 0 ? 200 : 503,
                      strcmp(firmwareMd5, _otaRequestedChecksum) == 0 ? nullptr : "REBOOT_PENDING");
    }
    return true;
  }
  if (total == 0 || index > total || len > total - index || (len != 0 && payload == nullptr)) {
    _failOtaUpdate(400, "BAD_CHUNK", F("Invalid OTA payload bounds"));
    return true;
  }
  if (len == 0) return true;
  if (properties.retain) {
    _failOtaUpdate(400, "RETAINED_FIRMWARE", F("OTA firmware must not be retained"));
    return true;
  }

  if (index == 0 && !_otaOngoing.load()) {
    if (!Interface::get().getConfig().get().ota.enabled) {
      _queueOtaStatus(403);
      return true;
    }
    if (!Helpers::validateMd5(firmwareMd5)) {
      _endOtaUpdate(false, UPDATE_ERROR_MD5);
      return true;
    }
    if (strcmp(firmwareMd5, _fwChecksum) == 0) {
      _queueOtaStatus(304);
      return true;
    }
    // Never cancel a session owned by another Update consumer.
    if (Update.isRunning()) {
      _queueOtaStatus(503, "UPDATE_BUSY");
      return true;
    }
    strlcpy(_otaRequestedChecksum, firmwareMd5, sizeof(_otaRequestedChecksum));
    _otaPayloadTotal = total;
    _otaPayloadProcessed = 0;
    _otaProgressPublishCounter = 0;
    _otaSizeDone = 0;
    _otaBase64Pads = 0;
    _otaIsBase64 = payload[0] != 0xE9;
    // A binary ESP header starts with 0xE9; its Base64 encoding starts with '6'.
    if (_otaIsBase64 && (payload[0] != '6' || total % 4 != 0)) {
      _endOtaUpdate(false, UPDATE_ERROR_MAGIC_BYTE);
      return true;
    }
    _otaSizeTotal = _otaIsBase64 ? (total / 4) * 3 : total;
    base64_init_decodestate(&_otaBase64State);
    _otaLastChunkAt = millis();
    _otaOngoing.store(true);
    _queueOtaStatus(202);
    setFlag(_otaStartedPending);
  } else if (!_otaOngoing.load()) {
    return true;  // Discard the tail of a failed/rejected transfer.
  }

  if (strcmp(firmwareMd5, _otaRequestedChecksum) != 0) {
    _failOtaUpdate(400, "NOT_REQUESTED", F("OTA data belongs to a different firmware request"));
    return true;
  }
  if (total != _otaPayloadTotal) {
    _failOtaUpdate(400, "SIZE_CHANGED", F("OTA payload size changed mid-transfer"));
    return true;
  }

  const size_t rawChunkEnd = index + len;  // Bounds checked above, including overflow.
  if (index < _otaPayloadProcessed) {
    const size_t skip = _otaPayloadProcessed - index;
    if (skip >= len) return true;  // Duplicates do not extend the inactivity timeout.
    payload += skip;
    len -= skip;
    index += skip;
  }
  if (index != _otaPayloadProcessed) {
    _failOtaUpdate(400, "OUT_OF_SEQUENCE", F("OTA chunk arrived out of sequence"));
    return true;
  }

  size_t writeLength = len;
  const uint8_t* writePayload = payload;
  if (_otaIsBase64) {
    // Reject misplaced/nonterminal padding before giving bytes to libb64.
    size_t encodedLength = 0;
    for (size_t i = 0; i < len; ++i) {
      const uint8_t c = payload[i];
      const bool base64 = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                       || (c >= '0' && c <= '9') || c == '+' || c == '/';
      if (base64 && _otaBase64Pads == 0) {
        ++encodedLength;
      } else if (c == '=' && index + i >= total - 2 && _otaBase64Pads < 2) {
        ++_otaBase64Pads;
      } else {
        _endOtaUpdate(false, UPDATE_ERROR_MAGIC_BYTE, "BASE64");
        return true;
      }
    }
    writeLength = 0;
    if (encodedLength != 0) {
      if (encodedLength > INT_MAX) {
        _failOtaUpdate(400, "BAD_CHUNK", F("OTA Base64 chunk exceeds decoder limits"));
        return true;
      }
      // libb64 also writes its partial output byte. A separate len+1 buffer
      // handles one-character fragments without overlapping input or stack bytes.
      if (_otaDecodeBufferCapacity < encodedLength + 1) {
        std::unique_ptr<char[]> buffer(new (std::nothrow) char[encodedLength + 1]);
        if (!buffer) {
          _failOtaUpdate(500, "OUT_OF_MEMORY", F("Cannot allocate OTA decode buffer"));
          return true;
        }
        _otaDecodeBuffer = std::move(buffer);
        _otaDecodeBufferCapacity = encodedLength + 1;
      }
      writeLength = base64_decode_block(reinterpret_cast<const char*>(payload), int(encodedLength),
                                       _otaDecodeBuffer.get(), &_otaBase64State);
      writePayload = reinterpret_cast<const uint8_t*>(_otaDecodeBuffer.get());
    }
  }

  if (writeLength != 0) {
    if (!_otaUpdateStarted) {
      if (writePayload[0] != 0xE9) {
        _endOtaUpdate(false, UPDATE_ERROR_MAGIC_BYTE);
        return true;
      }
      if (!Update.begin(_otaSizeTotal)) {
        _endOtaUpdate(false, Update.getError(), "BEGIN");
        return true;
      }
      _otaUpdateStarted = true;
      // Both Arduino cores clear the expected digest inside begin().
      if (!Update.setMD5(_otaRequestedChecksum)) {
        _endOtaUpdate(false, UPDATE_ERROR_MD5, "SET_MD5");
        return true;
      }
    }
    if (!_writeOtaPayload(writePayload, writeLength)) {
      if (takeFlag(_otaDisconnected)) {
        _failOtaUpdate(500, "DISCONNECTED", F("MQTT disconnected while writing OTA"));
      } else {
        _endOtaUpdate(false, Update.getError(), "WRITE");
      }
      return true;
    }
    _otaSizeDone += writeLength;
  }

  _otaPayloadProcessed = rawChunkEnd;
  _otaLastChunkAt = millis();
  if (rawChunkEnd == total) {
    if (_otaIsBase64) {
      _otaSizeTotal -= _otaBase64Pads;  // Also when the last chunk contains only '='.
      const auto expectedStep = _otaBase64Pads == 2 ? step_c : (_otaBase64Pads == 1 ? step_d : step_a);
      if (_otaBase64State.step != expectedStep || (_otaBase64Pads != 0 && _otaBase64State.plainchar != 0)) {
        _endOtaUpdate(false, UPDATE_ERROR_MAGIC_BYTE, "BASE64");
        return true;
      }
    }
    if (!_otaUpdateStarted || _otaSizeDone != _otaSizeTotal) {
      _endOtaUpdate(false, UPDATE_ERROR_SIZE, "LENGTH");
      return true;
    }
    if (takeFlag(_otaDisconnected)) {
      _failOtaUpdate(500, "DISCONNECTED", F("MQTT disconnected before OTA commit"));
      return true;
    }
    const bool success = Update.end(_otaIsBase64);
    _endOtaUpdate(success, Update.getError(), "END");
    return true;
  }

  if (writeLength != 0) {
    {
      AsyncStateCriticalGuard lock;
      _otaProgressSizeDone = _otaSizeDone;
      _otaProgressSizeTotal = _otaSizeTotal;
      _otaProgressPending = true;
    }
    if (++_otaProgressPublishCounter >= 100) {
      char progress[32];
      snprintf(progress, sizeof(progress), "%lu/%lu", static_cast<unsigned long>(_otaSizeDone),
               static_cast<unsigned long>(_otaSizeTotal));
      _queueOtaStatus(206, progress);
      _otaProgressPublishCounter = 0;
    }
  }
  return true;
}
