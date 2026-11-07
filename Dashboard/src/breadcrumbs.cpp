#include "breadcrumbs.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "log_util.h"
#include "diagnostics.h"
#include "tasks.h"

// =====================================================
// RTC LAYOUT
// =====================================================

#define BC_MAGIC  0xBC0DE003u
#define BC_RING   48
#define BC_HOURS  24
#define BC_STACKS 8            // 7 tasku + loop

struct BcEntry {
  uint32_t ms;                 // millis() v dobe zapisu
  uint8_t  task;
  uint8_t  step;
  uint8_t  core;
  uint8_t  pad;
};

struct BcHourly {
  uint32_t upS;
  uint32_t freeInt;
  uint32_t minInt;
  uint32_t maxBlk;
};

struct BcRtc {
  uint32_t magic;
  uint32_t boots;
  uint32_t head;               // celkovy pocet zapsanych kroku (index = head % BC_RING)
  BcEntry  ring[BC_RING];
  BcEntry  last[BT_COUNT];     // posledni krok kazdeho tasku

  // snapshot z posledniho heartbeatu
  uint32_t hbSeq;
  uint32_t hbUpMs;
  uint32_t hbFree;
  uint32_t hbMin;
  uint32_t hbMaxBlk;
  uint32_t hbPsram;
  int32_t  hbRssi;
  uint16_t hbStack[BC_STACKS];

  uint8_t  hourN;
  BcHourly hourly[BC_HOURS];
};

static RTC_NOINIT_ATTR BcRtc g_rtc;

// citac poradi zapisu je v obycejne RAM (atomicke instrukce nad RTC pameti nejsou spolehlive),
// do RTC se jen zapisuje vysledek
static uint32_t s_seq = 0;

// kopie dat z predchoziho behu (obycejna RAM)
static struct {
  bool              valid;
  esp_reset_reason_t reason;
  BcRtc             d;
} prev;

static const char *const TASK_NAMES[BT_COUNT] = {
  "wled", "esp", "auto", "emerg", "weather", "climate", "ntfycmd", "loop", "web", "setup"
};

static const char *const STEP_NAMES[BS_COUNT] = {
  "idle", "start", "wled_poll", "esp_poll_mc", "esp_poll_panic",
  "broadcast", "auto_logic", "discord", "emerg_change", "ntfy_panic",
  "weather_poll", "climate_read", "ntfy_report", "ntfycmd_poll",
  "desktop_poll", "buttons", "heartbeat",
  "h_panic", "h_send", "h_time", "h_wled",
  "setup_wifi", "setup_ntp", "setup_done",
  "dht_in", "dht_out"
};

static_assert(sizeof(TASK_NAMES) / sizeof(TASK_NAMES[0]) == BT_COUNT, "TASK_NAMES");
static_assert(sizeof(STEP_NAMES) / sizeof(STEP_NAMES[0]) == BS_COUNT, "STEP_NAMES");

static const char *taskName(uint8_t t) { return t < BT_COUNT ? TASK_NAMES[t] : "?"; }
static const char *stepName(uint8_t s) { return s < BS_COUNT ? STEP_NAMES[s] : "?"; }

// =====================================================
// HEAP HELPERS (internal RAM)
// =====================================================

static const uint32_t HEAP_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

static uint32_t heapFree()  { return heap_caps_get_free_size(HEAP_CAPS); }
static uint32_t heapMin()   { return heap_caps_get_minimum_free_size(HEAP_CAPS); }
static uint32_t heapBlock() { return heap_caps_get_largest_free_block(HEAP_CAPS); }

uint32_t bcMinHeap()   { return heapMin(); }
uint32_t bcMaxBlock()  { return heapBlock(); }

static String kB(uint32_t bytes) { return String((bytes + 512) / 1024) + " kB"; }

String bcHeapLine() {
  return "Heap: volny " + kB(heapFree()) + ", min " + kB(heapMin()) + ", max blok " + kB(heapBlock());
}

// =====================================================
// INIT
// =====================================================

void bcInit() {
  const esp_reset_reason_t reason = esp_reset_reason();

  // RTC_NOINIT po zapnuti napajeni obsahuje nahodna data
  const bool valid = (reason != ESP_RST_POWERON) && (g_rtc.magic == BC_MAGIC);

  prev.reason = reason;
  prev.valid  = valid;
  uint32_t boots = 0;
  if (valid) {
    memcpy(&prev.d, &g_rtc, sizeof(BcRtc));
    boots = g_rtc.boots;
  } else {
    memset(&prev.d, 0, sizeof(BcRtc));
  }

  memset(&g_rtc, 0, sizeof(BcRtc));
  s_seq = 0;
  g_rtc.magic = BC_MAGIC;
  g_rtc.boots = boots + 1;
}

uint32_t bcBootCount() { return g_rtc.boots; }

// =====================================================
// RUNTIME
// =====================================================

