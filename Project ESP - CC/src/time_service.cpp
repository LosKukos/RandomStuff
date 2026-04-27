#include "time_service.h"
#include "utils.h"
#include <time.h>

static bool timeConfigured = false;

// Europe/Prague:
// CET  = UTC+1
// CEST = UTC+2
// DST starts: last Sunday in March at 02:00
// DST ends:   last Sunday in October at 03:00
static const char* TZ_PRAGUE = "CET-1CEST,M3.5.0/2,M10.5.0/3";

void initTimeService() {
  if (timeConfigured) return;

  // configTzTime sets both NTP servers and timezone/DST rules.
  configTzTime(
    TZ_PRAGUE,
    "pool.ntp.org",
    "time.nist.gov",
    "time.google.com"
  );

  // Keep TZ env set explicitly as well, so localtime_r/strftime use Prague rules.
  setenv("TZ", TZ_PRAGUE, 1);
  tzset();

  timeConfigured = true;
  addLog("[TIME] NTP configured for Europe/Prague");
}

void syncTimeService() {
  initTimeService();

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1500)) {
    addLog("[TIME] NTP synced");
  } else {
    addLog("[TIME] NTP sync pending");
  }
}

bool isTimeSynced() {
  time_t now;
  time(&now);

  // Anything after 2023-01-01 is good enough to say NTP exists.
  return now > 1672531200;
}

TimeSnapshot getTimeSnapshot() {
  TimeSnapshot snap;
  snap.uptimeMs = millis();
  snap.synced = isTimeSynced();

  time_t now;
  time(&now);
  snap.epoch = now;

  if (!snap.synced) {
    snap.label = "uptime " + String(snap.uptimeMs / 1000) + "s";
    snap.iso = "";
    return snap;
  }

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char labelBuf[16];
  strftime(labelBuf, sizeof(labelBuf), "%H:%M", &timeinfo);
  snap.label = String(labelBuf);

  char isoBuf[32];
  strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%S%z", &timeinfo);
  snap.iso = String(isoBuf);

  return snap;
}
