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

struct OrderItem {
  String name;
  int count = 0;
  String nbt;
  String fingerprint;
};

struct OrderRecord {
  String orderId;
  String status;
  String destination;
  String deliveryMode;
  String recipient;
  uint32_t created = 0;
  uint32_t updated = 0;
  std::vector<OrderItem> items;
};

struct PackageRecord {
  String packageId;
  String orderId;
  String address;
  String destination;
  String deliveryMode;
  String recipient;
  String status;
  uint32_t created = 0;
  uint32_t updated = 0;

  String contentsJson;
  String filterJson;

  String currentNode;
  String currentNodeName;
  String lastEvent;
  uint32_t lastSeenMs = 0;
  String lastSeenIso;
  String lastSeenLabel;
  String historyJson;
};

extern std::vector<Command> commandQueue;
extern std::vector<OrderRecord> orders;
extern std::vector<PackageRecord> packages;

extern String meStorage;
extern uint32_t meLastUpdate;

extern bool queueDirty;
extern bool meDirty;
extern bool ordersDirty;
extern bool packagesDirty;
