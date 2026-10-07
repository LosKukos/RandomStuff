#pragma once

#include <Arduino.h>

struct TimeSnapshot {
  bool synced = false;
  uint32_t uptimeMs = 0;
  time_t epoch = 0;
  String iso;
  String label;
};

void initTimeService();
void syncTimeService();
bool isTimeSynced();
uint32_t nowStamp();   // epoch (s), pokud je cas synchronizovany; jinak millis()
TimeSnapshot getTimeSnapshot();
