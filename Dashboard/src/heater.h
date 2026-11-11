#pragma once
#include <ArduinoJson.h>
#include "heater_logic.h"

// IR ovladani elektrickeho pritmotopu (IR LED na PIN_IR_TX, SYMPHONY 12 bit).
// Stav je jen ODHAD (ovladac je jednosmerny), persistovany v /heater.json.

void initHeater();                       // zavolat pred startBackgroundTasks()
void heaterTask(void *pv);               // vlastni task (fronta prikazu + hlidani limitu)

bool heaterSendButton(HeaterBtn b);      // zaradi odeslani do fronty (neblokuje), false = plna fronta
void heaterSync(bool on, uint8_t mode);  // jen opravi odhad, nic neposila
void heaterStatus(JsonDocument &doc);    // JSON pro GET /api/heater
