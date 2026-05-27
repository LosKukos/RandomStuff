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

struct NodeRecord {
  String nodeId;
  String nodeName;
  uint32_t created = 0;
  uint32_t updated = 0;
  uint32_t lastSeenMs = 0;
  String lastSeenIso;
  String lastSeenLabel;
};

struct UserRecord {
  String userId;
  String username;
  String password;
  String mcName;
  String sessionToken;
  uint32_t created = 0;
  uint32_t lastSeen = 0;
};

extern std::vector<Command> commandQueue;
extern std::vector<OrderRecord> orders;
extern std::vector<PackageRecord> packages;
extern std::vector<NodeRecord> nodes;
extern std::vector<UserRecord> users;

extern String meStorage;
extern uint32_t meLastUpdate;

extern String playersOnline;   // JSON array of MC player names, pushed by CC master

extern bool queueDirty;
extern bool meDirty;
extern bool ordersDirty;
extern bool packagesDirty;
extern bool nodesDirty;
extern bool usersDirty;
