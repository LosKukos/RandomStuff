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
