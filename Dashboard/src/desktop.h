#pragma once
#include <Arduino.h>

// Odesle WOL magic packet na WOL_TARGET_MAC a oznaci cas (zrychli health check
// a UI ukaze "probouzim"). Vraci true, pokud se paket odeslal.
bool wakeDesktop();

// Jedna kontrola dostupnosti desktopu (TCP connect), aktualizuje stav `desktop`,
// pri zmene online/offline posle ntfy zpravu. Blokuje max ~2 s.
void desktopPoll();

// true, pokud je od posledniho WOL mene nez DESKTOP_WAKE_WINDOW_MS
bool desktopWakeWindowActive();
