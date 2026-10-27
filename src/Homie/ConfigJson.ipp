char* Config::getSafeConfigFile() const {
  File configFile = HOMIE_FS.open(CONFIG_FILE_PATH, "r");
  size_t configSize = configFile.size();

  char buf[MAX_JSON_CONFIG_FILE_SIZE];
  const bool readable = configFile && configSize < sizeof(buf);
  const size_t configRead = readable ? configFile.readBytes(buf, configSize) : 0;
  configFile.close();
  buf[readable ? configSize : 0] = '\0';

  StaticJsonDocument<MAX_JSON_CONFIG_ARDUINOJSON_BUFFER_SIZE> jsonDoc;
  if (!readable || configRead != configSize || deserializeJson(jsonDoc, buf) != DeserializationError::Ok
      || !jsonDoc.is<JsonObject>()) jsonDoc.to<JsonObject>();
  JsonObject parsedJson = jsonDoc.as<JsonObject>();
  parsedJson["wifi"].as<JsonObject>().remove("password");
  JsonObject mqtt = parsedJson["mqtt"].as<JsonObject>();
  mqtt.remove("username");
  mqtt.remove("password");
  // Runtime-only diagnostics are injected when advertising over MQTT. Do not
  // leak a stale value if a previous tool copied the advertised payload back
  // into config.json.
  mqtt.remove("effective_base_topic");

  size_t jsonBufferLength = measureJson(jsonDoc) + 1;
  char* jsonString = new char[jsonBufferLength];
  serializeJson(jsonDoc, jsonString, jsonBufferLength);
  return jsonString;
}

bool Config::prepare(JsonDocument& document, const JsonObject changes) {
  if (_recoveryFailed) return false;
  document.to<JsonObject>();
  if (_valid) {
    File file = HOMIE_FS.open(CONFIG_FILE_PATH, "r");
    if (!file || file.size() >= MAX_JSON_CONFIG_FILE_SIZE) return false;
    const auto error = deserializeJson(document, file);
    file.close();
    if (error != DeserializationError::Ok || !document.is<JsonObject>()) return false;
  }
  _patchJsonObject(document.as<JsonObject>(), changes);
  return !document.overflowed();
}

void Config::_patchJsonObject(JsonObject object, JsonObjectConst patch) {
  for (JsonPairConst patchItem : patch) {
    JsonVariantConst patchElement = patchItem.value();
    JsonVariant objectElement = object[patchItem.key()];

    // If both object element and patch element are objects, then recursively call this method again
    if (objectElement.is<JsonObject>() && patchElement.is<JsonObjectConst>()) {
      _patchJsonObject(objectElement.as<JsonObject>(), patchElement.as<JsonObjectConst>());
    } else {
      // Delete the object element if the patch element is null
      if (patchElement.isNull()) {
        object.remove(patchItem.key());
      } else {
        // Otherwise replace the object element value with the patch element value
        object[patchItem.key()] = patchElement;
      }
    }
  }
}
