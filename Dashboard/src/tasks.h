#pragma once
#include <Arduino.h>

// =====================================================
// ISR / BUTTON FLAGS
// =====================================================

void IRAM_ATTR isrSend();
void IRAM_ATTR isrDiag();

bool fetchAndClearSendRequested();
bool fetchAndClearDiagRequested();

// =====================================================
// RTOS TASKS
// =====================================================

// vytvori vsechny staticke tasky (wled poll, esp poll, auto-svatky,
// emergency). Vola se jednou ze setup() po startDashboardServer().
void startBackgroundTasks();

// neblokujici "refresh vseho": vzbudi poll tasky (wled, esp, pocasi, DHT).
// Bezpecne volat z async_tcp handleru.
void requestRefreshAll();

// neblokujici: posle hodinovy report na ntfy hned (test), vysledek je v /api/status
void requestNtfyTest();
