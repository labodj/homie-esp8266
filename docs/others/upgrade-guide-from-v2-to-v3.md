# Upgrade guide from v2 to v3

Use this guide when upgrading Homie devices from v2 to v3.

## New convention

The Homie convention was revised in v3 to be more extensible and
introspectable. Review the
[Homie v3 convention](https://github.com/homieiot/convention/tree/v3.0.1)
before deploying upgraded devices.

## API changes in the sketch

1. The `HomieNode` constructor needs the third mandatory parameter
   `const char* type`. For example,
   `HomieNode lightNode("light", "Light");` becomes
   `HomieNode lightNode("light", "Light", "switch");`.
2. The signature of `handleInput` changed to:
   `handleInput(const HomieRange& range, const String& property, const String& value)`
   See the Ping example for the current callback shape.
3. Review every advertised property id against the Homie 3.0.1 id rules. This
   fork keeps legacy ids for compatibility but logs a warning when an id is not
   convention-compliant.
4. If you rely on `overwriteSetter(true)`, use `setSetRetained()` when the
   mirrored `/set` topic needs a different retained flag from the main property
   publish.
