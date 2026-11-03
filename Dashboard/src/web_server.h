#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;
extern AsyncWebSocket ws;

// poskla JSON se stavem zarizeni/alarmu/systemu.
// wsWrap==true pridava {"type":"devices", ...} obal pro websocket push
String buildDashboardJson(bool wsWrap);

// posle buildDashboardJson(true) vsem websocket klientum
void broadcastDashboard();

void sendJsonResponse(AsyncWebServerRequest *request, int code, const JsonDocument &doc);

// zaregistruje routy/websocket a nastartuje AsyncWebServer
void startDashboardServer();
