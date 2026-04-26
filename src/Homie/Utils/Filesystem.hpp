#pragma once

#ifndef HOMIE_USE_LITTLEFS
#define HOMIE_USE_LITTLEFS 0
#endif

#ifndef HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS
#define HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS 0
#endif

// Filesystem selection is compile-time only so normal firmware images link just
// one backend. The migration flag is intentionally separate: production
// LittleFS builds should not keep the SPIFFS reader once a fleet has migrated.
#if HOMIE_USE_LITTLEFS
#include <LittleFS.h>
#if HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS
#ifdef ESP32
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif
#endif
#define HOMIE_FS LittleFS
#define HOMIE_FS_NAME "LittleFS"
#else
#ifdef ESP32
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif
#define HOMIE_FS SPIFFS
#define HOMIE_FS_NAME "SPIFFS"
#endif
