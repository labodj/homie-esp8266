# Homie v4/v5 compatibility outlook

This maintained fork currently implements the Homie `3.0.1` convention. The
fork version in `library.json`, `library.properties` and
`HOMIE_ESP8266_VERSION` is the package version of this maintained codebase; it
does not change the advertised Homie convention version.

## Homie v4

Homie `4.0.0` is a realistic compatibility target, but it should be implemented
as an explicit compatibility mode rather than by changing the existing default
advertisement behavior.

The current code already publishes most of the same structural information that
Homie v4 expects:

* device attributes such as `$homie`, `$name`, `$state`, `$nodes` and
  `$implementation`
* node attributes such as `$name`, `$type` and `$properties`
* property attributes such as `$name`, `$datatype`, `$format`, `$settable`,
  `$retained` and `$unit`
* `/set` command topics for settable properties

The main work for v4 would be:

* advertising `$homie` as `4.0.0` only when the device is intentionally running
  in v4 mode
* publishing a correct `$extensions` attribute
* mapping or advertising the legacy firmware and stats attributes through the
  official legacy extensions
* auditing topic ID validation and lifecycle semantics against the v4 wording
* testing controllers that consume Homie v4 discovery data

Because Homie v4 still uses retained MQTT attributes and per-topic discovery,
this can likely be added without rewriting the library architecture.

## Homie v5

Homie `5.0.0` is a larger architectural change. The v5 convention introduces a
versioned base topic shape such as `homie/5/<device-id>` and consolidates device
metadata into a retained `$description` JSON document.

That means v5 support would need more than a version string change:

* a new topic-prefix strategy for `homie/5/<device-id>`
* a JSON description generator for device, node and property metadata
* careful RAM sizing on ESP8266 when building the `$description` document
* updated subscription paths for settable property commands
* compatibility decisions for the existing Homie 3.x OTA/configuration
  implementation topics
* controller interoperability testing

The safest implementation path is to keep Homie `3.0.1` as the default and add a
separate opt-in convention mode. Homie v4 can probably be delivered as a
moderate compatibility layer. Homie v5 should be treated as a larger feature or
major-version branch because the discovery model and base topic shape differ
substantially from the current runtime.

References:

* [Homie convention v4.0.0](https://github.com/homieiot/convention/blob/v4.0.0/convention.md)
* [Homie convention v5.0.0](https://github.com/homieiot/convention/blob/v5.0.0/convention.md)