void bcMark(BcTask task, BcStep step) {
  const uint32_t n = __atomic_fetch_add(&s_seq, 1u, __ATOMIC_RELAXED);
  const uint32_t i = n % BC_RING;
  BcEntry e;
  e.ms   = millis();
  e.task = (uint8_t)task;
  e.step = (uint8_t)step;
  e.core = (uint8_t)xPortGetCoreID();
  e.pad  = 0;
  g_rtc.ring[i] = e;
  if (task < BT_COUNT) g_rtc.last[task] = e;
  if (g_rtc.head < n + 1) g_rtc.head = n + 1;   // benigni race mezi tasky, head jen neklesa
}

void bcHeartbeat() {
  bcMark(BT_LOOP, BS_HEARTBEAT);

  uint16_t st[BC_STACKS - 1];
  getTaskStackFree(st);
  for (int i = 0; i < BC_STACKS - 1; i++) g_rtc.hbStack[i] = st[i];
  const UBaseType_t loopFree = uxTaskGetStackHighWaterMark(NULL);   // volany z loop() = stack loop tasku
  g_rtc.hbStack[BC_STACKS - 1] = loopFree > 65535 ? 65535 : (uint16_t)loopFree;

  g_rtc.hbFree   = heapFree();
  g_rtc.hbMin    = heapMin();
  g_rtc.hbMaxBlk = heapBlock();
  g_rtc.hbPsram  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  g_rtc.hbRssi   = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  g_rtc.hbUpMs   = millis();
  g_rtc.hbSeq++;
}

void bcHourlySample() {
  uint8_t n = g_rtc.hourN;
  if (n >= BC_HOURS) {
    memmove(&g_rtc.hourly[0], &g_rtc.hourly[1], sizeof(BcHourly) * (BC_HOURS - 1));
    n = BC_HOURS - 1;
  }
  BcHourly h;
  h.upS     = millis() / 1000;
  h.freeInt = heapFree();
  h.minInt  = heapMin();
  h.maxBlk  = heapBlock();
  g_rtc.hourly[n] = h;
  g_rtc.hourN = n + 1;
}

// =====================================================
// PREVIOUS RUN
// =====================================================

bool bcPrevAbnormal() {
  if (!prev.valid) return false;
  switch (prev.reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
      return true;
    default:
      return false;
  }
}

// zaznamy z ringu v chronologickem poradi (nejstarsi prvni); vraci pocet
static int prevOrdered(BcEntry *out, int maxOut) {
  const uint32_t head = prev.d.head;
  const int total = head < BC_RING ? (int)head : BC_RING;
  const int start = head < BC_RING ? 0 : (int)(head % BC_RING);
  int n = 0;
  for (int k = 0; k < total && n < maxOut; k++) {
    out[n++] = prev.d.ring[(start + k) % BC_RING];
  }
  return n;
}

// referencni cas pro "-x.xs": nejnovejsi krok nebo heartbeat, co je pozdeji
static uint32_t prevRefMs(const BcEntry *ord, int n) {
  uint32_t ref = prev.d.hbUpMs;
  if (n > 0 && ord[n - 1].ms > ref) ref = ord[n - 1].ms;
  return ref;
}

static String ageStr(uint32_t ref, uint32_t ms) {
  const uint32_t d = ref >= ms ? ref - ms : 0;
  return "-" + String(d / 1000.0f, 1) + "s";
}

String bcPrevSummary() {
  if (!prev.valid) return "";

  const BcRtc &d = prev.d;
  String s;

  s += "Posledni beh skoncil: " + resetReasonStr() + "\n";

  if (d.hbSeq == 0) {
    s += "Pad pred prvnim heartbeatem (< 10 s behu)\n";
  } else {
    const uint32_t up = d.hbUpMs / 1000;
    s += "Uptime pred restartem: " + String(up / 3600) + " h " + String((up % 3600) / 60) + " min\n";
    s += "Heap: volny " + kB(d.hbFree) + ", min " + kB(d.hbMin) + ", max blok " + kB(d.hbMaxBlk) + "\n";
    s += "Stack volny (B):";
    for (int i = 0; i < BC_STACKS; i++) {
      s += String(i ? "," : "") + " " + taskName(i) + " " + String(d.hbStack[i]);
    }
    s += "\n";
  }

  // posledni krok kazdeho tasku
  BcEntry ord[BC_RING];
  const int n = prevOrdered(ord, BC_RING);
  const uint32_t ref = prevRefMs(ord, n);

  s += "Posledni krok tasku:";
  bool any = false;
  for (int t = 0; t < BT_COUNT; t++) {
    const BcEntry &e = d.last[t];
    if (e.ms == 0 && e.step == 0) continue;
    s += String(any ? "," : "") + " " + taskName(t) + ":" + stepName(e.step) + "(" + ageStr(ref, e.ms) + ")";
    any = true;
  }
  if (!any) s += " -";
  s += "\n";

  // poslednich par kroku v poradi
  s += "Posledni kroky:";
  const int from = n > 8 ? n - 8 : 0;
  for (int k = from; k < n; k++) {
    s += String(k > from ? "," : "") + " " + taskName(ord[k].task) + ":" + stepName(ord[k].step) +
         "(" + ageStr(ref, ord[k].ms) + ")";
  }
  if (n == 0) s += " -";
  s += "\n";

  // trend heapu po hodinach (poslednich max 12)
  if (d.hourN > 0) {
    const int cnt = d.hourN > BC_HOURS ? BC_HOURS : d.hourN;
    const int from2 = cnt > 12 ? cnt - 12 : 0;
    s += "Heap po hodinach (volny/min kB):";
    for (int i = from2; i < cnt; i++) {
      s += " " + String(d.hourly[i].freeInt / 1024) + "/" + String(d.hourly[i].minInt / 1024);
    }
    s += "\n";
  }

  s.trim();
  return s;
}

