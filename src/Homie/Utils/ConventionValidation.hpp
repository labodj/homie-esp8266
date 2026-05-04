#pragma once

#include <Arduino.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../Constants.hpp"

#if HOMIE_STRICT_PROPERTY_VALIDATION && HOMIE_CONVENTION_V5
#include <ArduinoJson.h>
#endif

namespace HomieInternals {
namespace ConventionValidation {

enum class Datatype {
  Integer,
  Float,
  Boolean,
  String,
  Enum,
  Color,
  DateTime,
  Duration,
  Json,
  Unknown
};

inline bool isValidTopicId(const char* value) {
  if (!value || value[0] == '\0') return false;

  const size_t length = strlen(value);
#if !HOMIE_CONVENTION_V5
  if (value[0] == '-' || value[length - 1] == '-') return false;
#endif

  for (size_t i = 0; i < length; i++) {
    const char c = value[i];
    const bool lower = c >= 'a' && c <= 'z';
    const bool digit = c >= '0' && c <= '9';
    if (!lower && !digit && c != '-') return false;
  }

  return true;
}

inline bool isValidRootTopicV3V4(const char* baseTopic) {
  if (!baseTopic || baseTopic[0] == '\0') return false;

  size_t end = strlen(baseTopic);
  if (baseTopic[end - 1] == '/') end--;
  if (end == 0) return false;

  for (size_t i = 0; i < end; i++) {
    if (baseTopic[i] == '/') return false;
  }

  char root[MAX_MQTT_BASE_TOPIC_LENGTH];
  if (end >= sizeof(root)) return false;
  memcpy(root, baseTopic, end);
  root[end] = '\0';
  return isValidTopicId(root);
}

inline bool isValidRootTopicV5(const char* baseTopic) {
  if (!baseTopic || baseTopic[0] == '\0') return false;

  size_t end = strlen(baseTopic);
  if (baseTopic[end - 1] == '/') end--;
  if (end == 0) return false;

  if (end >= 2 && baseTopic[end - 1] == '5' && baseTopic[end - 2] == '/') {
    end -= 2;
  }
  if (end == 0) return false;

  for (size_t i = 0; i < end; i++) {
    if (baseTopic[i] == '/') return false;
  }

  char domain[MAX_MQTT_BASE_TOPIC_LENGTH];
  if (end >= sizeof(domain)) return false;
  memcpy(domain, baseTopic, end);
  domain[end] = '\0';
  return isValidTopicId(domain);
}

inline bool isValidRootTopic(const char* baseTopic) {
#if HOMIE_CONVENTION_V5
  return isValidRootTopicV5(baseTopic);
#else
  return isValidRootTopicV3V4(baseTopic);
#endif
}

inline Datatype parseDatatype(const char* datatype) {
  if (!datatype || datatype[0] == '\0') return Datatype::Unknown;
  if (strcmp_P(datatype, PSTR("integer")) == 0) return Datatype::Integer;
  if (strcmp_P(datatype, PSTR("float")) == 0) return Datatype::Float;
  if (strcmp_P(datatype, PSTR("boolean")) == 0) return Datatype::Boolean;
  if (strcmp_P(datatype, PSTR("string")) == 0) return Datatype::String;
  if (strcmp_P(datatype, PSTR("enum")) == 0) return Datatype::Enum;
  if (strcmp_P(datatype, PSTR("color")) == 0) return Datatype::Color;
#if HOMIE_CONVENTION_V5
  if (strcmp_P(datatype, PSTR("datetime")) == 0) return Datatype::DateTime;
  if (strcmp_P(datatype, PSTR("duration")) == 0) return Datatype::Duration;
  if (strcmp_P(datatype, PSTR("json")) == 0) return Datatype::Json;
#endif
  return Datatype::Unknown;
}

inline const char* datatypeName(Datatype datatype) {
  switch (datatype) {
    case Datatype::Integer:
      return "integer";
    case Datatype::Float:
      return "float";
    case Datatype::Boolean:
      return "boolean";
    case Datatype::String:
      return "string";
    case Datatype::Enum:
      return "enum";
    case Datatype::Color:
      return "color";
    case Datatype::DateTime:
      return "datetime";
    case Datatype::Duration:
      return "duration";
    case Datatype::Json:
      return "json";
    case Datatype::Unknown:
    default:
      return HOMIE_DEFAULT_PROPERTY_DATATYPE;
  }
}

inline bool datatypeRequiresFormat(Datatype datatype) {
  return datatype == Datatype::Enum || datatype == Datatype::Color;
}

inline bool datatypeSupportsFormat(Datatype datatype) {
  switch (datatype) {
    case Datatype::Integer:
    case Datatype::Float:
    case Datatype::Enum:
    case Datatype::Color:
      return true;
#if HOMIE_CONVENTION_V5
    case Datatype::Boolean:
    case Datatype::Json:
      return true;
#endif
    default:
      return false;
  }
}

inline bool hasUtf8Bom(const char* value, size_t length) {
  return length >= 3
      && static_cast<uint8_t>(value[0]) == 0xef
      && static_cast<uint8_t>(value[1]) == 0xbb
      && static_cast<uint8_t>(value[2]) == 0xbf;
}

inline bool parseIntegerValue(const char* value, size_t length, long long& result) {
  if (!value || length == 0 || length > 20) return false;

  char buffer[21];
  memcpy(buffer, value, length);
  buffer[length] = '\0';

  char* end = nullptr;
  errno = 0;
  const long long parsed = strtoll(buffer, &end, 10);
  if (errno == ERANGE || !end || *end != '\0') return false;

  result = parsed;
  return true;
}

inline bool isIntegerLiteral(const char* value, size_t length) {
  if (!value || length == 0) return false;

  size_t index = 0;
  if (value[0] == '-') {
    if (length == 1) return false;
    index = 1;
  }

  for (; index < length; index++) {
    if (value[index] < '0' || value[index] > '9') return false;
  }

  long long ignored;
  return parseIntegerValue(value, length, ignored);
}

inline bool parseFloatValue(const char* value, size_t length, double& result) {
  if (!value || length == 0) return false;

  bool digit = false;
  for (size_t i = 0; i < length; i++) {
    const char c = value[i];
    const bool allowed = (c >= '0' && c <= '9')
                      || c == '-'
                      || c == '.'
                      || c == 'e'
                      || c == 'E';
    if (!allowed) return false;
    if (c >= '0' && c <= '9') digit = true;
  }
  if (!digit) return false;

  String copy;
  if (!copy.reserve(length + 1)) return false;
  for (size_t i = 0; i < length; i++) copy += value[i];

  char* end = nullptr;
  errno = 0;
  const double parsed = strtod(copy.c_str(), &end);
  if (errno == ERANGE || !end || *end != '\0' || !isfinite(parsed)) return false;

  result = parsed;
  return true;
}

inline bool isNumericFormat(Datatype datatype, const char* format) {
  if (!format || format[0] == '\0') return false;

  const char* firstColon = strchr(format, ':');
  if (!firstColon) return false;

  const char* secondColon = strchr(firstColon + 1, ':');
#if HOMIE_CONVENTION_V5
  if (secondColon && strchr(secondColon + 1, ':')) return false;
#else
  if (secondColon) return false;
  if (firstColon == format || firstColon[1] == '\0') return false;
#endif

  auto validPart = [datatype](const char* begin, const char* end) {
    const size_t length = static_cast<size_t>(end - begin);
    if (length == 0) return true;
    if (datatype == Datatype::Integer) return isIntegerLiteral(begin, length);
    double ignored;
    return parseFloatValue(begin, length, ignored);
  };

  if (!validPart(format, firstColon)) return false;
  if (!validPart(firstColon + 1, secondColon ? secondColon : format + strlen(format))) return false;

  const char* minBegin = format;
  const char* minEnd = firstColon;
  const char* maxBegin = firstColon + 1;
  const char* maxEnd = secondColon ? secondColon : format + strlen(format);
  if (datatype == Datatype::Integer) {
    long long minValue = 0;
    long long maxValue = 0;
    const bool hasMin = minEnd > minBegin
                     && parseIntegerValue(minBegin, static_cast<size_t>(minEnd - minBegin), minValue);
    const bool hasMax = maxEnd > maxBegin
                     && parseIntegerValue(maxBegin, static_cast<size_t>(maxEnd - maxBegin), maxValue);
    if (hasMin && hasMax && minValue > maxValue) return false;
  } else {
    double minValue = 0;
    double maxValue = 0;
    const bool hasMin = minEnd > minBegin
                     && parseFloatValue(minBegin, static_cast<size_t>(minEnd - minBegin), minValue);
    const bool hasMax = maxEnd > maxBegin
                     && parseFloatValue(maxBegin, static_cast<size_t>(maxEnd - maxBegin), maxValue);
    if (hasMin && hasMax && minValue > maxValue) return false;
  }

  if (secondColon) {
    const char* stepBegin = secondColon + 1;
    const size_t stepLength = strlen(stepBegin);
    if (stepLength == 0) return false;
    if (datatype == Datatype::Integer) {
      long long step = 0;
      return parseIntegerValue(stepBegin, stepLength, step) && step > 0;
    }

    double step = 0;
    return parseFloatValue(stepBegin, stepLength, step) && step > 0;
  }

  return true;
}

#if HOMIE_CONVENTION_V5
inline bool enumTokenSeenBefore(const char* format, const char* tokenBegin, size_t tokenLength) {
  const char* cursor = format;
  while (cursor < tokenBegin) {
    const char* comma = strchr(cursor, ',');
    const char* tokenEnd = comma ? comma : tokenBegin;
    const size_t currentLength = static_cast<size_t>(tokenEnd - cursor);
    if (currentLength == tokenLength && strncmp(cursor, tokenBegin, tokenLength) == 0) return true;
    if (!comma) break;
    cursor = comma + 1;
  }
  return false;
}
#endif

inline bool isEnumFormat(const char* format) {
  if (!format || format[0] == '\0') return false;

#if HOMIE_CONVENTION_V5 || HOMIE_CONVENTION_V4
  const char* tokenBegin = format;
  for (const char* cursor = format;; cursor++) {
    if (*cursor == ',' || *cursor == '\0') {
      const size_t tokenLength = static_cast<size_t>(cursor - tokenBegin);
      if (tokenLength == 0) return false;
#if HOMIE_CONVENTION_V5
      if (enumTokenSeenBefore(format, tokenBegin, tokenLength)) return false;
#endif
      if (*cursor == '\0') break;
      tokenBegin = cursor + 1;
    }
  }
  return true;
#else
  size_t currentLength = 0;
  for (size_t i = 0; format[i] != '\0'; i++) {
    if (format[i] == ',') {
      if (format[i + 1] == ',') {
        currentLength++;
        i++;
        continue;
      }
      if (currentLength == 0) return false;
      currentLength = 0;
    } else {
      currentLength++;
    }
  }

  return currentLength > 0;
#endif
}

inline bool enumPayloadMatchesFormat(const char* payload, size_t payloadLength, const char* format) {
  if (!isEnumFormat(format)) return false;

  bool tokenMatches = true;
  size_t payloadIndex = 0;
  for (size_t i = 0;; i++) {
    const char c = format[i];
#if !HOMIE_CONVENTION_V5 && !HOMIE_CONVENTION_V4
    if (c == ',' && format[i + 1] == ',') {
      // Homie 3 encodes a literal comma inside an enum token as ",,".
      // Keep scanning the current token after a mismatch so later enum values
      // can still be tested instead of failing on the first escaped comma.
      if (tokenMatches) {
        if (payloadIndex >= payloadLength || payload[payloadIndex] != ',') {
          tokenMatches = false;
        } else {
          payloadIndex++;
        }
      }
      i++;
      continue;
    }
#endif
    if (c == ',' || c == '\0') {
      if (tokenMatches && payloadIndex == payloadLength) return true;
      payloadIndex = 0;
      tokenMatches = true;
      if (c == '\0') break;
    } else {
      if (tokenMatches) {
        if (payloadIndex >= payloadLength || payload[payloadIndex] != c) {
          tokenMatches = false;
        } else {
          payloadIndex++;
        }
      }
    }
  }

  return false;
}

inline bool colorFormatContains(const char* format, const char* token) {
  if (!format || !token) return false;

  const size_t tokenLength = strlen(token);
  const char* cursor = format;
  while (*cursor) {
    const char* comma = strchr(cursor, ',');
    const size_t length = comma ? static_cast<size_t>(comma - cursor) : strlen(cursor);
    if (length == tokenLength && strncmp(cursor, token, tokenLength) == 0) return true;
    if (!comma) break;
    cursor = comma + 1;
  }

  return false;
}

inline bool isColorFormat(const char* format) {
  if (!format || format[0] == '\0') return false;

#if HOMIE_CONVENTION_V5
  bool seenRgb = false;
  bool seenHsv = false;
  bool seenXyz = false;
  const char* cursor = format;
  while (*cursor) {
    const char* comma = strchr(cursor, ',');
    const size_t length = comma ? static_cast<size_t>(comma - cursor) : strlen(cursor);
    if (length == 3 && strncmp(cursor, "rgb", 3) == 0) {
      if (seenRgb) return false;
      seenRgb = true;
    } else if (length == 3 && strncmp(cursor, "hsv", 3) == 0) {
      if (seenHsv) return false;
      seenHsv = true;
    } else if (length == 3 && strncmp(cursor, "xyz", 3) == 0) {
      if (seenXyz) return false;
      seenXyz = true;
    } else {
      return false;
    }
    if (!comma) break;
    cursor = comma + 1;
  }
  return seenRgb || seenHsv || seenXyz;
#else
  return strcmp_P(format, PSTR("rgb")) == 0 || strcmp_P(format, PSTR("hsv")) == 0;
#endif
}

inline bool isBooleanFormat(const char* format) {
  if (!format || format[0] == '\0') return false;
  const char* comma = strchr(format, ',');
  return comma
      && comma != format
      && comma[1] != '\0'
      && strchr(comma + 1, ',') == nullptr;
}

#if HOMIE_CONVENTION_V5
inline bool jsonRootMatches(const char* payload, size_t length, bool allowArray) {
  if (!payload || length == 0) return false;

  size_t begin = 0;
  while (begin < length && isspace(static_cast<unsigned char>(payload[begin]))) begin++;
  if (begin == length) return false;

  size_t end = length - 1;
  while (end > begin && isspace(static_cast<unsigned char>(payload[end]))) end--;

  const bool objectShape = payload[begin] == '{' && payload[end] == '}';
  const bool arrayShape = allowArray && payload[begin] == '[' && payload[end] == ']';
  if (!objectShape && !arrayShape) return false;

#if HOMIE_STRICT_PROPERTY_VALIDATION
  // Capacity is tied to payload size so strict validation does not reject valid
  // larger JSON payloads just because the parser document was undersized.
  const size_t maxSize = static_cast<size_t>(-1);
  if (length > (maxSize - 64) / 2) return false;
  DynamicJsonDocument document(length * 2 + 64);
  const DeserializationError error = deserializeJson(document, payload, length);
  if (error) return false;
  return document.is<JsonObject>() || (allowArray && document.is<JsonArray>());
#else
  return true;
#endif
}
#endif

inline bool isJsonFormat(const char* format) {
  if (!format || format[0] == '\0') return false;
#if HOMIE_CONVENTION_V5 && HOMIE_STRICT_PROPERTY_VALIDATION
  // Homie v5 json formats are JSON Schema documents encoded as strings.
  return jsonRootMatches(format, strlen(format), false);
#else
  return true;
#endif
}

inline bool isValidFormat(Datatype datatype, const char* format) {
  if (!format || format[0] == '\0') return false;

#if HOMIE_STRICT_PROPERTY_VALIDATION
  switch (datatype) {
    case Datatype::Integer:
    case Datatype::Float:
      return isNumericFormat(datatype, format);
    case Datatype::Enum:
      return isEnumFormat(format);
    case Datatype::Color:
      return isColorFormat(format);
#if HOMIE_CONVENTION_V5
    case Datatype::Boolean:
      return isBooleanFormat(format);
    case Datatype::Json:
      return isJsonFormat(format);
#endif
    default:
      return false;
  }
#else
  // The lightweight profile only checks whether this datatype may carry a
  // format. Full grammar/range validation is strict-mode only to keep ESP8266
  // flash use predictable.
  return datatypeSupportsFormat(datatype);
#endif
}

inline Datatype advertisedDatatype(const char* datatype, const char* format) {
  const Datatype parsed = parseDatatype(datatype);
  if (parsed == Datatype::Unknown) return Datatype::String;
  if (datatypeRequiresFormat(parsed) && !isValidFormat(parsed, format)) return Datatype::String;
  return parsed;
}

inline const char* advertisedDatatypeName(const char* datatype, const char* format) {
  return datatypeName(advertisedDatatype(datatype, format));
}

inline bool shouldAdvertiseDatatype(const char* datatype) {
#if HOMIE_CONVENTION_V4 || HOMIE_CONVENTION_V5
  (void)datatype;
  return true;
#else
  return datatype && datatype[0] != '\0';
#endif
}

inline bool shouldAdvertiseFormat(const char* advertisedDatatypeNameValue, const char* format) {
  const Datatype datatype = parseDatatype(advertisedDatatypeNameValue);
  return datatypeSupportsFormat(datatype) && isValidFormat(datatype, format);
}

inline bool numericPayloadMatchesFormat(Datatype datatype, const char* payload, size_t payloadLength, const char* format) {
  if (!format || format[0] == '\0' || !isNumericFormat(datatype, format)) return true;

  const char* firstColon = strchr(format, ':');
  const char* secondColon = strchr(firstColon + 1, ':');
  const char* minBegin = format;
  const char* minEnd = firstColon;
  const char* maxBegin = firstColon + 1;
  const char* maxEnd = secondColon ? secondColon : format + strlen(format);

  if (datatype == Datatype::Integer) {
    long long value = 0;
    if (!parseIntegerValue(payload, payloadLength, value)) return false;

    long long minValue = 0;
    if (minEnd > minBegin && parseIntegerValue(minBegin, static_cast<size_t>(minEnd - minBegin), minValue) && value < minValue) return false;

    long long maxValue = 0;
    if (maxEnd > maxBegin && parseIntegerValue(maxBegin, static_cast<size_t>(maxEnd - maxBegin), maxValue) && value > maxValue) return false;
#if HOMIE_CONVENTION_V5
    if (secondColon) {
      long long step = 0;
      if (!parseIntegerValue(secondColon + 1, strlen(secondColon + 1), step) || step <= 0) return false;

      long long base = 0;
      if (minEnd > minBegin) {
        if (!parseIntegerValue(minBegin, static_cast<size_t>(minEnd - minBegin), base)) return false;
      } else if (maxEnd > maxBegin) {
        if (!parseIntegerValue(maxBegin, static_cast<size_t>(maxEnd - maxBegin), base)) return false;
      } else {
        base = 0;
      }

      if ((value - base) % step != 0) return false;
    }
#endif
    return true;
  }

  double value = 0;
  if (!parseFloatValue(payload, payloadLength, value)) return false;

  double minValue = 0;
  if (minEnd > minBegin && parseFloatValue(minBegin, static_cast<size_t>(minEnd - minBegin), minValue) && value < minValue) return false;

  double maxValue = 0;
  if (maxEnd > maxBegin && parseFloatValue(maxBegin, static_cast<size_t>(maxEnd - maxBegin), maxValue) && value > maxValue) return false;
#if HOMIE_CONVENTION_V5
  if (secondColon) {
    double step = 0;
    if (!parseFloatValue(secondColon + 1, strlen(secondColon + 1), step) || step <= 0) return false;

    double base = 0;
    if (minEnd > minBegin) {
      if (!parseFloatValue(minBegin, static_cast<size_t>(minEnd - minBegin), base)) return false;
    } else if (maxEnd > maxBegin) {
      if (!parseFloatValue(maxBegin, static_cast<size_t>(maxEnd - maxBegin), base)) return false;
    } else {
      base = 0;
    }

    const double ratio = (value - base) / step;
    const long long rounded = static_cast<long long>(ratio >= 0 ? ratio + 0.5 : ratio - 0.5);
    double delta = ratio - static_cast<double>(rounded);
    if (delta < 0) delta = -delta;
    if (delta > 0.000001) return false;
  }
#endif
  return true;
}

inline bool parseUnsignedPart(const char* value, size_t length, double& result) {
  if (!parseFloatValue(value, length, result)) return false;
  return result >= 0;
}

inline bool colorPayloadValidV3V4(const char* payload, size_t payloadLength, const char* format) {
  if (!isColorFormat(format)) return false;

  uint8_t values = 0;
  const char* begin = payload;
  const char* end = payload + payloadLength;
  while (begin <= end) {
    const char* comma = static_cast<const char*>(memchr(begin, ',', static_cast<size_t>(end - begin)));
    const char* partEnd = comma ? comma : end;
    long long value = 0;
    if (!parseIntegerValue(begin, static_cast<size_t>(partEnd - begin), value)) return false;
    if (strcmp_P(format, PSTR("rgb")) == 0) {
      if (value < 0 || value > 255) return false;
    } else {
      if ((values == 0 && (value < 0 || value > 360)) || (values > 0 && (value < 0 || value > 100))) return false;
    }
    values++;
    if (!comma) break;
    begin = comma + 1;
  }

  return values == 3;
}

inline bool colorPayloadValidV5(const char* payload, size_t payloadLength, const char* format) {
  if (!isColorFormat(format)) return false;

  const char* firstComma = static_cast<const char*>(memchr(payload, ',', payloadLength));
  if (!firstComma) return false;

  const size_t typeLength = static_cast<size_t>(firstComma - payload);
  char type[4] = {0};
  if (typeLength != 3) return false;
  memcpy(type, payload, 3);
  if (!colorFormatContains(format, type)) return false;

  const uint8_t expectedValues = strcmp_P(type, PSTR("xyz")) == 0 ? 2 : 3;
  uint8_t values = 0;
  const char* begin = firstComma + 1;
  const char* end = payload + payloadLength;
  while (begin <= end) {
    const char* comma = static_cast<const char*>(memchr(begin, ',', static_cast<size_t>(end - begin)));
    const char* partEnd = comma ? comma : end;
    double value = 0;
    if (!parseUnsignedPart(begin, static_cast<size_t>(partEnd - begin), value)) return false;

    if (strcmp_P(type, PSTR("rgb")) == 0) {
      if (value < 0 || value > 255) return false;
    } else if (strcmp_P(type, PSTR("hsv")) == 0) {
      if ((values == 0 && (value < 0 || value > 360)) || (values > 0 && (value < 0 || value > 100))) return false;
    } else if (value < 0 || value > 1) {
      return false;
    }

    values++;
    if (!comma) break;
    begin = comma + 1;
  }

  return values == expectedValues;
}

inline bool parseTwoDigits(const char* payload, size_t offset, uint8_t& value) {
  if (payload[offset] < '0' || payload[offset] > '9'
      || payload[offset + 1] < '0' || payload[offset + 1] > '9') {
    return false;
  }

  value = static_cast<uint8_t>((payload[offset] - '0') * 10 + (payload[offset + 1] - '0'));
  return true;
}

inline bool parseFourDigits(const char* payload, uint16_t& value) {
  value = 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (payload[i] < '0' || payload[i] > '9') return false;
    value = static_cast<uint16_t>(value * 10 + payload[i] - '0');
  }
  return true;
}

inline bool isLeapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

inline bool validDate(uint16_t year, uint8_t month, uint8_t day) {
  if (month < 1 || month > 12 || day < 1) return false;

  static const uint8_t daysByMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t maxDay = daysByMonth[month - 1];
  if (month == 2 && isLeapYear(year)) maxDay = 29;
  return day <= maxDay;
}

inline bool isDateTimePayload(const char* payload, size_t length) {
  if (!payload || length < 19) return false;
  if (payload[4] != '-' || payload[7] != '-' || (payload[10] != 'T' && payload[10] != 't')
      || payload[13] != ':' || payload[16] != ':') {
    return false;
  }

  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  if (!parseFourDigits(payload, year)
      || !parseTwoDigits(payload, 5, month)
      || !parseTwoDigits(payload, 8, day)
      || !parseTwoDigits(payload, 11, hour)
      || !parseTwoDigits(payload, 14, minute)
      || !parseTwoDigits(payload, 17, second)) {
    return false;
  }
  if (!validDate(year, month, day) || hour > 23 || minute > 59 || second > 60) return false;

  size_t cursor = 19;
  if (cursor < length && payload[cursor] == '.') {
    cursor++;
    const size_t fractionBegin = cursor;
    while (cursor < length && payload[cursor] >= '0' && payload[cursor] <= '9') cursor++;
    if (cursor == fractionBegin) return false;
  }

  if (cursor == length) return true;
  if ((payload[cursor] == 'Z' || payload[cursor] == 'z') && cursor + 1 == length) return true;
  if ((payload[cursor] == '+' || payload[cursor] == '-') && cursor + 6 == length && payload[cursor + 3] == ':') {
    uint8_t offsetHour = 0;
    uint8_t offsetMinute = 0;
    return parseTwoDigits(payload, cursor + 1, offsetHour)
        && parseTwoDigits(payload, cursor + 4, offsetMinute)
        && offsetHour <= 23
        && offsetMinute <= 59;
  }
  return false;
}

inline bool isDurationPayload(const char* payload, size_t length) {
  if (!payload || length < 3 || payload[0] != 'P' || payload[1] != 'T') return false;

  bool digit = false;
  bool designator = false;
  char lastDesignator = '\0';
  for (size_t i = 2; i < length; i++) {
    const char c = payload[i];
    if (c >= '0' && c <= '9') {
      digit = true;
    } else if ((c == 'H' || c == 'M' || c == 'S') && digit) {
      if ((c == 'H' && lastDesignator != '\0')
          || (c == 'M' && (lastDesignator == 'M' || lastDesignator == 'S'))
          || (c == 'S' && lastDesignator == 'S')) {
        return false;
      }
      lastDesignator = c;
      designator = true;
      digit = false;
    } else {
      return false;
    }
  }

  return designator && !digit;
}

inline bool isJsonPayload(const char* payload, size_t length) {
#if HOMIE_CONVENTION_V5
  return jsonRootMatches(payload, length, true);
#else
  return false;
#endif
}

inline bool payloadMatches(const char* datatype, const char* format, const char* payload, size_t payloadLength, bool mqttEncodedPayload = false) {
  if (!payload) return false;
#if HOMIE_CONVENTION_V5
  if (hasUtf8Bom(payload, payloadLength)) return false;
#endif

  const Datatype effectiveDatatype = advertisedDatatype(datatype, format);
  if (effectiveDatatype == Datatype::String) {
#if HOMIE_CONVENTION_V5
    if (mqttEncodedPayload && payloadLength == 0) return false;
    if (mqttEncodedPayload && payloadLength == 1 && payload[0] == '\0') return true;
    if (memchr(payload, '\0', payloadLength)) return false;
#endif
    return true;
  }

  if (payloadLength == 0) return false;

  switch (effectiveDatatype) {
    case Datatype::Integer:
      return isIntegerLiteral(payload, payloadLength)
          && numericPayloadMatchesFormat(effectiveDatatype, payload, payloadLength, format);
    case Datatype::Float:
    {
      double ignored = 0;
      return parseFloatValue(payload, payloadLength, ignored)
          && numericPayloadMatchesFormat(effectiveDatatype, payload, payloadLength, format);
    }
    case Datatype::Boolean:
      return (payloadLength == 4 && memcmp(payload, "true", 4) == 0)
          || (payloadLength == 5 && memcmp(payload, "false", 5) == 0);
    case Datatype::Enum:
      return enumPayloadMatchesFormat(payload, payloadLength, format);
    case Datatype::Color:
#if HOMIE_CONVENTION_V5
      return colorPayloadValidV5(payload, payloadLength, format);
#else
      return colorPayloadValidV3V4(payload, payloadLength, format);
#endif
    case Datatype::DateTime:
      return isDateTimePayload(payload, payloadLength);
    case Datatype::Duration:
      return isDurationPayload(payload, payloadLength);
    case Datatype::Json:
      return isJsonPayload(payload, payloadLength);
    case Datatype::String:
    case Datatype::Unknown:
    default:
      return true;
  }
}

}  // namespace ConventionValidation
}  // namespace HomieInternals
