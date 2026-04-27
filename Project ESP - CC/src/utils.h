#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>
#include "app_state.h"

void addLog(const String& msg);
String genId();
String genOrderId();
bool isSTA(AsyncWebServerRequest* req);
bool isAP(AsyncWebServerRequest* req);
Command* findCommandById(const String& id);
OrderRecord* findOrderById(const String& orderId);
PackageRecord* findPackageById(const String& packageId);
String makeOkResponse(std::function<void(JsonObject)> fill);
String makeErrorResponse(const char* err);
