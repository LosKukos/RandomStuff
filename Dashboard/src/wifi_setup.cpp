#include "wifi_setup.h"
#include <WiFi.h>
#include "config.h"
#include "state.h"
#include "log_util.h"

void scanWifi() {
  logLine(F("[SCAN] scanning..."));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(300);
  int n = WiFi.scanNetworks();
  logValue(F("[SCAN] found: "), n);
  for (int i = 0; i < n; i++) {
    logLine("[SCAN] " + WiFi.SSID(i) + " RSSI:" + String(WiFi.RSSI(i)));
  }
  WiFi.scanDelete();
}

bool connectWiFi() {
  logLine(F("[WIFI] connecting"));
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.persistent(false);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  wl_status_t lastStatus = WL_IDLE_STATUS;

  while (millis() - start < WIFI_TIMEOUT_MS) {
    wl_status_t s = WiFi.status();
    if (s != lastStatus) { logValue(F("[WIFI] status: "), (int)s); lastStatus = s; }
    if (s == WL_CONNECTED) {
      logLine(F("[WIFI] connected"));
      logValue(F("[WIFI] IP: "), WiFi.localIP().toString());
      setNetReady(true);
      return true;
    }
    delay(250);
  }

  logLine(F("[WIFI] timeout"));
  setNetReady(false);
  setFatalError(true);
  return false;
}
