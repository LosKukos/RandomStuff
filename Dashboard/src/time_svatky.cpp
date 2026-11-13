#include "time_svatky.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "state.h"
#include "log_util.h"
#include "led.h"
#include "discord.h"

void printTimeInfo() {
  if (!getTimeReady()) { logLine(F("[TIME] not ready")); return; }
  char buf[32];
  if (lockMutex(stateMutex)) {
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    unlockMutex(stateMutex);
    logValue(F("[TIME] "), String(buf));
  }
}

String getTodayNames(const String &key) {
  File f = LittleFS.open("/names.json", "r");
  if (!f) {
    logLine(F("[NAMES] failed to open /names.json"));
    setNeedsUserAttention(true);
    return "FS error";
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    logValue(F("[NAMES] parse fail: "), String(err.c_str()));
    setNeedsUserAttention(true);
    return "JSON error";
  }

  JsonArray arr = doc[key].as<JsonArray>();
  if (arr.isNull()) {
    logValue(F("[NAMES] no entry for key "), key);
    return "Nikdo";
  }

  String result;
  for (JsonVariant v : arr) {
    if (!result.isEmpty()) result += ", ";
    result += v.as<String>();
  }
  return result;
}

bool updateLocalTime() {
  struct tm localCopy;
  if (!getLocalTime(&localCopy)) {
    logLine(F("[TIME] getLocalTime failed"));
    setNeedsUserAttention(true);
    return false;
  }

  char dateKey[6];
  char fullDate[11];

  if (lockMutex(stateMutex)) {
    timeinfo = localCopy;
    strftime(dateKey,  sizeof(dateKey),  "%m-%d",      &timeinfo);
    strftime(fullDate, sizeof(fullDate), "%Y-%m-%d",   &timeinfo);
    unlockMutex(stateMutex);
  }

  // settery si berou stateMutex sami -> volat mimo predchozi lock
  setCurrentDateKey(String(dateKey));
  setCurrentFullDate(String(fullDate));
  setCurrentNames(getTodayNames(getCurrentDateKey()));
  return true;
}

String loadLastSentFile() {
  if (!LittleFS.exists("/state.json")) { logLine(F("[STATE] /state.json not found")); return ""; }
  File f = LittleFS.open("/state.json", "r");
  if (!f) { logLine(F("[STATE] /state.json nejde otevrit")); return ""; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { logValue(F("[STATE] parse fail: "), String(err.c_str())); return ""; }
  return doc["last_sent"] | "";
}

void saveLastSentFile(const String &date) {
  JsonDocument doc;
  doc["last_sent"] = date;
  File f = LittleFS.open("/state.json", "w");
  if (!f) { logLine(F("[STATE] failed to open for write")); setNeedsUserAttention(true); return; }
  serializeJson(doc, f);
  f.close();
  logValue(F("[STATE] saved last_sent="), date);
}

bool shouldAutoSend() {
  bool inWindow = false;
  if (lockMutex(stateMutex)) {
    inWindow = (
      timeinfo.tm_hour == AUTO_SEND_HOUR &&
      timeinfo.tm_min  >= AUTO_SEND_START_MIN &&
      timeinfo.tm_min  <  AUTO_SEND_WINDOW_MIN
    );
    unlockMutex(stateMutex);
  }
  // getLastSent()/getCurrentFullDate() si berou stateMutex sami,
  // proto musi byt mimo predchozi lock blok (mutex neni rekurzivni)
  return inWindow && (getLastSent() != getCurrentFullDate());
}

bool setupTime() {
  if (WiFi.status() != WL_CONNECTED) { logLine(F("[TIME] WiFi not connected")); return false; }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();
  logLine(F("[TIME] waiting for NTP..."));

  for (int i = 0; i < 20; i++) {
    struct tm tmp;
    if (getLocalTime(&tmp)) {
      if (lockMutex(stateMutex)) { timeinfo = tmp; unlockMutex(stateMutex); }
      setTimeReady(true);
      updateLocalTime();
      logLine(F("[TIME] NTP ready"));
      printTimeInfo();
      return true;
    }
    logValue(F("[TIME] NTP retry "), i + 1);
    delay(500);
  }

  logLine(F("[TIME] NTP failed"));
  setTimeReady(false);
  setNeedsUserAttention(true);
  return false;
}

bool doManualSend() {
  setNeedsUserAttention(true);
  updateLedFromState();
  logLine(F("[SEND] manual/web send requested"));

  if (!getTimeReady()) {
    if (!updateLocalTime()) {
      logLine(F("[SEND] failed, time not ready"));
      setLastSendOk(false);
      setNeedsUserAttention(true);
      updateLedFromState();
      return false;
    }
    setTimeReady(true);
  } else {
    updateLocalTime();
  }

  String msg = "Dnes ma svatek: " + getCurrentNames();
  bool ok = sendMessage(msg);
  setLastSendOk(ok);

  if (ok) {
    saveLastSentFile(getCurrentFullDate());
    setLastSent(getCurrentFullDate());
    setNeedsUserAttention(false);
    logLine(F("[SEND] success"));
  } else {
    setNeedsUserAttention(true);
    logLine(F("[SEND] failed"));
  }

  updateLedFromState();
  return ok;
}
