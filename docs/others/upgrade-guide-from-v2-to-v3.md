This is an upgrade guide to upgrade your Homie devices from v2 to v3.

## New convention

The Homie convention has been revised to v3 to be more extensible and introspectable. Be sure to [check it out](https://github.com/homieiot/convention/tree/v3.0.1).

## API changes in the sketch

1. Constructor of `HomieNode` needs third mandatory param `const char* type`:
   E.g. `HomieNode lightNode("light", "Light");` -> `HomieNode lightNode("light", "Light", "switch");`.
2. Signature of handleInput has changed to: `handleInput(const HomieRange& range, const String& property, const String& value)`
   See the Ping example for the current callback shape.
3. Review every advertised property id against the Homie 3.0.1 id rules. This
   fork keeps legacy ids for compatibility but logs a warning when an id is not
   convention-compliant.
4. If you rely on `overwriteSetter(true)`, use `setSetRetained()` when the
   mirrored `/set` topic needs a different retained flag from the main property
   publish.
