# Homie v4/v5 compatibility

This maintained fork advertises the Homie `3.0.1` convention by default and can
advertise Homie `4.0.0` or Homie `5.0` when explicitly built for it. The fork
version in `library.json`, `library.properties` and `HOMIE_ESP8266_VERSION` is
the package version of this maintained codebase; it does not change the
advertised Homie convention version.

## Homie v4

Homie `4.0.0` compatibility is available as an explicit build-time mode. It is
not the default because changing the advertised convention version changes the
MQTT discovery contract seen by controllers.

Enable it with:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=4
```

The current code already publishes most of the same structural information that
Homie v4 expects:

- device attributes such as `$homie`, `$name`, `$state`, `$nodes` and
  `$implementation`
- node attributes such as `$name`, `$type` and `$properties`
- property attributes such as `$name`, `$datatype`, `$format`, `$settable`,
  `$retained` and `$unit`
- `/set` command topics for settable properties

When v4 mode is enabled the library:

- advertises `$homie` as `4.0.0`
- publishes the mandatory `$extensions` attribute
- declares the official legacy firmware extension:
  `org.homie.legacy-firmware:0.1.1:[4.x]`
- declares the official legacy stats extension:
  `org.homie.legacy-stats:0.1.1:[4.x]`
- keeps the existing `$fw`, `$mac`, `$localip` and `$stats` topics so existing
  Homie 3.0.1 consumers and OTA tooling remain usable
- publishes required property `$name` and `$datatype` metadata even for older
  sketches that omitted them
- rejects retained `/set` commands, because Homie controllers must send command
  messages as non-retained publishes

The property metadata compatibility fallback is deliberately conservative:

- missing property `$name` uses the property id
- missing property `$datatype` uses `string`
- invalid Homie v4 datatypes are advertised as `string`
- `enum` and `color` properties without the required `$format` are advertised
  as `string`

Production v4 sketches should still set explicit names, datatypes and formats.
The fallback exists only so enabling v4 mode does not make older sketches
disappear from strict v4 discovery consumers.

Homie topic IDs allow lowercase letters, digits and hyphens. The library treats
invalid MQTT roots, device IDs, firmware names, node IDs and property IDs as boot
errors in every convention mode. Firmware names are validated because Homie 3.0.1
and the Homie v4 legacy firmware extension apply the device-ID character rules
to `$fw/name`. Range nodes are also rejected in v4 mode because Homie v4 has no
core range-node metadata and the historical `$array` model is a Homie 3
compatibility feature.

Because Homie v4 still uses retained MQTT attributes and per-topic discovery,
the implementation does not rewrite the library architecture.

## Homie v5

Homie `5.0` compatibility is also available as an explicit build-time mode:

```ini
build_flags =
  -D HOMIE_CONVENTION_VERSION=5
```

The default remains Homie `3.0.1` because v5 changes the discovery contract and
the root topic shape. In v5 mode this fork follows the Homie v5 core model:

- the root topic is `<domain>/5/`, so the default device base becomes
  `homie/5/<device-id>`
- the device publishes retained `$state` values under the v5 base topic
- the device publishes a retained `$description` JSON document with
  `homie: "5.0"`, a numeric description `version`, `nodes`, node metadata and
  property metadata
- property commands use `homie/5/<device-id>/<node-id>/<property-id>/set`
- retained `/set` commands are ignored in v5 mode because controllers must send
  command messages as non-retained publishes
- non-retained property publishes use QoS 0, as required by the v5
  retained-message rules
- string property publishes with an empty value use the Homie v5 one-byte NUL
  representation instead of an MQTT zero-length retained payload
- broadcast subscriptions move to the v5 root and accept nested broadcast levels

Homie v5 topic IDs allow lowercase letters, digits and hyphens. Range nodes
therefore change their concrete MQTT topic IDs from the legacy `node_<index>`
shape to `node-<index>`.

The existing OTA, configuration, firmware and runtime statistics topics are not
part of Homie v5 core. This fork keeps them as an explicitly declared extension:

```text
io.github.labodj.esp-runtime
```

See [Homie v5 fork runtime extension](homie-v5-runtime-extension.md) for the
extension topic contract.

Production v5 sketches should set explicit property datatypes. If an older
sketch omits a datatype, uses a datatype that is not valid in Homie v5, or uses
`enum`/`color` without the format required by v5, the `$description` generator
advertises that property as `string` so the published description remains
spec-valid. Exact format and payload correctness remains the sketch's
responsibility by default. Enable `HOMIE_STRICT_PROPERTY_VALIDATION=1` when the
library should also reject invalid property formats and payloads at runtime.

References:

- [Homie convention v3.0.1](https://github.com/homieiot/convention/blob/v3.0.1/convention.md)
- [Homie convention v4.0.0](https://github.com/homieiot/convention/blob/v4.0.0/convention.md)
- [Homie convention v5.0.0](https://github.com/homieiot/convention/blob/v5.0.0/convention.md)
