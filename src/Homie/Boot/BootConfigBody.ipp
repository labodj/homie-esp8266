void BootConfig::__parsePost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (total > MAX_POST_SIZE || index > total || len > total - index) {
    free(request->_tempObject);
    request->_tempObject = nullptr;
    Interface::get().getLogger() << F("Request is to large to be processed.") << endl;
  } else {
    if (index == 0) {
      if (request->_tempObject != nullptr) {
        free(request->_tempObject);
        request->_tempObject = nullptr;
        return;
      }
      request->_tempObject = calloc(total + 1, 1);  // AsyncWebServer releases this with free().
      if (request->_tempObject == nullptr) {
        Interface::get().getLogger() << F("Cannot allocate request body buffer.") << endl;
        return;
      }
    } else if (request->_tempObject == nullptr) {
      Interface::get().getLogger() << F("Missing request body buffer.") << endl;
      return;
    }
    char* buff = reinterpret_cast<char*>(request->_tempObject) + index;
    memcpy(buff, data, len);
    if (index + len == total) {
      char* buff =  reinterpret_cast<char*>(request->_tempObject) + total;
      *buff = '\0';
    }
  }
}

void HomieInternals::BootConfig::__SendJSONError(AsyncWebServerRequest * request, String msg, int16_t code) {
  Interface::get().getLogger() << msg << endl;
  DynamicJsonDocument jsonDoc(JSON_OBJECT_SIZE(2) + msg.length() + 1);
  jsonDoc["success"] = false;
  jsonDoc["error"] = msg;
  String errorJson;
  serializeJson(jsonDoc, errorJson);
  request->send(code, FPSTR(PROGMEM_CONFIG_APPLICATION_JSON), errorJson);
}
