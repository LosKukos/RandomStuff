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

#define CMD_FORCE_OFF 0x80   // Power + po odeslani povazovat topeni za vypnute (zasah proti prehriti)
#define NOTE_LEN 140
#define CLIMATE_STALE_MS 300000UL

static IRsend irsend(PIN_IR_TX);
static QueueHandle_t cmdQueue = nullptr;
static QueueHandle_t noteQueue = nullptr;   // ntfy se posila jen z tasku (TLS), ne z async_tcp
static SemaphoreHandle_t heaterMutex = nullptr;

static HeaterEst est = {false, HM_FAN};
static uint32_t  onSinceMs = 0;      // millis() zapnuti (0 = vypnuto)
static bool      lastSendOk = true;
static uint8_t   irRepeat = 3;           // dalsi rámce za jeden stisk (celkem 1 + irRepeat)

// AUTO
static bool      autoActive = false;
static int       autoTarget = 21;
static int       autoMinutes = 60;       // nastaveny cas (default i pro dalsi Start)
static uint32_t  autoStartMs = 0;
static uint32_t  autoTotalS = 0;
static uint32_t  autoLastSeen = 0;       // updatedAtMs naposledy zpracovaneho mereni
static float     autoPrevTemp = 0;
static HeaterOverheat oh = {false, 0, 0, 0};

// upozorneni v UI (do dalsi akce uzivatele)
static char alertKind[8] = "";
static char alertTitle[40] = "";
static char alertText[120] = "";

static const char *modeText(uint8_t m) {
  return m == HM_I ? "Topeni I (1000 W)" : m == HM_II ? "Topeni II (2000 W)" : "Ventilator";
}

static void persist() {
  File f = LittleFS.open("/heater.json", "w");
  if (!f) return;
  JsonDocument d;
  d["on"] = est.on;
  d["mode"] = est.mode;
  d["target"] = autoTarget;
  d["minutes"] = autoMinutes;
  d["repeat"] = irRepeat;
  serializeJson(d, f);
  f.close();
}

static void note(const String &body) {
  if (!noteQueue) return;
  char buf[NOTE_LEN];
  body.substring(0, NOTE_LEN - 1).toCharArray(buf, NOTE_LEN);
  xQueueSend(noteQueue, buf, 0);
}

// volat s drzenym heaterMutex
static void setAlertLocked(const char *kind, const char *title, const String &text) {
  strlcpy(alertKind, kind, sizeof(alertKind));
  strlcpy(alertTitle, title, sizeof(alertTitle));
  strlcpy(alertText, text.c_str(), sizeof(alertText));
}
static void clearAlertLocked() { alertKind[0] = alertTitle[0] = alertText[0] = 0; }

static bool enqueue(uint8_t v) {
  return cmdQueue && xQueueSend(cmdQueue, &v, 0) == pdTRUE;
}

void initHeater() {
  heaterMutex = xSemaphoreCreateMutex();
  cmdQueue = xQueueCreate(10, sizeof(uint8_t));
  noteQueue = xQueueCreate(6, NOTE_LEN);
  irsend.begin();

  const bool powerOn = esp_reset_reason() == ESP_RST_POWERON;
  bool hadAuto = false;
  File f = LittleFS.open("/heater.json", "r");
  if (f) {
    JsonDocument d;
    if (!deserializeJson(d, f)) {
      autoTarget = heaterClampTarget(d["target"] | 21);
      autoMinutes = heaterClampMinutes(d["minutes"] | 60);
      irRepeat = d["repeat"] | 3;
      if (irRepeat > 5) irRepeat = 3;
      // Po vypadku napajeni je topeni (stejny okruh) vypnute. Po jinem restartu
      // (crash/WDT/flash) bezelo dal, tak vezmeme ulozeny odhad.
      if (!powerOn) {
        est.on = d["on"] | false;
        est.mode = d["mode"] | 0;
        if (est.mode > HM_II) est.mode = HM_FAN;
        if (est.on) onSinceMs = millis();   // limit se pocita znovu od restartu
        hadAuto = est.on;
      }
    }
    f.close();
  }
  if (powerOn) {
    setAlertLocked("info", "Restart po výpadku napájení", "Topení považuji za vypnuté, AUTO zrušeno.");
    persist();
  } else if (hadAuto) {
    setAlertLocked("warn", "Restart zařízení", "AUTO zrušeno, topení podle posledního odhadu.");
  }
}

