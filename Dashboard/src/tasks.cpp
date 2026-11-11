#include "tasks.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "state.h"
#include "log_util.h"
#include "led.h"
#include "devices.h"
#include "time_svatky.h"
#include "discord.h"
#include "ntfy.h"
#include "weather.h"
#include "sensors.h"
#include "diagnostics.h"
#include "ntfy_cmd.h"
#include "desktop.h"
#include "web_server.h"
#include "breadcrumbs.h"
#include "heater.h"

// =====================================================
// ISR / BUTTON FLAGS
// =====================================================

static portMUX_TYPE isrMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool sendRequested = false;
static volatile bool diagRequested = false;

void IRAM_ATTR isrSend() {
  portENTER_CRITICAL_ISR(&isrMux);
  sendRequested = true;
  portEXIT_CRITICAL_ISR(&isrMux);
}

void IRAM_ATTR isrDiag() {
  portENTER_CRITICAL_ISR(&isrMux);
  diagRequested = true;
  portEXIT_CRITICAL_ISR(&isrMux);
}

bool fetchAndClearSendRequested() {
  bool v;
  portENTER_CRITICAL(&isrMux);
  v = sendRequested;
  sendRequested = false;
  portEXIT_CRITICAL(&isrMux);
  return v;
}

bool fetchAndClearDiagRequested() {
  bool v;
  portENTER_CRITICAL(&isrMux);
  v = diagRequested;
  diagRequested = false;
  portEXIT_CRITICAL(&isrMux);
  return v;
}

// =====================================================
// STATIC TASK MEMORY
// =====================================================

static StaticTask_t wledPollTaskBuffer;
static StackType_t  wledPollTaskStack[6144];
static TaskHandle_t wledPollTaskHandle = nullptr;

static StaticTask_t espPollTaskBuffer;
static StackType_t  espPollTaskStack[6144];
static TaskHandle_t espPollTaskHandle = nullptr;

static StaticTask_t autoLogicTaskBuffer;
static StackType_t  autoLogicTaskStack[8192];
static TaskHandle_t autoLogicTaskHandle = nullptr;

static StaticTask_t emergencyTaskBuffer;
static StackType_t  emergencyTaskStack[8192];
static TaskHandle_t emergencyTaskHandle = nullptr;

static StaticTask_t weatherTaskBuffer;
static StackType_t  weatherTaskStack[16384];
static TaskHandle_t weatherTaskHandle = nullptr;

static StaticTask_t ntfyCmdTaskBuffer;
static StackType_t  ntfyCmdTaskStack[12288];   // TLS
static TaskHandle_t ntfyCmdTaskHandle = nullptr;

static StaticTask_t heaterTaskBuffer;
static StackType_t  heaterTaskStack[12288];    // TLS pro ntfy oznameni
static TaskHandle_t heaterTaskHandle = nullptr;

static StaticTask_t climateTaskBuffer;
static StackType_t  climateTaskStack[12288];   // TLS pro ntfy report
static TaskHandle_t climateTaskHandle = nullptr;

// =====================================================
// WLED POLL TASK
// =====================================================
// Vlastni task jen pro WLED - aby pomala/vypadla WLED odpoved
// nezdrzovala polling ESPcek (panic-button, mc).

static void wledPollTask(void *pv) {
  logLine(F("[TASK][WLED] started"));
  for (;;) {
    if (getNetReady()) {
      bcMark(BT_WLED, BS_WLED_POLL);
      pollWLED();
      bcMark(BT_WLED, BS_BROADCAST);
      broadcastDashboard();
    }
    bcMark(BT_WLED, BS_IDLE);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DEVICE_POLL_INTERVAL_MS));   // budi i requestRefreshAll()
  }
}

// =====================================================
// ESP DEVICE POLL TASK (mc + panic-button)
// =====================================================

static void espPollTask(void *pv) {
  logLine(F("[TASK][ESP] started"));
  for (;;) {
    if (getNetReady()) {
      bcMark(BT_ESP, BS_ESP_POLL_MC);
      pollCustomEsp(mcEsp,     MC_HOST,         MC_FALLBACK_IP,         MC_STATUS_ENDPOINT);
      bcMark(BT_ESP, BS_ESP_POLL_PANIC);
      pollCustomEsp(panicNode, PANIC_NODE_HOST, PANIC_NODE_FALLBACK_IP, PANIC_NODE_STATUS_ENDPOINT);
      bcMark(BT_ESP, BS_BROADCAST);
      broadcastDashboard();
    }
    bcMark(BT_ESP, BS_IDLE);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DEVICE_POLL_INTERVAL_MS));
  }
}

// =====================================================
// AUTO LOGIC TASK (svatky)
// =====================================================

