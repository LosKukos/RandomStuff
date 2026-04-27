#include "persistence.h"
#include "app_state.h"
#include "utils.h"
#include "orders.h"
#include "packages.h"
#include "nodes.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

void saveConfig() {
  StaticJsonDocument<256> doc;
  doc["ssid"] = staSsid;
  doc["pass"] = staPass;

  File f = LittleFS.open("/config.json", "w");
  if (!f) { addLog("[FS] failed to open /config.json for write"); return; }
  serializeJson(doc, f);
  f.close();
  addLog("[FS] config saved");
}

void loadConfig() {
  if (!LittleFS.exists("/config.json")) { addLog("[FS] config.json not found"); return; }
  File f = LittleFS.open("/config.json", "r");
  if (!f) { addLog("[FS] failed to open /config.json for read"); return; }
  if (f.size() == 0) { addLog("[FS] config.json empty"); f.close(); return; }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { addLog("[FS] config.json parse failed"); return; }

  staSsid = doc["ssid"] | "";
  staPass = doc["pass"] | "";
  addLog("[FS] config loaded");
}

void saveQueue() {
  StaticJsonDocument<12288> doc;
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& cmd : commandQueue) {
    if (cmd.status == "done" || cmd.status == "failed" || cmd.status == "partial" || cmd.status == "timeout") continue;
    JsonObject o = arr.createNestedObject();
    o["id"] = cmd.id;
    o["type"] = cmd.type;
    o["payload"] = cmd.payload;
    o["status"] = cmd.status;
    o["created"] = cmd.created;
    o["updated"] = cmd.updated;
  }

  File f = LittleFS.open("/queue.json", "w");
  if (!f) { addLog("[FS] failed to open /queue.json for write"); return; }
  serializeJson(doc, f);
  f.close();
  queueDirty = false;
  addLog("[FS] queue saved");
}

void loadQueue() {
  if (!LittleFS.exists("/queue.json")) { addLog("[FS] queue.json not found"); return; }
  File f = LittleFS.open("/queue.json", "r");
  if (!f) { addLog("[FS] failed to open /queue.json for read"); return; }
  if (f.size() == 0) { addLog("[FS] queue.json empty"); f.close(); return; }

  StaticJsonDocument<12288> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { addLog("[FS] queue.json parse failed"); return; }

  commandQueue.clear();
  for (JsonObject o : doc.as<JsonArray>()) {
    Command cmd;
    cmd.id = o["id"] | "";
    cmd.type = o["type"] | "";
    cmd.payload = o["payload"] | "{}";
    cmd.status = o["status"] | "queued";
    cmd.created = o["created"] | 0;
    cmd.updated = o["updated"] | 0;
    commandQueue.push_back(cmd);
  }
  addLog("[FS] queue loaded");
}

void saveME() {
  File f = LittleFS.open("/me.json", "w");
  if (!f) { addLog("[FS] failed to open /me.json for write"); return; }
  f.print(meStorage);
  f.close();
  meDirty = false;
  addLog("[FS] me cache saved");
}

void loadME() {
  if (!LittleFS.exists("/me.json")) { addLog("[FS] me.json not found"); return; }
  File f = LittleFS.open("/me.json", "r");
  if (!f) { addLog("[FS] failed to open /me.json for read"); return; }
  if (f.size() == 0) { addLog("[FS] me.json empty"); f.close(); return; }
  meStorage = f.readString();
  f.close();
  addLog("[FS] me cache loaded");
}

void saveOrders() {
  DynamicJsonDocument doc(24576);
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& order : orders) {
    JsonObject o = arr.createNestedObject();
    serializeOrder(o, order);
  }

  File f = LittleFS.open("/orders.json", "w");
  if (!f) { addLog("[FS] failed to open /orders.json for write"); return; }
  serializeJson(doc, f);
  f.close();
  ordersDirty = false;
  addLog("[FS] orders saved");
}

void loadOrders() {
  if (!LittleFS.exists("/orders.json")) { addLog("[FS] orders.json not found"); return; }
  File f = LittleFS.open("/orders.json", "r");
  if (!f) { addLog("[FS] failed to open /orders.json for read"); return; }
  if (f.size() == 0) { addLog("[FS] orders.json empty"); f.close(); return; }

  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { addLog("[FS] orders.json parse failed"); return; }

  orders.clear();
  for (JsonObject o : doc.as<JsonArray>()) {
    OrderRecord order;
    order.orderId = o["orderId"] | "";
    order.status = o["status"] | "pending";
    order.destination = o["destination"] | "";
    order.deliveryMode = o["deliveryMode"] | "";
    order.recipient = o["recipient"] | "";
    order.created = o["created"] | 0;
    order.updated = o["updated"] | 0;

    for (JsonObject itemObj : o["items"].as<JsonArray>()) {
      OrderItem item;
      item.name = itemObj["name"] | "";
      item.count = itemObj["count"] | 0;
      item.nbt = itemObj["nbt"] | "";
      item.fingerprint = itemObj["fingerprint"] | "";
      order.items.push_back(item);
    }
    orders.push_back(order);
  }
  addLog("[FS] orders loaded");
}

