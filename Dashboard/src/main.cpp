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

unsigned long lastHeartbeat = 0;

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  initMutexes();

  bootCounter++;

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

  if (!LittleFS.begin(true)) {
    logLine(F("[FS] LittleFS mount failed"));
    setFatalError(true);
    updateLedFromState();
    return;
  }
  logLine(F("[FS] LittleFS mounted"));

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
    doManualSend();
    broadcastDashboard();
  }

  if (fetchAndClearDiagRequested()) {
    doDiagnostics();
    broadcastDashboard();
  }

  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = millis();
    heartbeat();
    broadcastDashboard();
  }

  updateLedFromState();
  delay(20);
}
