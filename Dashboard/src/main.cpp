#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "config.h"
#include "state.h"
#include "log_util.h"
#include "led.h"
#include "wifi_setup.h"
#include "devices.h"
#include "time_svatky.h"
#include "diagnostics.h"
#include "web_server.h"
#include "tasks.h"
#include "breadcrumbs.h"

unsigned long lastHeartbeat = 0;

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  initMutexes();

  // RTC breadcrumbs: nacte zaznamy z predchoziho behu a vynuluje RTC pro tento beh.
  // Pocitadlo startu bere z RTC_NOINIT (RTC_DATA_ATTR se pri WDT resetu nemusi zachovat).
  bcInit();
  bootCounter = bcBootCount();
  bcMark(BT_SETUP, BS_START);

  initLed();

  pinMode(PIN_BTN_SEND, INPUT_PULLUP);
  pinMode(PIN_BTN_DIAG, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_SEND), isrSend, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_DIAG), isrDiag, FALLING);

  setNeedsUserAttention(true);
  updateLedFromState();

  logLine("");
  printDivider();
  logLine(F("[BOOT] ESP32 dashboard starting"));
  logValue(F("[BOOT] Boot count: "), (unsigned long)bootCounter);
  printDivider();
  bcPrintPrev();   // co se dělo před předchozím restartem (WDT / panic / ...)

  if (!LittleFS.begin(true)) {
    logLine(F("[FS] LittleFS mount failed"));
    setFatalError(true);
    updateLedFromState();
    return;
  }
  logLine(F("[FS] LittleFS mounted"));

  bcMark(BT_SETUP, BS_SETUP_WIFI);
  scanWifi();

  if (!connectWiFi()) {
    logLine(F("[BOOT] WiFi failed"));
    setNeedsUserAttention(true);
    updateLedFromState();
  }

  if (WiFi.status() == WL_CONNECTED) startMdns();

  startDashboardServer();

  if (WiFi.status() == WL_CONNECTED) {
    logValue(F("[WEB] http://"), WiFi.localIP().toString());
    logValue(F("[WEB] http://"), String(DASHBOARD_MDNS_NAME) + ".local");
  }

  if (getNetReady()) {
    bcMark(BT_SETUP, BS_SETUP_NTP);
    setupTime();
    setLastSent(loadLastSentFile());
    logValue(F("[STATE] lastSent="), getLastSent());
    logValue(F("[STATE] currentNames="), getCurrentNames());
    setNeedsUserAttention(false);
    setLogicDone(true);
    updateLedFromState();
  }

  startBackgroundTasks();

  broadcastDashboard();
  bcMark(BT_SETUP, BS_SETUP_DONE);
  logLine(F("[BOOT] setup finished"));
}

// =====================================================
// LOOP
// =====================================================
// Hlavni kostra: jen websocket cleanup, tlacitka a heartbeat.
// Polling zarizeni, auto-svatky a emergency reseni bezi ve
// vlastnich staticky alokovanych RTOS taskach (viz tasks.cpp).

void loop() {
  ws.cleanupClients();

  if (fetchAndClearSendRequested()) {
    bcMark(BT_LOOP, BS_BUTTONS);
    doManualSend();
    broadcastDashboard();
  }

  if (fetchAndClearDiagRequested()) {
    bcMark(BT_LOOP, BS_BUTTONS);
    doDiagnostics();
    broadcastDashboard();
  }

  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = millis();
    heartbeat();
    bcHeartbeat();   // snapshot heap/stacky do RTC (+ krok "heartbeat")
    broadcastDashboard();
  }

  updateLedFromState();
  delay(20);
}
