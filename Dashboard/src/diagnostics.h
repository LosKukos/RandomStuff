#pragma once
#include "state.h"

// vypise kompletni diagnosticky snapshot do serial logu
void printDiagnostics();

// vstupni bod pro DIAG tlacitko: vypise diagnostiku a obnovi UI
void doDiagnostics();

// periodicky "jsem zivy" log
void heartbeat();

void printDeviceDiag(const DeviceState &device);

// duvod posledniho restartu jako citelny ASCII text (poweron, brownout, task_wdt, panic...)
String resetReasonStr();
