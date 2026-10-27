bool Config::_filesystemBegin() {
  if (!_filesystemBegan) {
    // Always try a non-formatting mount first. In a LittleFS migration build a
    // mount failure is the signal that a provisioned SPIFFS image may still
    // exist on the same flash area and must be read before anything is erased.
    if (!_mountSelectedFilesystem(false)) {
      if (_migrateSpiffsToLittleFs()) {
        return true;
      }

      if (_filesystemIsErased()) {
        Interface::get().getLogger() << F("Initializing empty ") << HOMIE_FS_NAME << endl;
        _mountSelectedFilesystem(true);
      } else {
        Interface::get().getLogger() << F("Storage is not empty or cannot be read; preserving it without formatting") << endl;
      }
    }

    if (!_filesystemBegan) Interface::get().getLogger() << F("✖ Cannot mount ") << HOMIE_FS_NAME << endl;
  }

  return _filesystemBegan;
}

bool Config::_mountSelectedFilesystem(bool formatOnFail) {
#ifdef ESP32
  _filesystemBegan = HOMIE_FS.begin(formatOnFail);
#elif defined(ESP8266)
#if HOMIE_USE_LITTLEFS
  const bool configured = HOMIE_FS.setConfig(LittleFSConfig(false));
#else
  const bool configured = HOMIE_FS.setConfig(SPIFFSConfig(false));
#endif
  if (!configured) {
    // ESP8266 rejects config changes on a filesystem the sketch already mounted.
    FSInfo info;
    _filesystemBegan = HOMIE_FS.info(info);
    return _filesystemBegan;
  }
  _filesystemBegan = HOMIE_FS.begin();
  if (!_filesystemBegan && formatOnFail) {
    if (HOMIE_FS.format()) {
      _filesystemBegan = HOMIE_FS.begin();
    }
  }
#endif

  return _filesystemBegan;
}

bool Config::_filesystemIsErased() {
#ifdef ESP32
#if HOMIE_USE_LITTLEFS
  const auto* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
#else
  const auto* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
#endif
  if (!partition) return false;
  const size_t size = partition->size;
#elif defined(ESP8266)
  if (FS_start < 0x40200000 || FS_end <= FS_start) return false;
  const size_t size = FS_PHYS_SIZE;
  const uint32_t flashSize = ESP.getFlashChipRealSize();
  if (FS_PHYS_ADDR > flashSize || size > flashSize - FS_PHYS_ADDR) return false;
#endif
  if (size == 0 || size % sizeof(uint32_t) != 0) return false;
  uint32_t buffer[64];
  for (size_t offset = 0; offset < size; offset += sizeof(buffer)) {
    const size_t count = size - offset < sizeof(buffer) ? size - offset : sizeof(buffer);
#ifdef ESP32
    if (esp_partition_read_raw(partition, offset, buffer, count) != ESP_OK) return false;
#elif defined(ESP8266)
    if (!ESP.flashRead(FS_PHYS_ADDR + offset, buffer, count)) return false;
#endif
    for (size_t i = 0; i < count / sizeof(uint32_t); ++i) {
      if (buffer[i] != UINT32_MAX) return false;
    }
    yield();
  }
  return true;
}