bool heaterSendButton(HeaterBtn b) {
  if (b >= HB_COUNT || autoActive) return false;
  if (lockMutex(heaterMutex, pdMS_TO_TICKS(200))) { clearAlertLocked(); unlockMutex(heaterMutex); }
  return enqueue((uint8_t)b);
}

void heaterSync(bool on, uint8_t mode) {
  if (!heaterMutex) return;
  lockMutex(heaterMutex, pdMS_TO_TICKS(200));
  est.on = on;
  est.mode = mode > HM_II ? HM_FAN : mode;
  onSinceMs = on ? millis() : 0;
  clearAlertLocked();
  persist();
  unlockMutex(heaterMutex);
}

void heaterSetRepeat(uint8_t r) {
  if (r > 5) r = 5;
  if (lockMutex(heaterMutex, pdMS_TO_TICKS(200))) {
    irRepeat = r;
    persist();
    unlockMutex(heaterMutex);
  }
}

void heaterClearAlert() {
  if (lockMutex(heaterMutex, pdMS_TO_TICKS(200))) { clearAlertLocked(); unlockMutex(heaterMutex); }
}

static bool readIndoor(float &temp, uint32_t &updatedAt) {
  bool ok = false;
  if (lockMutex(climateMutex, pdMS_TO_TICKS(200))) {
    ok = climate.indoor.valid && climate.indoor.updatedAtMs != 0 &&
         (millis() - climate.indoor.updatedAtMs) < CLIMATE_STALE_MS;
    temp = climate.indoor.temp;
    updatedAt = climate.indoor.updatedAtMs;
    unlockMutex(climateMutex);
  }
  return ok;
}

bool heaterAutoStart(int target, int minutes) {
  float t; uint32_t u;
  if (!readIndoor(t, u)) return false;
  lockMutex(heaterMutex, pdMS_TO_TICKS(200));
  autoTarget = heaterClampTarget(target);
  autoMinutes = heaterClampMinutes(minutes);
  autoTotalS = (uint32_t)autoMinutes * 60UL;
  autoStartMs = millis();
  autoLastSeen = 0;
  autoPrevTemp = t;
  oh = {false, 0, 0, 0};
  autoActive = true;
  clearAlertLocked();
  persist();
  const bool needMode = est.on && est.mode != HM_II;
  const uint8_t presses = heaterModePresses(est.mode, HM_II);
  unlockMutex(heaterMutex);
  if (needMode) for (uint8_t i = 0; i < presses; i++) enqueue(HB_MODE);
  return true;
}

void heaterAutoUpdate(int target, int minutes) {
  if (!autoActive) return;
  lockMutex(heaterMutex, pdMS_TO_TICKS(200));
  const int nt = heaterClampTarget(target);
  const int nm = heaterClampMinutes(minutes);
  const bool timeChanged = nm != autoMinutes;
  const int oldTarget = autoTarget;
  autoTarget = nt;
  if (timeChanged) {
    // posun zbyvajiciho casu o rozdil (start zustava), zbyva min. 1 minuta
    const uint32_t el = (millis() - autoStartMs) / 1000UL;
    int64_t total = (int64_t)autoTotalS + ((int64_t)nm - autoMinutes) * 60;
    if (total < (int64_t)el + 60) total = (int64_t)el + 60;
    autoTotalS = (uint32_t)total;
    autoMinutes = heaterClampMinutes((int)((autoTotalS + 59) / 60));
  }
  // zmena cile se projevi hned: znovu vyhodnotit posledni mereni (ne pri zasahu proti prehriti, ten pocita mereni)
  if (nt != oldTarget && !oh.active) autoLastSeen = 0;
  persist();
  unlockMutex(heaterMutex);
}

// konec AUTO (volat s drzenym mutexem); vrati true kdyz je treba vypnout topeni
static bool endAutoLocked() {
  autoActive = false;
  oh = {false, 0, 0, 0};
  return est.on;
}

bool heaterAutoActive() { return autoActive; }

void heaterAutoStop() {
  if (!autoActive) return;
  lockMutex(heaterMutex, pdMS_TO_TICKS(200));
  const bool needOff = endAutoLocked();
  unlockMutex(heaterMutex);
  if (needOff) enqueue(HB_POWER);
}