void savePackages() {
  DynamicJsonDocument doc(32768);
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& pkg : packages) {
    JsonObject o = arr.createNestedObject();
    o["packageId"] = pkg.packageId;
    o["orderId"] = pkg.orderId;
    o["address"] = pkg.address;
    o["destination"] = pkg.destination;
    o["deliveryMode"] = pkg.deliveryMode;
    o["recipient"] = pkg.recipient;
    o["status"] = pkg.status;
    o["created"] = pkg.created;
    o["updated"] = pkg.updated;
    o["contentsJson"] = pkg.contentsJson;
    o["filterJson"] = pkg.filterJson;
    o["currentNode"] = pkg.currentNode;
    o["currentNodeName"] = pkg.currentNodeName;
    o["lastEvent"] = pkg.lastEvent;
    o["lastSeenMs"] = pkg.lastSeenMs;
    o["lastSeenIso"] = pkg.lastSeenIso;
    o["lastSeenLabel"] = pkg.lastSeenLabel;
    o["historyJson"] = pkg.historyJson;
  }

  File f = LittleFS.open("/packages.json", "w");
  if (!f) { addLog("[FS] failed to open /packages.json for write"); return; }
  serializeJson(doc, f);
  f.close();
  packagesDirty = false;
  addLog("[FS] packages saved");
}

void loadPackages() {
  if (!LittleFS.exists("/packages.json")) { addLog("[FS] packages.json not found"); return; }
  File f = LittleFS.open("/packages.json", "r");
  if (!f) { addLog("[FS] failed to open /packages.json for read"); return; }
  if (f.size() == 0) { addLog("[FS] packages.json empty"); f.close(); return; }

  DynamicJsonDocument doc(32768);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { addLog("[FS] packages.json parse failed"); return; }

  packages.clear();
  for (JsonObject o : doc.as<JsonArray>()) {
    PackageRecord pkg;
    pkg.packageId = o["packageId"] | "";
    pkg.orderId = o["orderId"] | "";
    pkg.address = o["address"] | "";
    pkg.destination = o["destination"] | "";
    pkg.deliveryMode = o["deliveryMode"] | "";
    pkg.recipient = o["recipient"] | "";
    pkg.status = o["status"] | "packed";
    pkg.created = o["created"] | 0;
    pkg.updated = o["updated"] | 0;
    pkg.contentsJson = o["contentsJson"] | "[]";
    pkg.filterJson = o["filterJson"] | "{}";
    pkg.currentNode = o["currentNode"] | "";
    pkg.currentNodeName = o["currentNodeName"] | "";
    pkg.lastEvent = o["lastEvent"] | "";
    pkg.lastSeenMs = o["lastSeenMs"] | 0;
    pkg.lastSeenIso = o["lastSeenIso"] | "";
    pkg.lastSeenLabel = o["lastSeenLabel"] | "";
    pkg.historyJson = o["historyJson"] | "[]";
    packages.push_back(pkg);
  }
  addLog("[FS] packages loaded");
}

void saveNodes() {
  DynamicJsonDocument doc(12288);
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& node : nodes) {
    JsonObject o = arr.createNestedObject();
    o["nodeId"] = node.nodeId;
    o["nodeName"] = node.nodeName;
    o["created"] = node.created;
    o["updated"] = node.updated;
    o["lastSeenMs"] = node.lastSeenMs;
    o["lastSeenIso"] = node.lastSeenIso;
    o["lastSeenLabel"] = node.lastSeenLabel;
  }

  File f = LittleFS.open("/nodes.json", "w");
  if (!f) { addLog("[FS] failed to open /nodes.json for write"); return; }
  serializeJson(doc, f);
  f.close();
  nodesDirty = false;
  addLog("[FS] nodes saved");
}

void loadNodes() {
  if (!LittleFS.exists("/nodes.json")) { addLog("[FS] nodes.json not found"); return; }
  File f = LittleFS.open("/nodes.json", "r");
  if (!f) { addLog("[FS] failed to open /nodes.json for read"); return; }
  if (f.size() == 0) { addLog("[FS] nodes.json empty"); f.close(); return; }

  DynamicJsonDocument doc(12288);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { addLog("[FS] nodes.json parse failed"); return; }

  nodes.clear();
  for (JsonObject o : doc.as<JsonArray>()) {
    NodeRecord node;
    node.nodeId = o["nodeId"] | "";
    node.nodeName = o["nodeName"] | "";
    node.created = o["created"] | 0;
    node.updated = o["updated"] | 0;
    node.lastSeenMs = o["lastSeenMs"] | 0;
    node.lastSeenIso = o["lastSeenIso"] | "";
    node.lastSeenLabel = o["lastSeenLabel"] | "";
    if (!node.nodeId.isEmpty()) nodes.push_back(node);
  }
  addLog("[FS] nodes loaded");
}
