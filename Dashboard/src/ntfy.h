#pragma once
#include <Arduino.h>

// posle SOS notifikaci na ntfy.sh. repeatCount==0 -> prvni odeslani,
// jinak "stale aktivni (Nx)" zprava
// hodinovy report (teploty, pocasi) na ntfy topic NTFY_REPORT_TOPIC
// Vraci HTTP kod (200 = ok), nebo zaporny kod: -100 = WiFi neni pripojena,
// -101 = http.begin selhal, jine zaporne = chyba HTTPClient (spojeni/TLS/timeout).
int sendNtfyReport(const String &title, const String &body, const char *priority = "min");

// POST na topic bez zapisu do stavu reportu (potvrzeni WOL apod.)
void ntfyPostQuiet(const String &title, const String &body, const char *priority = "min");

// posledni vysledek reportu (pro /api/status) - 0 = jeste neodeslano
int      ntfyReportLastCode();
uint32_t ntfyReportLastAtMs();

void sendNtfyPanic(const String &deviceName, const String &eventId, int repeatCount);
