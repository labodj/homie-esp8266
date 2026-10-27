#pragma once

#include "Arduino.h"

#include <ArduinoJson.h>
#include "Datatypes/Interface.hpp"
#include "Datatypes/ConfigStruct.hpp"
#include "Utils/DeviceId.hpp"
#include "Utils/Filesystem.hpp"
#include "Utils/Validation.hpp"
#include "Constants.hpp"
#include "Limits.hpp"
#include "../HomieBootMode.hpp"
#include "../HomieSetting.hpp"
#include "../StreamingOperator.hpp"

namespace HomieInternals {
class Config {
 public:
  Config();
  bool load();
  inline const ConfigStruct& get() const;
  char* getSafeConfigFile() const;
  bool erase();
  bool setHomieBootModeOnNextBoot(HomieBootMode bootMode);
  HomieBootMode getHomieBootModeOnNextBoot();
  bool write(const JsonObject config);
  bool prepare(JsonDocument& document, const JsonObject changes);
  bool patch(const char* patch);
  void log() const;  // print the current config to log output
  bool isValid() const;
  bool wasRecovered() const { return _recovered; }
  bool isStorageReady() const { return _filesystemBegan && !_recoveryFailed; }

 private:
  ConfigStruct _configStruct;
  bool _filesystemBegan;
  bool _valid;
  bool _recovered;
  bool _recoveryFailed;

  bool _filesystemBegin();
  bool _loadFile(const char* path);
  bool _mountSelectedFilesystem(bool formatOnFail);
  bool _filesystemIsErased();
  bool _ensureFilesystemDirectories();
  bool _migrateSpiffsToLittleFs();
  void _patchJsonObject(JsonObject object, JsonObjectConst patch);
};

const ConfigStruct& Config::get() const {
  return _configStruct;
}
}  // namespace HomieInternals
