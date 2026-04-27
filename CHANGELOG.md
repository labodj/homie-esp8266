# Changelog

All notable changes in this maintained fork are documented here.

The format follows the spirit of [Keep a Changelog](https://keepachangelog.com/)
and the version numbers follow Semantic Versioning for the maintained fork.
The implemented Homie convention version is still `3.0.1` unless explicitly
documented otherwise.

## [Unreleased]

### Added

- Automatic GitHub Release workflow for `v*` tags, including metadata validation,
  package validation, documentation build, OTA updater tests and PlatformIO package
  asset upload.
- Dedicated documentation build requirements in `docs/requirements.txt`.
- More robust OTA updater CLI options for TLS client certificates, TLS verification
  overrides, explicit MQTT client IDs and optional expected firmware MD5 checks.
- Homie v4/v5 compatibility outlook documentation.

### Changed

- GitHub Pages deployment now runs from the repository CI workflow instead of the
  legacy Pages builder.
- CI actions were updated to current Node 24-compatible official action versions.

## [3.2.0] - 2026-04-26

### Added

- Opt-in LittleFS support with `HOMIE_USE_LITTLEFS=1`.
- Temporary OTA migration mode from SPIFFS to LittleFS with
  `HOMIE_MIGRATE_SPIFFS_TO_LITTLEFS=1`.
- Internal MQTT ACK and inbound message queue size overrides.
- Retained Homie statistics for dropped MQTT ACK and inbound events:
  `$stats/mqttackdropped` and `$stats/mqttinbounddropped`.
- `PropertyInterface::setRetained()` for advertised property retention metadata.
- PioArduino quickstart documentation.
- Maintained fork differences documentation.
- OTA updater unit tests.
- GitHub Actions CI across ESP8266, ESP32, LittleFS, migration, queue-size and
  PioArduino build variants.

### Changed

- Deferred most MQTT callback processing into the main `Homie.loop()` flow.
- Hardened Wi-Fi and MQTT lifecycle reconciliation when async callbacks are missed.
- Hardened OTA publish handling around duplicate delivery and overlapping retries.
- Improved boot configuration filesystem handling.
- Expanded compiler flag, migration, troubleshooting and API documentation.

### Compatibility

- SPIFFS remains the default filesystem.
- LittleFS and SPIFFS-to-LittleFS migration are opt-in.
- The Homie MQTT convention version remains `3.0.1`.
- Existing configuration paths and the historical OTA MQTT contract are preserved.

## [3.1.2] - 2026-04-18

### Fixed

- PlatformIO package export paths.

## [3.1.1] - 2026-04-18

### Added

- MQTT ACK queue tuning override.

## [3.1.0] - 2026-04-14

### Changed

- Bumped the maintained fork package version to `3.1.0`.

[Unreleased]: https://github.com/labodj/homie-esp8266/compare/v3.2.0...develop
[3.2.0]: https://github.com/labodj/homie-esp8266/compare/v3.1.2...v3.2.0
[3.1.2]: https://github.com/labodj/homie-esp8266/compare/v3.1.1...v3.1.2
[3.1.1]: https://github.com/labodj/homie-esp8266/compare/v3.1.0...v3.1.1
[3.1.0]: https://github.com/labodj/homie-esp8266/releases/tag/v3.1.0
