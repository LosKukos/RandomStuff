#pragma once
#include <Arduino.h>

// WMO weather code -> icon-key (pro frontend) a CZ popis
String wmoToIcon(int code);
String wmoToCondition(int code);

// fetch z Open-Meteo (bez API klice), naplni state.h `weather`
// pod weatherMutex. Vraci true pri uspechu.
bool pollWeather();
