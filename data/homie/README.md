`data/homie` folder
===================

This folder contains optional files for the selected SPIFFS or LittleFS filesystem.
Blank devices already serve a setup page from firmware: connect to their hotspot
and open `http://192.168.123.1`. No data upload is needed for onboarding.

To upload files to the filesystem of your device, first create a folder named
`data` in your sketch directory. In this `data` folder, create a `homie`
directory. You can put two files in it:

1. The `config.json` file, if you want to bypass the `configuration` mode.
2. A custom `ui_bundle.gz` file. If present, it overrides the built-in setup page
   in configuration mode. Remove it to restore the built-in page.

Finally initiate the
[filesystem upload process][pio-upload] via PlatformIO, or via the
[Arduino IDE][arduino-upload].

[pio-upload]: https://docs.platformio.org/en/stable/platforms/espressif8266.html#uploading-files-to-file-system
[arduino-upload]: https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html
