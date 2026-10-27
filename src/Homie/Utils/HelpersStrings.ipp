std::unique_ptr<char[]> Helpers::cloneString(const char* string, size_t length) {
  std::unique_ptr<char[]> copy(new (std::nothrow) char[length + 1]);
  if (!copy) {
    Helpers::abort(F("✖ Cannot allocate string storage"));
    return nullptr;
  }
  memcpy(copy.get(), string, length);
  copy[length] = '\0';
  return copy;
}

std::unique_ptr<char[]> Helpers::cloneString(const String& string) {
  return cloneString(string.c_str(), string.length());
}

std::unique_ptr<char[]> Helpers::cloneString(const char* string) {
  return string ? cloneString(string, strlen(string)) : nullptr;
}

std::unique_ptr<char[]> Helpers::cloneStrings(const char* const* strings, const char** copies, size_t count) {
  size_t total = 0;
  for (size_t i = 0; i < count; ++i) {
    if (!strings[i]) continue;
    const size_t length = strlen(strings[i]);
    if (length >= SIZE_MAX - total) {
      Helpers::abort(F("✖ String storage too large"));
      return nullptr;
    }
    total += length + 1;
  }

  std::unique_ptr<char[]> storage;
  if (total) {
    storage.reset(new (std::nothrow) char[total]);
    if (!storage) {
      Helpers::abort(F("✖ Cannot allocate string storage"));
      return nullptr;
    }
  }

  char* next = storage.get();
  for (size_t i = 0; i < count; ++i) {
    if (!strings[i]) {
      copies[i] = nullptr;
      continue;
    }
    const size_t length = strlen(strings[i]) + 1;
    memcpy(next, strings[i], length);
    copies[i] = next;
    next += length;
  }
  return storage;
}