void bcPrintPrev() {
  if (!prev.valid) {
    logLine(F("[BC] zadna data z predchoziho behu (power-on nebo neplatna RTC pamet)"));
    return;
  }
  logLine(String("[BC] predchozi beh, boot #") + String((unsigned long)g_rtc.boots));
  logLine(bcPrevSummary());
}

// =====================================================
// JSON (GET /api/crash)
// =====================================================

static void putEntry(JsonObject o, const BcEntry &e, uint32_t ref) {
  o["task"]  = taskName(e.task);
  o["step"]  = stepName(e.step);
  o["ms"]    = e.ms;
  o["ageMs"] = ref >= e.ms ? ref - e.ms : 0;
  o["core"]  = e.core;
}

static void putStacks(JsonObject o, const uint16_t *st) {
  for (int i = 0; i < BC_STACKS; i++) o[taskName(i)] = st[i];
}

void bcFillJson(JsonObject root) {
  // ---- predchozi beh ----
  JsonObject p = root["prev"].to<JsonObject>();
  p["valid"] = prev.valid;
  if (prev.valid) {
    const BcRtc &d = prev.d;
    p["abnormal"] = bcPrevAbnormal();
    p["reason"]   = resetReasonStr();
    p["hbSeq"]    = d.hbSeq;
    p["upMs"]     = d.hbUpMs;
    p["free"]     = d.hbFree;
    p["min"]      = d.hbMin;
    p["maxBlock"] = d.hbMaxBlk;
    p["psramFree"] = d.hbPsram;
    p["rssi"]     = d.hbRssi;
    putStacks(p["stackFree"].to<JsonObject>(), d.hbStack);

    BcEntry ord[BC_RING];
    const int n = prevOrdered(ord, BC_RING);
    const uint32_t ref = prevRefMs(ord, n);

    JsonObject last = p["last"].to<JsonObject>();
    for (int t = 0; t < BT_COUNT; t++) {
      if (d.last[t].ms == 0 && d.last[t].step == 0) continue;
      putEntry(last[taskName(t)].to<JsonObject>(), d.last[t], ref);
    }

    JsonArray ring = p["ring"].to<JsonArray>();
    for (int k = 0; k < n; k++) putEntry(ring.add<JsonObject>(), ord[k], ref);

    JsonArray hourly = p["hourly"].to<JsonArray>();
    const int cnt = d.hourN > BC_HOURS ? BC_HOURS : d.hourN;
    for (int i = 0; i < cnt; i++) {
      JsonObject h = hourly.add<JsonObject>();
      h["upS"]  = d.hourly[i].upS;
      h["free"] = d.hourly[i].freeInt;
      h["min"]  = d.hourly[i].minInt;
      h["maxBlock"] = d.hourly[i].maxBlk;
    }
  }

  // ---- aktualni beh ----
  JsonObject now = root["now"].to<JsonObject>();
  now["boots"]    = g_rtc.boots;
  now["upMs"]     = millis();
  now["free"]     = heapFree();
  now["min"]      = heapMin();
  now["maxBlock"] = heapBlock();
  now["psramFree"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  now["hbSeq"]    = g_rtc.hbSeq;
  putStacks(now["stackFreeAtHeartbeat"].to<JsonObject>(), g_rtc.hbStack);

  JsonArray hourly = now["hourly"].to<JsonArray>();
  const int cnt = g_rtc.hourN > BC_HOURS ? BC_HOURS : g_rtc.hourN;
  for (int i = 0; i < cnt; i++) {
    JsonObject h = hourly.add<JsonObject>();
    h["upS"]  = g_rtc.hourly[i].upS;
    h["free"] = g_rtc.hourly[i].freeInt;
    h["min"]  = g_rtc.hourly[i].minInt;
    h["maxBlock"] = g_rtc.hourly[i].maxBlk;
  }
}
