#include "ntfy_cmd.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "log_util.h"
#include "ntfy.h"
#include "desktop.h"

// "since" parametr pro ntfy: nejdriv unix cas startu (stare zpravy se ignoruji),
// pak id posledni videne zpravy.
static String sinceParam = "";

void ntfyCmdPoll() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (sinceParam.length() == 0) {
    time_t now = time(nullptr);
    if (now < 1700000000) return;   // cas jeste neni synchronizovany z NTP
    sinceParam = String((unsigned long)now);
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  http.setTimeout(8000);

  String url = String("https://ntfy.sh/") + NTFY_REPORT_TOPIC + "/json?poll=1&since=" + sinceParam;
  if (!http.begin(client, url)) return;

  const int code = http.GET();
  if (code != 200) {
    logValue(F("[NTFY-CMD] poll failed, code: "), code);
    http.end();
    return;
  }

  const String body = http.getString();
  http.end();

  bool wol = false;
  int pos = 0;
  while (pos < (int)body.length()) {
    int nl = body.indexOf('\n', pos);
    if (nl < 0) nl = body.length();
    String line = body.substring(pos, nl);
    pos = nl + 1;
    line.trim();
    if (line.isEmpty()) continue;

    JsonDocument doc;
    if (deserializeJson(doc, line)) continue;

    const char *id = doc["id"] | "";
    if (*id) sinceParam = id;                       // posun okna, zpravu uz nezpracujeme znovu
    if (strcmp(doc["event"] | "", "message") != 0) continue;

    String msg = doc["message"] | "";
    msg.trim();
    msg.toUpperCase();
    if (msg == "WOL") wol = true;                   // vlastni reporty ESP se ignoruji
  }

  if (wol) {
    logLine(F("[NTFY-CMD] WOL prikaz prijat"));
    const bool ok = wakeDesktop();
    ntfyPostQuiet(ok ? "WOL odeslan" : "WOL selhal",
                  String(ok ? "Magic packet odeslan na " : "Magic packet se nepodarilo odeslat na ") + WOL_TARGET_MAC);
  }
}
