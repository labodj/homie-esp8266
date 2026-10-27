void BootConfig::_loopWifiScan() {
  if (!_lastWifiScanEnded) {
    const auto scanResult = WiFi.scanComplete();

    if (scanResult == WIFI_SCAN_RUNNING) return;
    if (scanResult < 0) {
      Interface::get().getLogger() << F("✖ Wi-Fi scan failed") << endl;
      _ssidCount = 0;
      _wifiScanTimer.reset();
    } else {
      Interface::get().getLogger() << F("✔ Wi-Fi scan completed") << endl;
      _ssidCount = scanResult > UINT8_MAX ? UINT8_MAX : static_cast<uint8_t>(scanResult);
      _generateNetworksJson();
      _wifiScanAvailable = true;
    }

    _lastWifiScanEnded = true;
  }

  if (_lastWifiScanEnded && _wifiScanTimer.check()) {
    Interface::get().getLogger() << F("Triggering Wi-Fi scan...") << endl;
    WiFi.scanNetworks(true);
    _wifiScanTimer.tick();
    _lastWifiScanEnded = false;
  }
}