static void runAutoSendLogic() {
  if (!getNetReady() || !getTimeReady()) return;

  if (!updateLocalTime()) {
    logLine(F("[AUTO] updateLocalTime failed"));
    setNeedsUserAttention(true);
    broadcastDashboard();
    return;
  }

  if (shouldAutoSend()) {
    logLine(F("[AUTO] conditions met"));
    bcMark(BT_AUTO, BS_DISCORD);
    bool ok = sendMessage("Dnes ma svatek: " + getCurrentNames());
    setLastSendOk(ok);
    if (ok) {
      saveLastSentFile(getCurrentFullDate());
      setLastSent(getCurrentFullDate());
      setAutoSendDone(true);
      setNeedsUserAttention(false);
      logLine(F("[AUTO] success"));
    } else {
      setNeedsUserAttention(true);
      logLine(F("[AUTO] failed"));
    }
    updateLedFromState();
    broadcastDashboard();
  } else {
    setAutoSendDone(false);
  }
}

static void autoLogicTask(void *pv) {
  logLine(F("[TASK][AUTO] started"));
  for (;;) {
    bcMark(BT_AUTO, BS_AUTO_LOGIC);
    runAutoSendLogic();
    bcMark(BT_AUTO, BS_IDLE);
    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

// =====================================================
// EMERGENCY TASK
// =====================================================
// Normalne idle. Jakmile activeAlarm.active naskoci na true,
// suspendne wled/esp/auto-logic tasky (at uz nezere CPU/sit zbytecnym
// pollingem behem SOS) a stara se jen o opakovane ntfy notifikace,
// dokud panic-button nevysle "panic_reset". Pak vse resumne.
//
// Web server bezi nezavisle (vlastni AsyncTCP/LWIP kontext), takze
// /api/panic (reset) a websocket zustavaji funkcni po celou dobu.

static void emergencyTask(void *pv) {
  logLine(F("[TASK][EMERGENCY] started"));
  bool wasActive = false;

  for (;;) {
    bool     active  = false;
    uint32_t lastAt  = 0;
    int      count   = 0;
    String   evId, devName;

    if (lockMutex(alarmMutex)) {
      active  = activeAlarm.active;
      lastAt  = activeAlarm.lastNtfyAt;
      count   = activeAlarm.ntfyCount;
      evId    = activeAlarm.eventId;
      devName = activeAlarm.deviceName;
      unlockMutex(alarmMutex);
    }

    if (active != wasActive) bcMark(BT_EMERG, BS_EMERG_CHANGE);

    if (active && !wasActive) {
      logLine(F("[EMERGENCY] activated - suspending background tasks"));
      if (wledPollTaskHandle)   vTaskSuspend(wledPollTaskHandle);
      if (espPollTaskHandle)    vTaskSuspend(espPollTaskHandle);
      if (autoLogicTaskHandle)  vTaskSuspend(autoLogicTaskHandle);
      wasActive = true;
    } else if (!active && wasActive) {
      logLine(F("[EMERGENCY] cleared - resuming background tasks"));
      if (wledPollTaskHandle)   vTaskResume(wledPollTaskHandle);
      if (espPollTaskHandle)    vTaskResume(espPollTaskHandle);
      if (autoLogicTaskHandle)  vTaskResume(autoLogicTaskHandle);
      wasActive = false;
    }

    if (active && millis() - lastAt >= NTFY_REPEAT_INTERVAL_MS) {
      bcMark(BT_EMERG, BS_NTFY_PANIC);
      sendNtfyPanic(devName, evId, count);
      if (lockMutex(alarmMutex)) {
        activeAlarm.lastNtfyAt = millis();
        activeAlarm.ntfyCount++;
        unlockMutex(alarmMutex);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(active ? EMERGENCY_TICK_ACTIVE_MS : EMERGENCY_TICK_IDLE_MS));
  }
}

// =====================================================
// WEATHER TASK
// =====================================================
// Open-Meteo (bez API klice), poll co WEATHER_POLL_INTERVAL_MS (15 min).
// Pri prvnim behu pollne hned, ne az po prvnim intervalu.

static void weatherTask(void *pv) {
  logLine(F("[TASK][WEATHER] started"));
  for (;;) {
    if (getNetReady()) {
      bcMark(BT_WEATHER, BS_WEATHER_POLL);
      if (pollWeather()) {
        bcMark(BT_WEATHER, BS_BROADCAST);
        broadcastDashboard();
      }
    }
    bcMark(BT_WEATHER, BS_IDLE);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WEATHER_POLL_INTERVAL_MS));
  }
}

// =====================================================
// CLIMATE TASK (DHT11 vnitrni + DHT22 venkovni)
// =====================================================
// Cte senzory co CLIMATE_POLL_INTERVAL_MS (1 min). Nepotrebuje sit,
// takze bezi i bez WiFi. Prvni cteni hned po startu (DHT potrebuje
// ~1-2 s po power-on, proto uvodni delay).

static String fmtSensor(const char *label, const DhtReading &r) {
  if (!r.valid) return String(label) + ": senzor nedostupny";
  return String(label) + ": " + String(r.temp, 1) + " C, vlhkost " + String((int)round(r.hum)) + " %";
}

// hodinovy report na ntfy: teploty + pocasi + mesic
static void sendHourlyReport() {
  bcMark(BT_CLIMATE, BS_NTFY_REPORT);
  DhtReading in, out;
  if (lockMutex(climateMutex)) { in = climate.indoor; out = climate.outdoor; unlockMutex(climateMutex); }

  String body = fmtSensor("Venku", out) + "\n" + fmtSensor("Uvnitr", in);

  if (lockMutex(weatherMutex)) {
    if (weather.valid) {
      body += "\nPocasi: " + String(weather.temp, 1) + " C (pocitove " + String(weather.apparentTemp, 1) +
              " C), " + weather.condition + ", vitr " + String((int)round(weather.windSpeed)) + " km/h";
      body += "\nSlunce: vychod " + weather.sunrise + ", zapad " + weather.sunset;
      body += "\nMesic: " + weather.moonName;
    }
    unlockMutex(weatherMutex);
  }

  uint32_t upS = millis() / 1000;
  body += "\nUptime: " + String(upS / 3600) + " h " + String((upS % 3600) / 60) + " min, restart: " + resetReasonStr() +
          " (boot #" + String((unsigned long)bootCounter) + ")";
  body += "\n" + bcHeapLine();

  // po padu (WDT / panic / brownout) pridat souhrn z RTC breadcrumbs do prvnich reportu
  if (bcPrevAbnormal() && upS < BC_REPORT_PREV_SECS) body += "\n\n" + bcPrevSummary();

  if (out.valid && out.temp <= 1.0f) body += "\n\nPOZOR, ZIMA (venku " + String(out.temp, 1) + " C)";

  sendNtfyReport("Dashboard report", body);
}

static volatile bool ntfyTestRequested = false;

// Hodinovy report je vazany na skutecny cas (NTP), ne na millis():
// posle se jednou v prvnich minutach kazde hodiny (:00-:02) a cekani
// se zkrati tak, aby task vstal tesne po cele hodine. Zadne hromadeni
// zpozdeni (cteni DHT + TLS odeslani) z hodiny na hodinu.
static bool wallClock(struct tm &t) {
  return getTimeReady() && getLocalTime(&t, 0);
}

static void climateTask(void *pv) {
  logLine(F("[TASK][CLIMATE] started"));
  vTaskDelay(pdMS_TO_TICKS(2000));
  int lastReportKey = -1;   // den_v_roce*24 + hodina posledniho odeslaneho reportu

  for (;;) {
    bcMark(BT_CLIMATE, BS_CLIMATE_READ);
    if (pollClimate()) {
      bcMark(BT_CLIMATE, BS_BROADCAST);
      broadcastDashboard();
    }

    if (ntfyTestRequested) {
      ntfyTestRequested = false;
      sendHourlyReport();   // rucni test (POST /api/ntfy-test), nezasahuje do casovani
    }

    struct tm t;
    uint32_t waitMs = CLIMATE_POLL_INTERVAL_MS;

    if (getNetReady() && wallClock(t)) {
      int key = t.tm_yday * 24 + t.tm_hour;
      if (t.tm_min < 3 && key != lastReportKey) {
        bcHourlySample();   // hodinovy vzorek heapu do RTC (trend / uniky), i pri alarmu
        bool alarm = false;
        if (lockMutex(alarmMutex)) { alarm = activeAlarm.active; unlockMutex(alarmMutex); }
        if (!alarm) sendHourlyReport();
        lastReportKey = key;
        if (!wallClock(t)) t.tm_min = 0, t.tm_sec = 0;   // cas po odeslani
      }
      // cekat nejvyse do dalsi cele hodiny (+0.5 s), ne vzdy 60 s
      uint32_t msToHour = (uint32_t)(3600 - (t.tm_min * 60 + t.tm_sec)) * 1000UL + 500UL;
      if (msToHour < waitMs) waitMs = msToHour;
    }

    bcMark(BT_CLIMATE, BS_IDLE);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
  }
}

// =====================================================
// NTFY COMMAND + DESKTOP HEALTH TASK
// =====================================================
// Jeden task (12 kB stack kvuli TLS) dela dve veci s ruznymi intervaly:
//  - kazdych NTFY_CMD_POLL_INTERVAL_MS zkontroluje ntfy topic (zprava "WOL")
//  - kazdych DESKTOP_CHECK_INTERVAL_MS (po WOL kazdych DESKTOP_CHECK_FAST_MS)
//    zkontroluje, jestli je desktop online

static void ntfyCmdTask(void *pv) {
  logLine(F("[TASK][NTFY-CMD] started"));
  vTaskDelay(pdMS_TO_TICKS(10000));
  uint32_t lastNtfy = 0, lastDesktop = 0;
  for (;;) {
    if (getNetReady()) {
      const uint32_t now = millis();
      const uint32_t deskEvery = desktopWakeWindowActive() ? DESKTOP_CHECK_FAST_MS : DESKTOP_CHECK_INTERVAL_MS;

      if (now - lastDesktop >= deskEvery || lastDesktop == 0) {
        bcMark(BT_NTFYCMD, BS_DESKTOP_POLL);
        desktopPoll();
        lastDesktop = millis();
        bcMark(BT_NTFYCMD, BS_IDLE);
      }
      if (millis() - lastNtfy >= NTFY_CMD_POLL_INTERVAL_MS || lastNtfy == 0) {
        bcMark(BT_NTFYCMD, BS_NTFYCMD_POLL);
        ntfyCmdPoll();
        lastNtfy = millis();
        bcMark(BT_NTFYCMD, BS_IDLE);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// =====================================================
// MANUAL REFRESH
// =====================================================
// Vola se z web handleru (async_tcp task, kde se nesmi blokovat -
// task watchdog ~5 s). Jen vzbudi poll tasky, ty udelaji praci
// ve svem vlakne a vysledek posunou websocketem.

void requestRefreshAll() {
  if (wledPollTaskHandle) xTaskNotifyGive(wledPollTaskHandle);
  if (espPollTaskHandle)  xTaskNotifyGive(espPollTaskHandle);
  if (weatherTaskHandle)  xTaskNotifyGive(weatherTaskHandle);
  if (climateTaskHandle)  xTaskNotifyGive(climateTaskHandle);
}

// volny stack tasku v bajtech (ESP-IDF vraci high-water mark v bajtech), pro RTC breadcrumbs.
// Poradi: wled, esp, auto, emergency, weather, climate, ntfycmd
void getTaskStackFree(uint16_t out[7]) {
  TaskHandle_t h[7] = {
    wledPollTaskHandle, espPollTaskHandle, autoLogicTaskHandle, emergencyTaskHandle,
    weatherTaskHandle, climateTaskHandle, ntfyCmdTaskHandle
  };
  for (int i = 0; i < 7; i++) {
    const UBaseType_t w = h[i] ? uxTaskGetStackHighWaterMark(h[i]) : 0;
    out[i] = w > 65535 ? 65535 : (uint16_t)w;
  }
}

// vzbudi climate task, ktery hned posle report na ntfy (test)
void requestNtfyTest() {
  ntfyTestRequested = true;
  if (climateTaskHandle) xTaskNotifyGive(climateTaskHandle);
}

// =====================================================
// STARTUP
// =====================================================

void startBackgroundTasks() {
  initSensors();
  initHeater();

  wledPollTaskHandle = xTaskCreateStaticPinnedToCore(
    wledPollTask, "WledPollTask", 6144, NULL, 1,
    wledPollTaskStack, &wledPollTaskBuffer, 1);

  espPollTaskHandle = xTaskCreateStaticPinnedToCore(
    espPollTask, "EspPollTask", 6144, NULL, 1,
    espPollTaskStack, &espPollTaskBuffer, 1);

  autoLogicTaskHandle = xTaskCreateStaticPinnedToCore(
    autoLogicTask, "AutoLogicTask", 8192, NULL, 1,
    autoLogicTaskStack, &autoLogicTaskBuffer, 1);

  emergencyTaskHandle = xTaskCreateStaticPinnedToCore(
    emergencyTask, "EmergencyTask", 8192, NULL, 2,
    emergencyTaskStack, &emergencyTaskBuffer, 1);

  weatherTaskHandle = xTaskCreateStaticPinnedToCore(
    weatherTask, "WeatherTask", 16384, NULL, 1,
    weatherTaskStack, &weatherTaskBuffer, 1);

  climateTaskHandle = xTaskCreateStaticPinnedToCore(
    climateTask, "ClimateTask", 12288, NULL, 1,
    climateTaskStack, &climateTaskBuffer, 1);

  ntfyCmdTaskHandle = xTaskCreateStaticPinnedToCore(
    ntfyCmdTask, "NtfyCmdTask", 12288, NULL, 1,
    ntfyCmdTaskStack, &ntfyCmdTaskBuffer, 1);

  heaterTaskHandle = xTaskCreateStaticPinnedToCore(
    heaterTask, "HeaterTask", 12288, NULL, 1,
    heaterTaskStack, &heaterTaskBuffer, 1);
}
