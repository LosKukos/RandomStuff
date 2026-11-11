#pragma once
// Cista logika odhadu stavu pritmotopu (bez HW, testovatelne na PC).
// Ovladac je jednosmerny IR: Power je toggle, Mode cykli Ventilator -> I -> II -> Ventilator.
#include <stdint.h>

enum HeaterBtn : uint8_t {
  HB_POWER = 0, HB_MODE, HB_SPIN, HB_CLOCK, HB_ECO, HB_MINUS, HB_PLUS, HB_COUNT
};

enum HeaterMode : uint8_t { HM_FAN = 0, HM_I = 1, HM_II = 2 };

struct HeaterEst {
  bool    on;
  uint8_t mode;   // HeaterMode, platne jen kdyz on
};

// Po Power vzdy zacina Ventilator. Mode pri vypnutem topeni nic nedela.
static inline void heaterApplyButton(HeaterEst &s, HeaterBtn b) {
  if (b == HB_POWER) {
    s.on = !s.on;
    s.mode = HM_FAN;
  } else if (b == HB_MODE && s.on) {
    s.mode = (uint8_t)((s.mode + 1) % 3);
  }
}

// Kolik stisku Mode je potreba z aktualniho modu na cilovy (0..2).
static inline uint8_t heaterModePresses(uint8_t from, uint8_t to) {
  return (uint8_t)((to + 3 - from) % 3);
}

static inline const char *heaterBtnName(HeaterBtn b) {
  static const char *const n[HB_COUNT] = {"power", "mode", "spin", "clock", "eco", "minus", "plus"};
  return b < HB_COUNT ? n[b] : "?";
}

static inline bool heaterBtnFromName(const char *s, HeaterBtn &out) {
  for (int i = 0; i < HB_COUNT; i++) {
    const char *n = heaterBtnName((HeaterBtn)i);
    int k = 0;
    while (n[k] && s[k] == n[k]) k++;
    if (!n[k] && !s[k]) { out = (HeaterBtn)i; return true; }
  }
  return false;
}

// ---------------- AUTO ----------------
#define HEATER_AUTO_HYST        2.0f   // vypne pri cil + 2
#define HEATER_AUTO_OVERHEAT    4.0f   // zasah pri cil + 4
#define HEATER_AUTO_MIN_TARGET  15
#define HEATER_AUTO_MAX_TARGET  35
#define HEATER_AUTO_STEP_MIN    15
#define HEATER_AUTO_MAX_MIN     480

enum AutoAct : uint8_t { AA_NONE = 0, AA_ON, AA_OFF };

static inline AutoAct heaterAutoDecide(bool on, float temp, float target) {
  if (!on && temp < target) return AA_ON;
  if (on && temp >= target + HEATER_AUTO_HYST) return AA_OFF;
  return AA_NONE;
}

struct HeaterOverheat {
  bool    active;
  uint8_t attempts;   // 1..3
  float   base;       // teplota pri poslednim pokusu
  uint8_t readings;   // nova mereni od posledniho pokusu
};

enum OhAct : uint8_t { OH_NONE = 0, OH_PRESS, OH_FAIL };

// Volat pro kazde nove mereni. OH_PRESS = stisknout Power (a povazovat topeni za vypnute),
// OH_FAIL = 3 pokusy bez uspechu.
static inline OhAct heaterOverheatStep(HeaterOverheat &o, float temp, float prev, float target, bool estOn) {
  if (!o.active) {
    const bool trigger = temp >= target + HEATER_AUTO_OVERHEAT && (estOn || temp > prev);
    if (!trigger) return OH_NONE;
    o.active = true; o.attempts = 1; o.base = temp; o.readings = 0;
    return OH_PRESS;
  }
  o.readings++;
  if (temp < o.base) { o.active = false; o.attempts = 0; return OH_NONE; }
  if (o.readings >= 3) {
    if (temp <= o.base) { o.active = false; o.attempts = 0; return OH_NONE; }
    if (o.attempts >= 3) { o.active = false; return OH_FAIL; }
    o.attempts++; o.base = temp; o.readings = 0;
    return OH_PRESS;
  }
  return OH_NONE;
}

static inline int heaterClampTarget(int t) {
  return t < HEATER_AUTO_MIN_TARGET ? HEATER_AUTO_MIN_TARGET : t > HEATER_AUTO_MAX_TARGET ? HEATER_AUTO_MAX_TARGET : t;
}
static inline int heaterClampMinutes(int m) {
  m = (m / HEATER_AUTO_STEP_MIN) * HEATER_AUTO_STEP_MIN;
  return m < HEATER_AUTO_STEP_MIN ? HEATER_AUTO_STEP_MIN : m > HEATER_AUTO_MAX_MIN ? HEATER_AUTO_MAX_MIN : m;
}
