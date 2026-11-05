#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// =====================================================
// RTC BREADCRUMBS
// =====================================================
// Male zaznamy v RTC_NOINIT pameti: prezijou watchdog / panic / software
// reset (ne vypadek napajeni). Po restartu z nich lze zjistit, co ESP
// delalo tesne pred padem a v jakem stavu byl heap a stacky.
//
//  - bcMark()       ring poslednich kroku + "posledni krok" kazdeho tasku
//  - bcHeartbeat()  snapshot (uptime, heap, min heap, stacky) kazdych 10 s
//  - bcHourlySample() hodinovy vzorek heapu (trend / uniky za ~24 h)
//
// Po restartu: bcPrintPrev() (Serial), bcPrevSummary() (ntfy report),
// GET /api/crash (JSON).

// Logicke "tasky" (poradi 0-6 odpovida poli ve getTaskStackFree(), 7 = loop)
enum BcTask : uint8_t {
  BT_WLED = 0, BT_ESP, BT_AUTO, BT_EMERG, BT_WEATHER, BT_CLIMATE, BT_NTFYCMD,
  BT_LOOP,     // Arduino loop()
  BT_WEB,      // async_tcp handlery
  BT_SETUP,
  BT_COUNT
};

enum BcStep : uint8_t {
  BS_IDLE = 0, BS_START, BS_WLED_POLL, BS_ESP_POLL_MC, BS_ESP_POLL_PANIC,
  BS_BROADCAST, BS_AUTO_LOGIC, BS_DISCORD, BS_EMERG_CHANGE, BS_NTFY_PANIC,
  BS_WEATHER_POLL, BS_CLIMATE_READ, BS_NTFY_REPORT, BS_NTFYCMD_POLL,
  BS_DESKTOP_POLL, BS_BUTTONS, BS_HEARTBEAT,
  BS_H_PANIC, BS_H_SEND, BS_H_TIME, BS_H_WLED,
  BS_SETUP_WIFI, BS_SETUP_NTP, BS_SETUP_DONE,
  BS_COUNT
};

// Po abnormalnim restartu se souhrn pridava do hodinovych reportu
// prvnich BC_REPORT_PREV_SECS sekund behu.
#define BC_REPORT_PREV_SECS (3UL * 3600UL)

void     bcInit();                       // volat hned po initMutexes() v setup()
uint32_t bcBootCount();                  // pocet startu (kazdy reset krome power-on pridava 1)

void bcMark(BcTask task, BcStep step);   // lock-free, bezpecne z kazdeho tasku
void bcHeartbeat();                      // volat z loop() (vyuziva i stack loop tasku)
void bcHourlySample();                   // volat jednou za hodinu

// predchozi beh
bool   bcPrevAbnormal();                 // PANIC / WDT / BROWNOUT a platna data
String bcPrevSummary();                  // vice radkovy text (prazdny kdyz neni co hlasit)
void   bcPrintPrev();                    // vypise souhrn na Serial (jen pokud jsou data)

// aktualni beh
String bcHeapLine();                     // "Heap: volny X kB, min Y kB, max blok Z kB"
uint32_t bcMinHeap();
uint32_t bcMaxBlock();

// {"prev": {...}, "now": {...}} pro GET /api/crash
void bcFillJson(JsonObject root);