void heaterStatus(JsonDocument &doc) {
  bool on = false; uint8_t mode = 0; uint32_t since = 0;
  bool au = false; int tgt = 0, mins = 0; uint32_t startMs = 0, total = 0;
  bool ohActive = false; uint8_t ohAtt = 0;
  char ak[8] = "", at[40] = "", ax[120] = "";
  if (heaterMutex && lockMutex(heaterMutex, pdMS_TO_TICKS(200))) {
    on = est.on; mode = est.mode; since = onSinceMs;
    au = autoActive; tgt = autoTarget; mins = autoMinutes; startMs = autoStartMs; total = autoTotalS;
    ohActive = oh.active; ohAtt = oh.attempts;
    strlcpy(ak, alertKind, sizeof(ak)); strlcpy(at, alertTitle, sizeof(at)); strlcpy(ax, alertText, sizeof(ax));
    unlockMutex(heaterMutex);
  }
  doc["on"] = on;
  doc["mode"] = mode;
  doc["modeText"] = modeText(mode);
  doc["control"] = au ? "auto" : "manual";
  const uint32_t onS = (on && since) ? (millis() - since) / 1000UL : 0;
  doc["onForS"] = onS;
  doc["manualLeftS"] = (on && !au) ? (onS >= HEATER_MANUAL_LIMIT_S ? 0 : HEATER_MANUAL_LIMIT_S - onS) : 0;
  doc["lastSendOk"] = lastSendOk;
  doc["repeat"] = irRepeat;

  doc["target"] = tgt;
  doc["minutes"] = mins;
  doc["offAt"] = tgt + (int)HEATER_AUTO_HYST;
  float t = 0; uint32_t u = 0;
  if (readIndoor(t, u)) doc["temp"] = t; else doc["temp"] = nullptr;
  if (au) {
    const uint32_t el = (millis() - startMs) / 1000UL;
    doc["autoLeftS"] = el >= total ? 0 : total - el;
    doc["autoTotalS"] = total;
    if (ohActive) doc["overheatAttempt"] = ohAtt;
  }
  if (ak[0]) {
    JsonObject a = doc["alert"].to<JsonObject>();
    a["kind"] = ak; a["title"] = at; a["text"] = ax;
  }
}

static void sendOne(uint8_t v) {
  const HeaterBtn b = (HeaterBtn)(v & 0x7F);
  bcMark(BT_HEATER, BS_HEATER_SEND);
  irsend.sendSymphony(IR_CODES[b], 12, irRepeat);
  lastSendOk = true;
  logLine(String("[HEATER] IR sent: ") + heaterBtnName(b));
  bcMark(BT_HEATER, BS_IDLE);
}

static String fmt1(float t) { return String(t, 1); }

// vyhodnoceni AUTO pri novem mereni
// zasah proti prehrati pocita "mereni" po minutach (jako driv), i kdyz se teplota cte rychleji
#define OH_COUNT_MIN_MS 55000UL
static uint32_t ohCountMs = 0;
static uint32_t autoLogMs = 0;

