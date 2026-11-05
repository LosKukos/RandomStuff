#include "web_server.h"
#include <WiFi.h>
#include <LittleFS.h>
#include "config.h"
#include "state.h"
#include "log_util.h"
#include "devices.h"
#include "time_svatky.h"
#include "ntfy.h"
#include "diagnostics.h"
#include "desktop.h"
#include "diagnostics.h"
#include "weather.h"
#include "sensors.h"
#include "tasks.h"
#include "ntfy.h"
#include "breadcrumbs.h"

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// =====================================================
// DASHBOARD JSON
// =====================================================

static void addDeviceToJson(JsonArray &devices, const DeviceState &device) {
  JsonObject d = devices.add<JsonObject>();
  d["id"]         = device.id;
  d["name"]       = device.name;
  d["type"]       = device.type;
  d["ip"]         = device.ip;
  d["host"]       = device.host;
  d["online"]     = device.online;
  d["rssi"]       = device.rssi;
  d["uptime"]     = device.uptime;
  d["lastSeenMs"] = device.lastSeenMs;

  if (device.type == "wled") {
    d["power"]      = device.power;
    d["brightness"] = device.brightness;
  }

  if (device.type == "esp") {
    d["heap"]   = device.heap;
    d["mac"]    = device.mac;
    d["role"]   = device.role;
    d["status"] = device.status;
    d["chip"]   = device.chip;
  }

  d["version"] = device.version;
  d["error"]   = device.error;
}

