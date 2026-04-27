#include "utils.h"
#include "app_state.h"
#include <Arduino.h>

void addLog(const String& msg) {
  Serial.println(msg);
  logs.push_back(msg);
  if (logs.size() > 100) {
    logs.erase(logs.begin());
  }
}

String genId() {
  static uint32_t counter = 0;
  counter++;
  return "cmd_" + String(millis()) + "_" + String(counter);
}

bool isSTA(AsyncWebServerRequest* req) {
  return req->client()->localIP() == WiFi.localIP();
}

bool isAP(AsyncWebServerRequest* req) {
  return req->client()->localIP() == WiFi.softAPIP();
}

String makeOkResponse(std::function<void(JsonObject)> fill) {
  StaticJsonDocument<1024> doc;
  doc["ok"] = true;
  JsonObject data = doc.createNestedObject("data");
  fill(data);

  String out;
  serializeJson(doc, out);
  return out;
}

String makeErrorResponse(const char* err) {
  StaticJsonDocument<256> doc;
  doc["ok"] = false;
  doc["error"] = err;

  String out;
  serializeJson(doc, out);
  return out;
}
