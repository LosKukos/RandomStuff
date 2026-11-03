#pragma once
#include <Arduino.h>

// =====================================================
// TIME / SVATKY / STATE PERSISTENCE
// =====================================================

// vypise aktualni cas do logu (pokud je timeReady)
void printTimeInfo();

// nacte jmeniny pro dany klic (MM-DD) z /names.json
String getTodayNames(const String &key);

// aktualizuje timeinfo/currentDateKey/currentFullDate/currentNames
// z NTP/RTC casu; vraci false pokud getLocalTime selhalo
bool updateLocalTime();

// nacte/ulozi posledni odeslane datum do /state.json
String loadLastSentFile();
void   saveLastSentFile(const String &date);

// true, pokud je v okne pro automaticke odeslani a jeste se dnes neposilalo
bool shouldAutoSend();

// pripoji NTP a ceka na prvni platny cas (blokujici, max ~10s)
bool setupTime();

// rucni/web odeslani "Dnes ma svatek: ..." - aktualizuje cas,
// posle na Discord, ulozi stav. Vraci true pri uspechu.
bool doManualSend();
