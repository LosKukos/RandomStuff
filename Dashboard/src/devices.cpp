#include "devices.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "log_util.h"
#include "web_server.h"

// =====================================================
// mDNS
// =====================================================

void startMdns() {
  if (WiFi.status() != WL_CONNECTED) {
    logLine(F("[mDNS] WiFi not connected"));
    return;
  }
  if (MDNS.begin(DASHBOARD_MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    logValue(F("[mDNS] started: http://"), String(DASHBOARD_MDNS_NAME) + ".local");
  } else {
    logLine(F("[mDNS] failed to start"));
  }
}

IPAddress resolveMdnsHost(const char* hostName) {
  logValue(F("[mDNS] resolving: "), String(hostName) + ".local");
  IPAddress ip = MDNS.queryHost(hostName, 1500);
  if (ip) logValue(F("[mDNS] resolved: "), ip.toString());
  else    logValue(F("[mDNS] failed: "), String(hostName));
  return ip;
}

String resolveDeviceAddress(DeviceState &device, const char* hostName, const char* fallbackIp) {
  IPAddress ip = resolveMdnsHost(hostName);

  if (ip) {
    String ipStr = ip.toString();
    if (lockMutex(deviceMutex)) { device.ip = ipStr; device.error = ""; unlockMutex(deviceMutex); }
    return ipStr;
  }

  // mDNS selhalo — pouzij naposledy znamou IP pokud ji mame
  String cachedIp;
  if (lockMutex(deviceMutex)) { cachedIp = device.ip; unlockMutex(deviceMutex); }

  if (cachedIp.length() > 0 && cachedIp != "0.0.0.0") {
    logValue(F("[mDNS] using cached IP: "), cachedIp);
    return cachedIp;
  }

  // zadna cache — zkus staticky fallback
  if (String(fallbackIp).length() > 0) {
    logValue(F("[mDNS] using fallback: "), String(fallbackIp));
    if (lockMutex(deviceMutex)) { device.ip = String(fallbackIp); unlockMutex(deviceMutex); }
    return String(fallbackIp);
  }

  if (lockMutex(deviceMutex)) {
    device.online = false;
    device.error  = "mDNS resolve failed";
    device.status = "offline";
    unlockMutex(deviceMutex);
  }
  return "";
}

// =====================================================
// WLED
// =====================================================

static void setWledOffline(const String &err) {
  if (lockMutex(deviceMutex)) { wledPokoj.online = false; wledPokoj.error = err; unlockMutex(deviceMutex); }
}

static String resolveWledAddress() {
  return resolveDeviceAddress(wledPokoj, WLED_POKOJ_HOST, WLED_POKOJ_FALLBACK_IP);
}

void pollWLED() {
  if (WiFi.status() != WL_CONNECTED) { setWledOffline("WiFi disconnected"); return; }

  String ip = resolveWledAddress();
  if (ip.isEmpty()) return;

  HTTPClient httpInfo;
  httpInfo.setTimeout(1500);
  if (!httpInfo.begin("http://" + ip + "/json/info")) { setWledOffline("info begin failed"); return; }
  int infoCode = httpInfo.GET();
  if (infoCode != 200) { httpInfo.end(); setWledOffline("info code " + String(infoCode)); return; }
  String infoPayload = httpInfo.getString();
  httpInfo.end();

  JsonDocument infoDoc;
  if (deserializeJson(infoDoc, infoPayload)) { setWledOffline("info JSON parse failed"); return; }

  HTTPClient httpState;
  httpState.setTimeout(1500);
  bool gotState = false;
  JsonDocument stateDoc;
  if (httpState.begin("http://" + ip + "/json/state")) {
    if (httpState.GET() == 200) {
      if (!deserializeJson(stateDoc, httpState.getString())) gotState = true;
    }
    httpState.end();
  }

  if (lockMutex(deviceMutex)) {
    wledPokoj.online     = true;
    wledPokoj.error      = "";
    wledPokoj.lastSeenMs = millis();
    if (infoDoc["name"].is<const char*>())    wledPokoj.name    = infoDoc["name"].as<String>();
    if (infoDoc["ver"].is<const char*>())     wledPokoj.version = infoDoc["ver"].as<String>();
    if (infoDoc["wifi"]["rssi"].is<int>())    wledPokoj.rssi    = infoDoc["wifi"]["rssi"].as<int>();
    if (infoDoc["uptime"].is<uint32_t>())     wledPokoj.uptime  = infoDoc["uptime"].as<uint32_t>();
    if (gotState) {
      wledPokoj.power      = stateDoc["on"]  | false;
      wledPokoj.brightness = stateDoc["bri"] | 0;
    }
    unlockMutex(deviceMutex);
  }
}

static bool wledPostJson(const String &payload) {
  if (WiFi.status() != WL_CONNECTED) { setWledOffline("WiFi disconnected"); return false; }
  String ip = resolveWledAddress();
  if (ip.isEmpty()) return false;

  HTTPClient http;
  http.setTimeout(1500);
  if (!http.begin("http://" + ip + "/json/state")) { setWledOffline("POST begin failed"); return false; }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  http.end();

  if (code <= 0 || code >= 400) { setWledOffline("POST code " + String(code)); return false; }
  pollWLED();
  broadcastDashboard();
  return true;
}

bool wledSetPower(bool on) {
  return wledPostJson(String("{\"on\":") + (on ? "true" : "false") + "}");
}
bool wledTogglePower() {
  bool p = false;
  if (lockMutex(deviceMutex)) { p = wledPokoj.power; unlockMutex(deviceMutex); }
  return wledSetPower(!p);
}
bool wledSetBrightness(int bri) {
  bri = constrain(bri, 0, 255);
  return wledPostJson("{\"bri\":" + String(bri) + "}");
}
bool wledIdentify() {
  bool prevPower = false; int prevBri = 128;
  if (lockMutex(deviceMutex)) { prevPower = wledPokoj.power; prevBri = wledPokoj.brightness; unlockMutex(deviceMutex); }
  bool ok1 = wledPostJson("{\"on\":true,\"bri\":255}");
  delay(350);
  bool ok2 = wledPostJson(String("{\"on\":") + (prevPower ? "true" : "false") + ",\"bri\":" + String(prevBri) + "}");
  return ok1 && ok2;
}

// =====================================================
// CUSTOM ESP POLLING
// =====================================================

static void setCustomEspOffline(DeviceState &device, const String &err) {
  if (lockMutex(deviceMutex)) { device.online = false; device.error = err; device.status = "offline"; unlockMutex(deviceMutex); }
}

void pollCustomEsp(DeviceState &device, const char* hostName, const char* fallbackIp, const char* endpoint) {
  if (WiFi.status() != WL_CONNECTED) { setCustomEspOffline(device, "WiFi disconnected"); return; }

  String ip = resolveDeviceAddress(device, hostName, fallbackIp);
  if (ip.isEmpty()) return;

  HTTPClient http;
  http.setTimeout(1500);
  if (!http.begin("http://" + ip + String(endpoint))) { setCustomEspOffline(device, "HTTP begin failed"); return; }

  int code = http.GET();
  if (code != 200) { http.end(); setCustomEspOffline(device, "HTTP code " + String(code)); return; }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) { setCustomEspOffline(device, "JSON parse failed"); return; }

  if (lockMutex(deviceMutex)) {
    device.online     = true;
    device.error      = "";
    device.lastSeenMs = millis();
    if (doc["id"].is<const char*>())   device.id      = doc["id"].as<String>();
    if (doc["name"].is<const char*>()) device.name    = doc["name"].as<String>();
    if (doc["type"].is<const char*>()) device.type    = doc["type"].as<String>();
    if (doc["role"].is<const char*>()) device.role    = doc["role"].as<String>();
    if (doc["status"].is<const char*>()) device.status = doc["status"].as<String>();
    if (doc["fw"].is<const char*>())   device.version = doc["fw"].as<String>();
    if (doc["chip"].is<const char*>()) device.chip    = doc["chip"].as<String>();
    if (doc["mac"].is<const char*>())  device.mac     = doc["mac"].as<String>();
    if (doc["ip"].is<const char*>())   device.ip      = doc["ip"].as<String>();
    device.rssi   = doc["rssi"]    | 0;
    device.uptime = doc["uptimeMs"] | 0;
    device.heap   = doc["heap"]    | 0;
    unlockMutex(deviceMutex);
  }
}
