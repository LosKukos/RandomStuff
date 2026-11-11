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

// AUTO: udrzuje teplotu z indoor DHT (zapne pod cil, vypne od cil + 2), po case se vypne
bool heaterAutoStart(int target, int minutes);   // false = chybi platne cidlo
void heaterAutoUpdate(int target, int minutes);  // za behu; zmena casu restartuje odpocet
void heaterAutoStop();                           // zastavi a vypne topeni
void heaterClearAlert();
void heaterSetRepeat(uint8_t r);              // dalsi IR ramce za stisk (0..5, default 3); uklada se
