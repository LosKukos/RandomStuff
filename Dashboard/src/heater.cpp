#include "heater.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config.h"
#include "state.h"
#include "log_util.h"
#include "ntfy.h"
#include "breadcrumbs.h"

static const uint16_t IR_CODES[HB_COUNT] = {
  0xDC3,  // power
  0xD90,  // mode
  0xD88,  // spin
  0xD82,  // clock
  0xD84,  // eco
  0xDA0,  // minus
  0xDC6   // plus
};

static IRsend irsend(PIN_IR_TX);
static QueueHandle_t cmdQueue = nullptr;
static SemaphoreHandle_t heaterMutex = nullptr;

static HeaterEst est = {false, HM_FAN};
static uint32_t  onSinceMs = 0;      // millis() zapnuti (0 = vypnuto)
static uint32_t  lastSentMs = 0;
static bool      lastSendOk = true;
static volatile bool syncNotePending = false;   // ntfy z tasku, ne z async_tcp handleru

static const char *modeText(uint8_t m) {
  return m == HM_I ? "Topeni I (1000 W)" : m == HM_II ? "Topeni II (2000 W)" : "Ventilator";
}

static void persist() {
  File f = LittleFS.open("/heater.json", "w");
  if (!f) return;
  JsonDocument d;
  d["on"] = est.on;
  d["mode"] = est.mode;
  serializeJson(d, f);
  f.close();
}

static void notify(const String &body) {
  ntfyPostQuiet("Topeni", body, "default", NTFY_CMD_TOPIC);
}

void initHeater() {
  heaterMutex = xSemaphoreCreateMutex();
  cmdQueue = xQueueCreate(8, sizeof(uint8_t));
  irsend.begin();

  // Po vypadku napajeni je topeni (stejny okruh) vypnute. Po jinem restartu (crash/WDT/flash)
  // bezelo dal, tak vezmeme ulozeny odhad.
  if (esp_reset_reason() != ESP_RST_POWERON) {
    File f = LittleFS.open("/heater.json", "r");
    if (f) {
      JsonDocument d;
      if (!deserializeJson(d, f)) {
        est.on = d["on"] | false;
        est.mode = d["mode"] | 0;
        if (est.mode > HM_II) est.mode = HM_FAN;
        if (est.on) onSinceMs = millis();   // limit se pocita znovu od restartu
      }
      f.close();
    }
  } else {
    persist();
  }
}

bool heaterSendButton(HeaterBtn b) {
  if (!cmdQueue || b >= HB_COUNT) return false;
  const uint8_t v = (uint8_t)b;
  return xQueueSend(cmdQueue, &v, 0) == pdTRUE;
}

void heaterSync(bool on, uint8_t mode) {
  if (!heaterMutex) return;
  lockMutex(heaterMutex, pdMS_TO_TICKS(200));
  est.on = on;
  est.mode = mode > HM_II ? HM_FAN : mode;
  onSinceMs = on ? millis() : 0;
  persist();
  unlockMutex(heaterMutex);
  syncNotePending = true;
}

void heaterStatus(JsonDocument &doc) {
  bool on = false; uint8_t mode = 0; uint32_t since = 0;
  if (heaterMutex && lockMutex(heaterMutex, pdMS_TO_TICKS(200))) {
    on = est.on; mode = est.mode; since = onSinceMs;
    unlockMutex(heaterMutex);
  }
  doc["on"] = on;
  doc["mode"] = mode;
  doc["modeText"] = modeText(mode);
  doc["control"] = "manual";
  const uint32_t onS = (on && since) ? (millis() - since) / 1000UL : 0;
  doc["onForS"] = onS;
  doc["manualLeftS"] = on ? (onS >= HEATER_MANUAL_LIMIT_S ? 0 : HEATER_MANUAL_LIMIT_S - onS) : 0;
  doc["lastSendOk"] = lastSendOk;
}

static void sendOne(HeaterBtn b) {
  bcMark(BT_HEATER, BS_HEATER_SEND);
  irsend.sendSymphony(IR_CODES[b], 12, 3);
  lastSentMs = millis();
  lastSendOk = true;
  logLine(String("[HEATER] IR sent: ") + heaterBtnName(b));
  bcMark(BT_HEATER, BS_IDLE);
}

void heaterTask(void *pv) {
  logLine(F("[TASK][HEATER] started"));
  uint32_t lastLimitCheck = 0;
  for (;;) {
    uint8_t v;
    if (xQueueReceive(cmdQueue, &v, pdMS_TO_TICKS(500)) == pdTRUE) {
      const HeaterBtn b = (HeaterBtn)v;
      sendOne(b);
      lockMutex(heaterMutex, pdMS_TO_TICKS(500));
      const bool wasOn = est.on;
      heaterApplyButton(est, b);
      if (est.on && !wasOn) onSinceMs = millis();
      if (!est.on) onSinceMs = 0;
      persist();
      const HeaterEst now = est;
      unlockMutex(heaterMutex);

      String msg = String("Rucne: ") + heaterBtnName(b);
      if (b == HB_POWER || b == HB_MODE) msg += String(" -> ") + (now.on ? modeText(now.mode) : "OFF");
      notify(msg);
      vTaskDelay(pdMS_TO_TICKS(400));   // mezera mezi odeslanimi
    }

    if (syncNotePending) {
      syncNotePending = false;
      HeaterEst now = est;
      notify(String("Sync: ") + (now.on ? String("ON, ") + modeText(now.mode) : String("OFF")));
    }

    // Limit rucniho rezimu: po 4 h vypnout
    if (millis() - lastLimitCheck >= 5000) {
      lastLimitCheck = millis();
      bool over = false;
      if (lockMutex(heaterMutex, pdMS_TO_TICKS(200))) {
        over = est.on && onSinceMs && (millis() - onSinceMs) / 1000UL >= HEATER_MANUAL_LIMIT_S;
        unlockMutex(heaterMutex);
      }
      if (over) {
        logLine(F("[HEATER] manual limit reached, power off"));
        heaterSendButton(HB_POWER);
        notify("Limit manualu (4 h): topeni automaticky vypnuto");
      }
    }
  }
}
