#include "state.h"
#include "config.h"

// =====================================================
// MUTEXES
// =====================================================

SemaphoreHandle_t serialMutex = nullptr;
SemaphoreHandle_t ledMutex    = nullptr;
SemaphoreHandle_t stateMutex  = nullptr;
SemaphoreHandle_t deviceMutex = nullptr;
SemaphoreHandle_t alarmMutex  = nullptr;
SemaphoreHandle_t weatherMutex = nullptr;
SemaphoreHandle_t climateMutex = nullptr;

void initMutexes() {
  serialMutex  = xSemaphoreCreateMutex();
  ledMutex     = xSemaphoreCreateMutex();
  stateMutex   = xSemaphoreCreateMutex();
  deviceMutex  = xSemaphoreCreateMutex();
  alarmMutex   = xSemaphoreCreateMutex();
  weatherMutex = xSemaphoreCreateMutex();
  climateMutex = xSemaphoreCreateMutex();
}

bool lockMutex(SemaphoreHandle_t mtx, TickType_t timeout) {
  if (!mtx) return false;
  return xSemaphoreTake(mtx, timeout) == pdTRUE;
}

void unlockMutex(SemaphoreHandle_t mtx) {
  if (mtx) xSemaphoreGive(mtx);
}

// =====================================================
// BOOT COUNTER
// =====================================================

RTC_DATA_ATTR uint32_t bootCounter = 0;

// =====================================================
// GLOBAL FLAGS (vsechny chranene stateMutex)
// =====================================================

static bool netReady          = false;
static bool timeReady          = false;
static bool logicDone          = false;
static bool fatalError         = false;
static bool needsUserAttention = false;
static bool autoSendDone       = false;
static bool lastSendOk         = false;

struct tm timeinfo;

static String currentDateKey  = "";
static String currentFullDate = "";
static String currentNames    = "";
static String lastSent        = "";

void setNetReady(bool v) {
  if (lockMutex(stateMutex)) { netReady = v; unlockMutex(stateMutex); }
}
bool getNetReady() {
  bool v = false;
  if (lockMutex(stateMutex)) { v = netReady; unlockMutex(stateMutex); }
  return v;
}

void setTimeReady(bool v) {
  if (lockMutex(stateMutex)) { timeReady = v; unlockMutex(stateMutex); }
}
bool getTimeReady() {
  bool v = false;
  if (lockMutex(stateMutex)) { v = timeReady; unlockMutex(stateMutex); }
  return v;
}

void setLogicDone(bool v) {
  if (lockMutex(stateMutex)) { logicDone = v; unlockMutex(stateMutex); }
}
bool getLogicDone() {
  bool v = false;
  if (lockMutex(stateMutex)) { v = logicDone; unlockMutex(stateMutex); }
  return v;
}

void setFatalError(bool v) {
  if (lockMutex(stateMutex)) { fatalError = v; unlockMutex(stateMutex); }
}
bool getFatalError() {
  bool v = false;
  if (lockMutex(stateMutex)) { v = fatalError; unlockMutex(stateMutex); }
  return v;
}

void setNeedsUserAttention(bool v) {
  if (lockMutex(stateMutex)) { needsUserAttention = v; unlockMutex(stateMutex); }
}
bool getNeedsUserAttention() {
  bool v = false;
  if (lockMutex(stateMutex)) { v = needsUserAttention; unlockMutex(stateMutex); }
  return v;
}

void setAutoSendDone(bool v) {
  if (lockMutex(stateMutex)) { autoSendDone = v; unlockMutex(stateMutex); }
}
bool getAutoSendDone() {
  bool v = false;
  if (lockMutex(stateMutex)) { v = autoSendDone; unlockMutex(stateMutex); }
  return v;
}

void setLastSendOk(bool v) {
  if (lockMutex(stateMutex)) { lastSendOk = v; unlockMutex(stateMutex); }
}
bool getLastSendOk() {
  bool v = false;
  if (lockMutex(stateMutex)) { v = lastSendOk; unlockMutex(stateMutex); }
  return v;
}

void setCurrentDateKey(const String &v) {
  if (lockMutex(stateMutex)) { currentDateKey = v; unlockMutex(stateMutex); }
}
String getCurrentDateKey() {
  String v;
  if (lockMutex(stateMutex)) { v = currentDateKey; unlockMutex(stateMutex); }
  return v;
}

void setCurrentFullDate(const String &v) {
  if (lockMutex(stateMutex)) { currentFullDate = v; unlockMutex(stateMutex); }
}
String getCurrentFullDate() {
  String v;
  if (lockMutex(stateMutex)) { v = currentFullDate; unlockMutex(stateMutex); }
  return v;
}

void setCurrentNames(const String &v) {
  if (lockMutex(stateMutex)) { currentNames = v; unlockMutex(stateMutex); }
}
String getCurrentNames() {
  String v;
  if (lockMutex(stateMutex)) { v = currentNames; unlockMutex(stateMutex); }
  return v;
}

void setLastSent(const String &v) {
  if (lockMutex(stateMutex)) { lastSent = v; unlockMutex(stateMutex); }
}
String getLastSent() {
  String v;
  if (lockMutex(stateMutex)) { v = lastSent; unlockMutex(stateMutex); }
  return v;
}

// =====================================================
// DEVICE STATE INSTANCES
// =====================================================

DeviceState wledPokoj = {
  "wled-pokoj", "WLED pokoj", "wled",
  WLED_POKOJ_FALLBACK_IP, String(WLED_POKOJ_HOST) + ".local",
  false, 0, 0, 0, false, 0, 0, "", "", "", "", "-", ""
};

DeviceState mcEsp = {
  "mc", "MC ESP", "esp",
  "", String(MC_HOST) + ".local",
  false, 0, 0, 0, false, 0, 0, "", "Minecraft Controller", "unknown", "ESP32", "0.1.0", ""
};

DeviceState panicNode = {
  "panic-c3", "Panic Button", "esp",
  "", String(PANIC_NODE_HOST) + ".local",
  false, 0, 0, 0, false, 0, 0, "", "panic-button", "unknown", "ESP32-C3", "0.2.0", ""
};

// =====================================================
// ALARM STATE
// =====================================================

AlarmState activeAlarm = { false, "", "", "", 0, false, "", 0, 0 };

// =====================================================
// WEATHER STATE
// =====================================================

WeatherState weather = { false, 0, 0, 0, 0, "", "", "", "", 0, 0, 0, "", "", 0, {}, 0 };

// =====================================================
// CLIMATE STATE
// =====================================================

ClimateState climate = { { false, 0, 0, 0, 0, 0, 0 }, { false, 0, 0, 0, 0, 0, 0 } };

// =====================================================
// DESKTOP STATE
// =====================================================

DesktopState desktop = { false, false, 0, 0 };
