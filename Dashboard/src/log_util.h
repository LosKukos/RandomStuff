#pragma once
#include <Arduino.h>

// =====================================================
// SERIAL HELPERS (mutex-protected logging)
// =====================================================

void logLine(const String &msg);
void logValue(const String &prefix, const String &value);
void logValue(const String &prefix, int value);
void logValue(const String &prefix, unsigned long value);
void printDivider();
