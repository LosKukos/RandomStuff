#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>

void addLog(const String& msg);
String genId();
String genOrderId();
bool isSTA(AsyncWebServerRequest* req);
bool isAP(AsyncWebServerRequest* req);
String makeOkResponse(std::function<void(JsonObject)> fill);
String makeErrorResponse(const char* err);
String readBody(uint8_t* data, size_t len);