static void autoEvaluate() {
  float t; uint32_t u;
  const bool ok = readIndoor(t, u);

  lockMutex(heaterMutex, pdMS_TO_TICKS(500));
  if (!autoActive) { unlockMutex(heaterMutex); return; }

  // konec casu
  if ((millis() - autoStartMs) / 1000UL >= autoTotalS) {
    const bool needOff = endAutoLocked();
    unlockMutex(heaterMutex);
    if (needOff) enqueue(HB_POWER);
    return;
  }
  // chyba cidla
  if (!ok) {
    const bool needOff = endAutoLocked();
    setAlertLocked("bad", "Chyba čidla", "AUTO ukončeno, DHT11 neodpovídá. Topení vypnuto.");
    unlockMutex(heaterMutex);
    if (needOff) enqueue(HB_POWER);
    note("Chyba cidla: AUTO ukonceno, topeni vypnuto");
    return;
  }
  if (u == autoLastSeen) { unlockMutex(heaterMutex); return; }   // zadne nove mereni
  autoLastSeen = u;

  const float prev = autoPrevTemp;
  autoPrevTemp = t;
  const int target = autoTarget;

  OhAct oa = OH_NONE;
  if (!oh.active || millis() - ohCountMs >= OH_COUNT_MIN_MS) {
    oa = heaterOverheatStep(oh, t, prev, (float)target, est.on);
    ohCountMs = millis();
  }
  if (oa == OH_PRESS) {
    const uint8_t att = oh.attempts;
    clearAlertLocked();
    setAlertLocked("bad", "Přehřátí", fmt1(t) + " °C při cíli " + target + " °C. Vypínám znovu (pokus " + att + " ze 3).");
    unlockMutex(heaterMutex);
    enqueue(HB_POWER | CMD_FORCE_OFF);
    note(String("Prehrati: ") + fmt1(t) + " C pri cili " + target + " C, vypinam (pokus " + att + "/3)");
    return;
  }
  if (oa == OH_FAIL) {
    endAutoLocked();
    setAlertLocked("bad", "Přehřátí", fmt1(t) + " °C při cíli " + target + " °C. Vypnutí selhalo, AUTO ukončeno.");
    unlockMutex(heaterMutex);
    note(String("PREHRATI: ") + fmt1(t) + " C, vypnuti selhalo 3x, AUTO ukonceno. Zkontroluj topeni!");
    return;
  }
  if (oh.active) { unlockMutex(heaterMutex); return; }   // cekame na vysledek zasahu

  const AutoAct act = heaterAutoDecide(est.on, t, (float)target);
  const bool dbgOn = est.on;
  unlockMutex(heaterMutex);
  // log: pri akci hned, jinak max 1x/min (mereni jsou casta)
  if (act != AA_NONE || autoLogMs == 0 || millis() - autoLogMs >= 60000UL) {
    autoLogMs = millis();
    logLine(String("[HEATER] auto: t=") + fmt1(t) + " cil=" + target + " vypne od " + (target + (int)HEATER_AUTO_HYST) + " odhad=" + (dbgOn ? "ON" : "OFF") + " overheat=" + (oh.active ? "ano" : "ne") + " -> " + (act == AA_ON ? "ZAPNOUT" : act == AA_OFF ? "VYPNOUT" : "nic"));
  }
  if (act == AA_ON) {
    enqueue(HB_POWER); enqueue(HB_MODE); enqueue(HB_MODE);   // Power -> ventilator -> I -> II
  } else if (act == AA_OFF) {
    enqueue(HB_POWER);
  }
}

void heaterTask(void *pv) {
  logLine(F("[TASK][HEATER] started"));
  uint32_t lastCheck = 0;
  for (;;) {
    uint8_t v;
    if (xQueueReceive(cmdQueue, &v, pdMS_TO_TICKS(500)) == pdTRUE) {
      const HeaterBtn b = (HeaterBtn)(v & 0x7F);
      const bool force = (v & CMD_FORCE_OFF) != 0;
      sendOne(v);
      lockMutex(heaterMutex, pdMS_TO_TICKS(500));
      const bool wasOn = est.on;
      if (force) { est.on = false; est.mode = HM_FAN; }
      else heaterApplyButton(est, b);
      if (est.on && !wasOn) onSinceMs = millis();
      if (!est.on) onSinceMs = 0;
      persist();
      unlockMutex(heaterMutex);
      vTaskDelay(pdMS_TO_TICKS(400));   // mezera mezi odeslanimi
    }

    // oznameni (TLS)
    char buf[NOTE_LEN];
    while (xQueueReceive(noteQueue, buf, 0) == pdTRUE) {
      ntfyPostQuiet("Topeni", String(buf), "default", NTFY_CMD_TOPIC);
    }

    if (millis() - lastCheck >= 5000) {
      lastCheck = millis();

      if (uxQueueMessagesWaiting(cmdQueue) == 0) autoEvaluate();

      // limit rucniho rezimu: po 4 h vypnout
      bool over = false;
      if (lockMutex(heaterMutex, pdMS_TO_TICKS(200))) {
        over = !autoActive && est.on && onSinceMs && (millis() - onSinceMs) / 1000UL >= HEATER_MANUAL_LIMIT_S;
        if (over) setAlertLocked("warn", "Limit v manuálu", "Topení bylo 4 hodiny zapnuté, automaticky vypnuto.");
        unlockMutex(heaterMutex);
      }
      if (over) {
        logLine(F("[HEATER] manual limit reached, power off"));
        enqueue(HB_POWER);
        note("Limit manualu (4 h): topeni automaticky vypnuto");
      }
    }
  }
}
