#include "ntfy.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "log_util.h"
#include "config.h"

void sendNtfyPanic(const String &deviceName, const String &eventId, int repeatCount) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  if (!http.begin(client, "https://ntfy.sh/mecerodovi_panic_button")) return;

  http.addHeader("Title",    "SOS - " + deviceName);
  http.addHeader("Priority", "urgent");
  http.addHeader("Tags",     "rotating_light,sos");

  String body;
  if (repeatCount == 0) {
    body = "SOS bylo stisknuto!\n"
           "Zarizeni: " + deviceName + "\n"
           "Event: " + eventId;
  } else {
    body = "SOS stale aktivni (" + String(repeatCount) + "x)\n"
           "Zarizeni: " + deviceName + "\n"
           "Event: " + eventId;
  }

  const int code = http.POST(body);
  logValue(F("[NTFY] POST code: "), code);
  http.end();
}

// jednoduchy POST na topic bez zapisu do stavu reportu (potvrzeni WOL apod.)
void ntfyPostQuiet(const String &title, const String &body, const char *priority, const char *topic) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  http.setTimeout(8000);
  if (!http.begin(client, String("https://ntfy.sh/") + (topic ? topic : NTFY_REPORT_TOPIC))) return;
  http.addHeader("Title",    title);
  http.addHeader("Priority", priority);
  http.POST(body);
  http.end();
}

static volatile int      lastReportCode = 0;
static volatile uint32_t lastReportAtMs = 0;

int ntfyReportLastCode()     { return lastReportCode; }
uint32_t ntfyReportLastAtMs() { return lastReportAtMs; }

int sendNtfyReport(const String &title, const String &body, const char *priority) {
  int code;

  if (WiFi.status() != WL_CONNECTED) {
    code = -100;
    logLine(F("[NTFY] report: WiFi not connected"));
  } else {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    http.setTimeout(8000);

    if (!http.begin(client, String("https://ntfy.sh/") + NTFY_REPORT_TOPIC)) {
      code = -101;
      logLine(F("[NTFY] report: http.begin failed"));
    } else {
      http.addHeader("Title",    title);   // jen ASCII (HTTP hlavicka)
      http.addHeader("Priority", priority);   // "min" = tichy report bez zvuku/vibrace
      http.addHeader("Tags",     "thermometer");
      code = http.POST(body);
      if (code != 200) logValue(F("[NTFY] report FAILED, code: "), code);
      else             logLine(F("[NTFY] report sent (200)"));
      http.end();
    }
  }

  lastReportCode = code;
  lastReportAtMs = millis();
  return code;
}
