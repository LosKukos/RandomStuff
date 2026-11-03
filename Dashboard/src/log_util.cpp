#include "log_util.h"
#include "state.h"

void logLine(const String &msg) {
  if (lockMutex(serialMutex)) {
    Serial.println(msg);
    Serial.flush();
    unlockMutex(serialMutex);
  }
}

void logValue(const String &prefix, const String &value) {
  if (lockMutex(serialMutex)) {
    Serial.print(prefix);
    Serial.println(value);
    Serial.flush();
    unlockMutex(serialMutex);
  }
}

void logValue(const String &prefix, int value) {
  if (lockMutex(serialMutex)) {
    Serial.print(prefix);
    Serial.println(value);
    Serial.flush();
    unlockMutex(serialMutex);
  }
}

void logValue(const String &prefix, unsigned long value) {
  if (lockMutex(serialMutex)) {
    Serial.print(prefix);
    Serial.println(value);
    Serial.flush();
    unlockMutex(serialMutex);
  }
}

void printDivider() {
  logLine(F("--------------------------------------------------"));
}
