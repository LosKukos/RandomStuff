#include "wifi_task.h"
#include "app_state.h"
#include "utils.h"
#include "time_service.h"
#include <WiFi.h>
#include <ESPmDNS.h>

static const char* MDNS_HOSTNAME = "mc";

void wifiTask(void* pvParameters) {
  (void)pvParameters;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  addLog("[WiFi] AP started");
  addLog("[WiFi] AP IP: " + WiFi.softAPIP().toString());

  bool connectInProgress = false;
  bool mdnsStarted = false;

  for (;;) {
    wl_status_t st = WiFi.status();
    bool connectedNow = (st == WL_CONNECTED);

    if (connectedNow && !staConnected) {
      staConnected = true;
      connectInProgress = false;
      addLog("[WiFi] STA connected: " + WiFi.localIP().toString());

      if (!mdnsStarted) {
        if (MDNS.begin(MDNS_HOSTNAME)) {
          MDNS.addService("http", "tcp", 80);
          mdnsStarted = true;
          addLog(String("[mDNS] started: http://") + MDNS_HOSTNAME + ".local");
        } else {
          addLog("[mDNS] start failed");
        }
      }

      syncTimeService();

    } else if (!connectedNow && staConnected) {
      staConnected = false;
      addLog("[WiFi] STA disconnected");

      if (mdnsStarted) {
        MDNS.end();
        mdnsStarted = false;
        addLog("[mDNS] stopped");
      }
    }

    if (!staSsid.isEmpty() && !connectedNow && !connectInProgress) {
      WiFi.begin(staSsid.c_str(), staPass.c_str());
      connectInProgress = true;
      addLog("[WiFi] connecting to STA...");
    }

    if (!connectedNow && connectInProgress) {
      connectInProgress = false;
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