String buildDashboardJson(bool wsWrap) {
  JsonDocument doc;

  if (wsWrap) doc["type"] = "devices";

  JsonArray devices = doc["devices"].to<JsonArray>();

  if (lockMutex(deviceMutex)) {
    addDeviceToJson(devices, wledPokoj);
    addDeviceToJson(devices, mcEsp);
    addDeviceToJson(devices, panicNode);
    unlockMutex(deviceMutex);
  }

  if (lockMutex(alarmMutex)) {
    JsonObject alarm      = doc["alarm"].to<JsonObject>();
    alarm["active"]       = activeAlarm.active;
    alarm["eventId"]      = activeAlarm.eventId;
    alarm["deviceId"]     = activeAlarm.deviceId;
    alarm["deviceName"]   = activeAlarm.deviceName;
    alarm["receivedAtMs"] = activeAlarm.receivedAtMs;
    alarm["acked"]        = activeAlarm.acked;
    alarm["type"]         = activeAlarm.type;
    unlockMutex(alarmMutex);
  }

  JsonObject sys       = doc["system"].to<JsonObject>();
  sys["ip"]            = WiFi.localIP().toString();
  sys["mdns"]          = String(DASHBOARD_MDNS_NAME) + ".local";
  sys["gateway"]       = WiFi.gatewayIP().toString();
  sys["subnet"]        = WiFi.subnetMask().toString();
  sys["dns"]           = WiFi.dnsIP().toString();
  sys["wifiStatus"]    = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
  sys["rssi"]          = WiFi.RSSI();
  sys["heap"]          = ESP.getFreeHeap();
  sys["uptimeMs"]      = millis();
  sys["netReady"]      = getNetReady();
  sys["timeReady"]     = getTimeReady();
  sys["logicDone"]     = getLogicDone();
  sys["fatalError"]    = getFatalError();
  sys["needsUserAttention"] = getNeedsUserAttention();
  sys["autoSendDone"]  = getAutoSendDone();
  sys["lastSendOk"]    = getLastSendOk();
  sys["currentDateKey"]  = getCurrentDateKey();
  sys["currentFullDate"] = getCurrentFullDate();
  sys["currentNames"]    = getCurrentNames();
  sys["lastSent"]        = getLastSent();
  sys["resetReason"]     = resetReasonStr();
  sys["bootCount"]       = (uint32_t)bootCounter;
  sys["minHeap"]         = bcMinHeap();      // nejnizsi volny internal heap od startu
  sys["maxBlock"]        = bcMaxBlock();     // nejvetsi souvisly volny blok (fragmentace)

  {
    JsonObject n = sys["ntfyReport"].to<JsonObject>();
    uint32_t at = ntfyReportLastAtMs();
    n["code"] = ntfyReportLastCode();                        // 200 = ok, 0 = jeste neodeslano, zaporne = chyba
    n["ageS"] = at ? (int32_t)((millis() - at) / 1000) : -1;  // pred kolika s byl posledni pokus
  }

  if (lockMutex(weatherMutex)) {
    JsonObject w = doc["weather"].to<JsonObject>();
    w["valid"]        = weather.valid;
    w["temp"]         = weather.temp;
    w["apparentTemp"] = weather.apparentTemp;
    w["windSpeed"]    = weather.windSpeed;
    w["code"]         = weather.code;
    w["condition"]    = weather.condition;
    w["icon"]         = weather.icon;
    w["sunrise"]      = weather.sunrise;
    w["sunset"]       = weather.sunset;
    w["sunriseMin"]   = weather.sunriseMin;
    w["sunsetMin"]    = weather.sunsetMin;
    w["moonPhase"]    = weather.moonPhase;
    w["moonEmoji"]    = weather.moonEmoji;
    w["moonName"]     = weather.moonName;
    w["updatedAtMs"]  = weather.updatedAtMs;
    JsonArray fc = w["forecast"].to<JsonArray>();
    int fcLimit = min(weather.forecastCount, 48);
    for (int i = 0; i < fcLimit; i++) {
      JsonObject e = fc.add<JsonObject>();
      e["hour"] = weather.forecast[i].hour;
      e["temp"] = weather.forecast[i].temp;
      e["code"] = weather.forecast[i].code;
      e["icon"] = wmoToIcon(weather.forecast[i].code);
      e["date"] = weather.forecast[i].date;
    }
    unlockMutex(weatherMutex);
  }

  if (lockMutex(climateMutex)) {
    JsonObject c = doc["climate"].to<JsonObject>();
    auto put = [&](const char *key, const DhtReading &r) {
      JsonObject o = c[key].to<JsonObject>();
      o["valid"]       = r.valid;
      o["temp"]        = serialized(String(r.temp, 1));
      o["hum"]         = serialized(String(r.hum, 0));
      o["updatedAtMs"] = r.updatedAtMs;
      o["ageS"]        = r.updatedAtMs ? (millis() - r.updatedAtMs) / 1000 : -1;
      o["trend"]       = serialized(String(r.trend, 1));
      o["trendMin"]    = r.trendMin;
    };
    put("indoor",  climate.indoor);
    put("outdoor", climate.outdoor);
    unlockMutex(climateMutex);
  }

  {
    bool known = false, online = false;
    uint32_t chk = 0, wol = 0;
    if (lockMutex(deviceMutex)) {
      known = desktop.known; online = desktop.online;
      chk = desktop.lastCheckMs; wol = desktop.wolSentAtMs;
      unlockMutex(deviceMutex);
    }
    JsonObject d = doc["desktop"].to<JsonObject>();
    d["known"]     = known;
    d["online"]    = online;
    d["ip"]        = DESKTOP_IP;
    d["checkAgeS"] = chk ? (int32_t)((millis() - chk) / 1000) : -1;
    d["wolAgeS"]   = wol ? (int32_t)((millis() - wol) / 1000) : -1;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

void broadcastDashboard() {
  ws.textAll(buildDashboardJson(true));
}

// =====================================================
// WEBSOCKET / WEB API
// =====================================================

static void onWsEvent(
  AsyncWebSocket *server,
  AsyncWebSocketClient *client,
  AwsEventType type,
  void *arg,
  uint8_t *data,
  size_t len
) {
  switch (type) {
    case WS_EVT_CONNECT:
      logValue(F("[WS] client connected: "), (int)client->id());
      client->text(buildDashboardJson(true));
      break;
    case WS_EVT_DISCONNECT:
      logValue(F("[WS] client disconnected: "), (int)client->id());
      break;
    default:
      break;
  }
}

void sendJsonResponse(AsyncWebServerRequest *request, int code, const JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  request->send(code, "application/json", out);
}

void startDashboardServer() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/api/devices", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildDashboardJson(false));
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildDashboardJson(false));
  });

  server.on("/api/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request) {
    printDiagnostics();
    request->send(200, "application/json", buildDashboardJson(false));
  });

  // RTC breadcrumbs: co ESP delalo pred poslednim restartem + aktualni heap/stacky
  server.on("/api/crash", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    bcFillJson(doc.to<JsonObject>());
    sendJsonResponse(request, 200, doc);
  });

  // Neblokujici: handler bezi v async_tcp tasku (watchdog ~5 s), takze jen
  // vzbudi poll tasky a hned vrati aktualni stav. Cerstva data dorazi
  // pres websocket, jakmile je tasky nactou.
  server.on("/api/refresh", HTTP_POST, [](AsyncWebServerRequest *request) {
    requestRefreshAll();
    request->send(202, "application/json", buildDashboardJson(false));
  });

  // Sledovane svatky: POST /api/watch?names=Jan,Petr -> /watch.json (LittleFS, persistentni)
  // GET jde pres serveStatic (/watch.json)
  server.on("/api/watch", HTTP_POST, [](AsyncWebServerRequest *request) {
    String raw = request->hasParam("names") ? request->getParam("names")->value() : "";
    if (raw.length() > 600) {
      request->send(400, "application/json", "{\"ok\":false,\"message\":\"too long\"}");
      return;
    }
    JsonDocument out;
    JsonArray arr = out.to<JsonArray>();
    int start = 0;
    while (start <= (int)raw.length() && arr.size() < 30) {
      int comma = raw.indexOf(',', start);
      if (comma < 0) comma = raw.length();
      String n = raw.substring(start, comma);
      n.trim();
      if (n.length() > 0 && n.length() <= 30) arr.add(n);
      start = comma + 1;
    }
    File f = LittleFS.open("/watch.json", "w");
    bool ok = false;
    if (f) { ok = serializeJson(out, f) > 0 || arr.size() == 0; f.close(); }
    JsonDocument doc;
    doc["ok"]    = ok;
    doc["count"] = arr.size();
    sendJsonResponse(request, ok ? 200 : 500, doc);
  });

  // Rucni WOL z dashboardu (UDP broadcast, rychle a neblokujici)
  server.on("/api/wol", HTTP_POST, [](AsyncWebServerRequest *request) {
    const bool ok = wakeDesktop();
    broadcastDashboard();
    JsonDocument doc;
    doc["ok"]      = ok;
    doc["message"] = ok ? "WOL odeslan" : "WOL selhal";
    sendJsonResponse(request, ok ? 200 : 500, doc);
  });

  // Rucni test ntfy reportu (neblokujici) - vysledek v /api/status -> system.ntfyReport
  server.on("/api/ntfy-test", HTTP_POST, [](AsyncWebServerRequest *request) {
    requestNtfyTest();
    request->send(202, "application/json", "{\"ok\":true,\"message\":\"ntfy test queued\"}");
  });

  // ── /api/panic ──────────────────────────────────────────────────────────
  server.on("/api/panic", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {

      bcMark(BT_WEB, BS_H_PANIC);
      JsonDocument doc;
      if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"ok\":false,\"message\":\"parse error\"}");
        return;
      }

      const String type    = doc["type"]       | "unknown";
      const String eventId = doc["eventId"]    | "";
      const String devId   = doc["deviceId"]   | "";
      const String devName = doc["deviceName"] | "";

      bool firstTrigger = false;

      if (lockMutex(alarmMutex)) {
        if (type == "panic") {
          firstTrigger = !activeAlarm.active;  // true jen pri uplne prvnim prijeti
          activeAlarm.active     = true;
          activeAlarm.acked      = true;
          activeAlarm.eventId    = eventId;
          activeAlarm.deviceId   = devId;
          activeAlarm.deviceName = devName;
          activeAlarm.type       = "panic";
          struct tm t;
          activeAlarm.receivedAtMs = getLocalTime(&t) ? (uint32_t)mktime(&t) : 0;
          if (firstTrigger) {
            activeAlarm.lastNtfyAt = millis();
            activeAlarm.ntfyCount  = 0;
          }
          logLine("[ALARM] panic received: " + eventId + " from " + devName);
        } else if (type == "panic_reset") {
          activeAlarm.active = false;
          activeAlarm.type   = "panic_reset";
          logLine("[ALARM] reset received: " + eventId);
        }
        unlockMutex(alarmMutex);
      }

      // ntfy jde hned jen pri prvnim prijeti — repeat resi emergencyTask
      if (firstTrigger) {
        bcMark(BT_WEB, BS_NTFY_PANIC);
        sendNtfyPanic(devName, eventId, 0);
      }

      broadcastDashboard();

      JsonDocument ack;
      ack["schema"]   = "espdash.ack.v1";
      ack["eventId"]  = eventId;
      ack["deviceId"] = devId;
      ack["ok"]       = true;
      ack["accepted"] = true;
      ack["message"]  = "panic event accepted";
      sendJsonResponse(request, 200, ack);
    }
  );

  server.on("/api/send", HTTP_POST, [](AsyncWebServerRequest *request) {
    bcMark(BT_WEB, BS_H_SEND);
    bool ok = doManualSend();
    broadcastDashboard();
    JsonDocument doc;
    doc["ok"]       = ok;
    doc["message"]  = ok ? "Sent" : "Send failed";
    doc["date"]     = getCurrentFullDate();
    doc["names"]    = getCurrentNames();
    doc["lastSent"] = getLastSent();
    sendJsonResponse(request, ok ? 200 : 500, doc);
  });

  server.on("/api/update-time", HTTP_POST, [](AsyncWebServerRequest *request) {
    bcMark(BT_WEB, BS_H_TIME);
    bool ok = updateLocalTime();
    if (ok) setTimeReady(true);
    broadcastDashboard();
    JsonDocument doc;
    doc["ok"]    = ok;
    doc["date"]  = getCurrentFullDate();
    doc["key"]   = getCurrentDateKey();
    doc["names"] = getCurrentNames();
    sendJsonResponse(request, ok ? 200 : 500, doc);
  });

  server.on("/api/reset-last-sent", HTTP_POST, [](AsyncWebServerRequest *request) {
    setLastSent("");
    saveLastSentFile("");
    setAutoSendDone(false);
    setLastSendOk(false);
    broadcastDashboard();
    JsonDocument doc;
    doc["ok"]       = true;
    doc["lastSent"] = getLastSent();
    sendJsonResponse(request, 200, doc);
  });

  server.on("/api/wled/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool ok = wledTogglePower();
    JsonDocument doc;
    doc["ok"]      = ok;
    doc["message"] = ok ? "WLED toggled" : "WLED toggle failed";
    sendJsonResponse(request, ok ? 200 : 500, doc);
  });

  server.on("/api/wled/on", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool ok = wledSetPower(true);
    JsonDocument doc;
    doc["ok"]      = ok;
    doc["message"] = ok ? "WLED on" : "WLED on failed";
    sendJsonResponse(request, ok ? 200 : 500, doc);
  });

  server.on("/api/wled/off", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool ok = wledSetPower(false);
    JsonDocument doc;
    doc["ok"]      = ok;
    doc["message"] = ok ? "WLED off" : "WLED off failed";
    sendJsonResponse(request, ok ? 200 : 500, doc);
  });

  server.on("/api/wled/brightness", HTTP_POST, [](AsyncWebServerRequest *request) {
    int bri = 128;
    if (request->hasParam("value", true)) bri = request->getParam("value", true)->value().toInt();
    else if (request->hasParam("value"))  bri = request->getParam("value")->value().toInt();
    bool ok = wledSetBrightness(bri);
    JsonDocument doc;
    doc["ok"]         = ok;
    doc["brightness"] = bri;
    doc["message"]    = ok ? "Brightness set" : "Brightness failed";
    sendJsonResponse(request, ok ? 200 : 500, doc);
  });

  server.on("/api/wled/identify", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool ok = wledIdentify();
    JsonDocument doc;
    doc["ok"]      = ok;
    doc["message"] = ok ? "Identify done" : "Identify failed";
    sendJsonResponse(request, ok ? 200 : 500, doc);
  });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "404 - tady nic neni, jen digitalni prach.");
  });

  server.begin();
  logLine(F("[WEB] Dashboard server started"));
}
