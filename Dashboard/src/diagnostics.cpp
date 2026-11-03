#include "diagnostics.h"
#include <WiFi.h>
#include <esp_system.h>
#include "log_util.h"
#include "led.h"
#include "time_svatky.h"
#include "web_server.h"

void printDeviceDiag(const DeviceState &device) {
  logValue(F("[DIAG] id: "),     device.id);
  logValue(F("[DIAG] name: "),   device.name);
  logValue(F("[DIAG] host: "),   device.host);
  logValue(F("[DIAG] ip: "),     device.ip);
  logValue(F("[DIAG] online: "), device.online ? String("true") : String("false"));
  logValue(F("[DIAG] error: "),  device.error);
}

void printDiagnostics() {
  printDivider();
  logLine(F("[DIAG] System diagnostics"));
  logValue(F("[DIAG] Boot counter: "), (unsigned long)bootCounter);
  logValue(F("[DIAG] WiFi: "), (WiFi.status() == WL_CONNECTED) ? String("CONNECTED") : String("DISCONNECTED"));

  if (WiFi.status() == WL_CONNECTED) {
    logValue(F("[DIAG] IP: "), WiFi.localIP().toString());
    logValue(F("[DIAG] RSSI: "), WiFi.RSSI());
  }

  logValue(F("[DIAG] netReady: "),  getNetReady()  ? String("true") : String("false"));
  logValue(F("[DIAG] timeReady: "), getTimeReady() ? String("true") : String("false"));
  logValue(F("[DIAG] fatalError: "), getFatalError() ? String("true") : String("false"));
  logValue(F("[DIAG] currentFullDate: "), getCurrentFullDate());
  logValue(F("[DIAG] currentNames: "),    getCurrentNames());
  logValue(F("[DIAG] lastSent: "),        getLastSent());

  if (lockMutex(deviceMutex)) {
    printDivider();
    printDeviceDiag(wledPokoj);
    printDeviceDiag(mcEsp);
    printDeviceDiag(panicNode);
    unlockMutex(deviceMutex);
  }

  if (lockMutex(alarmMutex)) {
    printDivider();
    logValue(F("[DIAG] alarm.active: "),     activeAlarm.active ? String("true") : String("false"));
    logValue(F("[DIAG] alarm.eventId: "),    activeAlarm.eventId);
    logValue(F("[DIAG] alarm.deviceName: "), activeAlarm.deviceName);
    logValue(F("[DIAG] alarm.ntfyCount: "),  activeAlarm.ntfyCount);
    unlockMutex(alarmMutex);
  }

  printTimeInfo();
  printDivider();
}

void doDiagnostics() {
  setNeedsUserAttention(true);
  updateLedFromState();
  logLine(F("[USER] diagnostics requested"));
  printDiagnostics();
  broadcastDashboard();
  setNeedsUserAttention(false);
  updateLedFromState();
}

void heartbeat() {
  logLine(F("[HEARTBEAT] alive"));
  logValue(F("[HEARTBEAT] uptime ms: "), (unsigned long)millis());
  logValue(F("[HEARTBEAT] free heap: "), (unsigned long)ESP.getFreeHeap());
  logValue(F("[HEARTBEAT] WiFi: "), (WiFi.status() == WL_CONNECTED) ? String("yes") : String("no"));
}

String resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on (zapnuti / vypadek napajeni)";
    case ESP_RST_EXT:       return "externi reset (EN tlacitko)";
    case ESP_RST_SW:        return "software reset (esp_restart)";
    case ESP_RST_PANIC:     return "PANIC (pad programu / abort)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog (zasekly task)";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "probuzeni z deep sleep";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (nizke napeti / slabe napajeni)";
    case ESP_RST_SDIO:      return "SDIO reset";
    default:                return "neznamy (" + String((int)esp_reset_reason()) + ")";
  }
}
