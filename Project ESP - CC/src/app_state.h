#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <vector>

#define AP_SSID "AP client"
#define AP_PASS "1234567890"

extern AsyncWebServer server;
extern AsyncWebSocket ws;

extern TaskHandle_t wifiTaskHandle;
extern TaskHandle_t webTaskHandle;
extern TaskHandle_t commandTaskHandle;

extern volatile bool staConnected;
extern String staSsid;
extern String staPass;

extern std::vector<String> logs;

struct Command {
  String id;
  String type;
  String payload;
  String status;
  uint32_t created;
  uint32_t updated;
};

extern std::vector<Command> commandQueue;

extern String meStorage;
extern uint32_t meLastUpdate;

extern bool queueDirty;
extern bool meDirty;
