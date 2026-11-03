#pragma once
#include <Arduino.h>

// escapuje string pro vlozeni do JSON literalu
String jsonEscape(const String &input);

// posle zpravu na Discord webhook s mention role; vraci true pri 2xx
bool sendMessage(const String &msg);
