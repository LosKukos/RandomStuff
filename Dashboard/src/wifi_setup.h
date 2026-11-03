#pragma once

// vypise nalezene site do logu (diagnostika pred connectem)
void scanWifi();

// pripoji se k WiFi (blokujici, max WIFI_TIMEOUT_MS).
// pri uspechu setNetReady(true), pri neuspechu setNetReady(false)+setFatalError(true)
bool connectWiFi();
