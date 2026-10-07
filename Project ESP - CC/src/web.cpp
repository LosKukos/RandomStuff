#include "web.h"
#include "app_state.h"
#include "utils.h"
#include "commands.h"
#include "persistence.h"
#include "orders.h"
#include "packages.h"
#include "nodes.h"
#include "users.h"
#include "time_service.h"
#include "mqtt_bridge.h"
#include "sync.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static void sendJson(AsyncWebServerRequest* req, int code, const String& body) {
  req->send(code, "application/json", body);
}

// Skládá tělo POST requestu z více TCP chunků a handler zavolá až po posledním.
static ArBodyHandlerFunction bodyHandler(std::function<void(AsyncWebServerRequest*, const String&)> fn) {
  return [fn](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (index == 0) {
      if (req->_tempObject) { free(req->_tempObject); req->_tempObject = nullptr; }
      req->_tempObject = malloc(total + 1);
    }
    char* buf = (char*)req->_tempObject;
    if (!buf) {
      if (index + len >= total) sendJson(req, 500, makeErrorResponse("out_of_memory"));
      return;
    }
    memcpy(buf + index, data, len);
    if (index + len >= total) {
      buf[total] = 0;
      String body(buf);
      free(req->_tempObject);
      req->_tempObject = nullptr;
      DataLock lock;  // vsechny POST handlery bezi pod zamkem
      fn(req, body);
    }
  };
}

static void emitEvent(const String& json) {
  mqttBroadcast(json);
}

static void emitOrderUpdate(const String& orderId, const String& status, JsonVariantConst meta = JsonVariantConst()) {
  StaticJsonDocument<1024> evt;
  evt["event"] = "order_updated"; evt["orderId"] = orderId; evt["status"] = status;
  if (!meta.isNull()) evt["meta"] = meta;
  String out; serializeJson(evt, out);
  emitEvent(out);
}

static void emitPackageEventWs(const PackageRecord& pkg, const String& eventName, const String& actorId, const String& actorName) {
  StaticJsonDocument<1024> evt;
  evt["event"] = "package_event"; evt["packageId"] = pkg.packageId; evt["orderId"] = pkg.orderId;
  evt["nodeId"] = actorId; evt["nodeName"] = actorName; evt["packageEvent"] = eventName;
  evt["status"] = pkg.status; evt["timeLabel"] = pkg.lastSeenLabel; evt["timeIso"] = pkg.lastSeenIso;
  String out; serializeJson(evt, out);
  emitEvent(out);
}

static void serializePackagesForOrder(JsonArray arr, const String& orderId) {
  for (const auto& pkg : packages) {
    if (pkg.orderId == orderId) { JsonObject p = arr.createNestedObject(); serializePackage(p, pkg); }
  }
}

static bool allPackagesForOrderHaveStatus(const String& orderId, const String& status, int& total, int& matching) {
  total = 0; matching = 0;
  for (const auto& pkg : packages) {
    if (pkg.orderId == orderId) { total++; if (pkg.status == status) matching++; }
  }
  return total > 0 && total == matching;
}

static void sendOrderWithPackages(AsyncWebServerRequest* req, const OrderRecord& order) {
  DynamicJsonDocument outDoc(24576);
  outDoc["ok"] = true;
  JsonObject dataObj = outDoc.createNestedObject("data");
  JsonObject orderObj = dataObj.createNestedObject("order");
  serializeOrder(orderObj, order);
  JsonArray pkgArr = dataObj.createNestedArray("packages");
  serializePackagesForOrder(pkgArr, order.orderId);
  String out; serializeJson(outDoc, out);
  sendJson(req, 200, out);
}

void setupWeb() {
  ws.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    (void)server; (void)arg; (void)data; (void)len;
    if (type == WS_EVT_CONNECT) addLog("[WS] client connected #" + String(client->id()));
    else if (type == WS_EVT_DISCONNECT) addLog("[WS] client disconnected #" + String(client->id()));
  });

  server.addHandler(&ws);

  server.on("/api/dash-status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
    StaticJsonDocument<512> doc;
    doc["id"] = "mc"; doc["name"] = "MC ESP"; doc["type"] = "esp"; doc["role"] = "custom-controller";
    doc["fw"] = "0.2.0"; doc["status"] = "ok"; doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress(); doc["rssi"] = WiFi.RSSI(); doc["uptimeMs"] = millis();
    doc["heap"] = ESP.getFreeHeap(); doc["chip"] = "ESP32";
    String out; serializeJson(doc, out); sendJson(req, 200, out);
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (isAP(req)) { req->send(LittleFS, "/config.html", "text/html"); return; }
    if (isSTA(req)) { req->send(LittleFS, "/index.html", "text/html"); return; }
    sendJson(req, 500, makeErrorResponse("unknown_interface"));
  });

  server.serveStatic("/css/", LittleFS, "/css/");
  server.serveStatic("/js/", LittleFS, "/js/");

  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isAP(req)) { sendJson(req, 403, makeErrorResponse("ap_only")); return; }
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String ssid = doc["ssid"] | ""; String pass = doc["pass"] | "";
      if (ssid.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_ssid")); return; }
      staSsid = ssid; staPass = pass; saveConfig();
      sendJson(req, 200, makeOkResponse([](JsonObject data) { data["saved"] = true; }));
    }));

  server.on("/api/debug", HTTP_GET, [](AsyncWebServerRequest* req) {
    DataLock lock;
    if (!isAP(req)) { sendJson(req, 403, makeErrorResponse("ap_only")); return; }
    TimeSnapshot t = getTimeSnapshot();
    sendJson(req, 200, makeOkResponse([&](JsonObject data) {
      data["heap"] = ESP.getFreeHeap(); data["uptime"] = millis(); data["wifi"] = staConnected;
      data["sta_ip"] = WiFi.localIP().toString(); data["ap_ip"] = WiFi.softAPIP().toString();
      data["ws"] = ws.count(); data["queue"] = commandQueue.size(); data["orders"] = orders.size();
      data["packages"] = packages.size(); data["nodes"] = nodes.size();
      data["me_age"] = meLastUpdate == 0 ? -1 : (int32_t)(millis() - meLastUpdate);
      data["time_synced"] = t.synced; data["time_label"] = t.label; data["time_iso"] = t.iso;
    }));
  });

  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAP(req)) { sendJson(req, 403, makeErrorResponse("ap_only")); return; }
    std::vector<String> snapshot = getLogsCopy();
    DynamicJsonDocument doc(8192); doc["ok"] = true;
    JsonArray arr = doc.createNestedArray("data");
    for (const auto& line : snapshot) arr.add(line);
    String out; serializeJson(doc, out); sendJson(req, 200, out);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    DataLock lock;
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
    TimeSnapshot t = getTimeSnapshot();
    sendJson(req, 200, makeOkResponse([&](JsonObject data) {
      data["wifi"] = staConnected; data["queue"] = commandQueue.size();
      data["orders"] = orders.size(); data["packages"] = packages.size(); data["nodes"] = nodes.size();
      data["me_age"] = meLastUpdate == 0 ? -1 : (int32_t)(millis() - meLastUpdate);
      data["time_synced"] = t.synced; data["time_label"] = t.label; data["time_iso"] = t.iso;
    }));
  });

  server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      StaticJsonDocument<768> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String type = doc["type"] | "";
      if (type.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_type")); return; }
      String payloadJson = "{}";
      if (doc["payload"].is<JsonVariantConst>()) serializeJson(doc["payload"], payloadJson);
      pushCommand(type, payloadJson);
      const String queuedId = commandQueue.back().id;
      sendJson(req, 200, makeOkResponse([&queuedId](JsonObject data) { data["queued"] = true; data["id"] = queuedId; }));
    }));

  server.on("/api/ack", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String id = doc["id"] | "";
      if (id.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_id")); return; }
      Command* cmd = findCommandById(id);
      if (!cmd) { sendJson(req, 404, makeErrorResponse("command_not_found")); return; }
      cmd->status = "accepted"; cmd->updated = millis(); queueDirty = true;
      sendJson(req, 200, makeOkResponse([&id](JsonObject data) { data["id"] = id; data["status"] = "accepted"; }));
    }));

  server.on("/api/result", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      StaticJsonDocument<4096> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String id = doc["id"] | ""; String status = doc["status"] | "";
      if (id.isEmpty() || status.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_fields")); return; }
      Command* cmd = findCommandById(id);
      if (!cmd) { sendJson(req, 404, makeErrorResponse("command_not_found")); return; }
      cmd->status = status; cmd->updated = millis(); queueDirty = true;
      StaticJsonDocument<4096> evt;
      evt["event"] = "command_result"; evt["id"] = id; evt["status"] = status;
      if (doc.containsKey("requested")) evt["requested"] = doc["requested"];
      if (doc.containsKey("accepted")) evt["accepted"] = doc["accepted"];
      if (doc.containsKey("reason")) evt["reason"] = doc["reason"];
      if (doc.containsKey("missing") && doc["missing"].is<JsonArrayConst>()) {
        JsonArray dst = evt.createNestedArray("missing");
        for (JsonObjectConst srcItem : doc["missing"].as<JsonArrayConst>()) {
          JsonObject d = dst.createNestedObject();
          d["name"] = srcItem["name"] | ""; d["displayName"] = srcItem["displayName"] | ""; d["count"] = srcItem["count"] | 0;
        }
      }
      String out; serializeJson(evt, out); emitEvent(out);
      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["id"] = id; data["status"] = status;
        if (doc.containsKey("requested")) data["requested"] = doc["requested"];
        if (doc.containsKey("accepted")) data["accepted"] = doc["accepted"];
        if (doc.containsKey("reason")) data["reason"] = doc["reason"];
      }));
      addLog("[CMD] result " + id + " -> " + status);
    }));

  server.on("/api/me/list", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      if (body.length() > 60000 || body.indexOf("\"items\"") < 0) { sendJson(req, 400, makeErrorResponse("invalid_me_snapshot")); return; }
      meStorage = body; meLastUpdate = millis(); meDirty = true;
      addLog("[ME] storage updated");
      sendJson(req, 200, makeOkResponse([](JsonObject data) { data["updated"] = true; }));
    }));

  server.on("/api/me/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    DataLock lock;
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
    sendJson(req, 200, meStorage);
  });

  server.on("/api/node/register", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      DynamicJsonDocument doc(1024);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      NodeRecord node; bool existed = false; String err;
      if (!registerNodeFromJson(doc, node, existed, err)) { sendJson(req, 400, makeErrorResponse(err.c_str())); return; }
      if (existed) { NodeRecord* existing = findNodeById(node.nodeId); if (existing) *existing = node; } else { nodes.push_back(node); }
      nodesDirty = true;
      addLog(String("[NODE] ") + (existed ? "registered existing " : "registered new ") + node.nodeId + " " + node.nodeName);
      StaticJsonDocument<512> evt;
      evt["event"] = "node_registered"; evt["nodeId"] = node.nodeId; evt["nodeName"] = node.nodeName; evt["existed"] = existed;
      String wsOut; serializeJson(evt, wsOut); emitEvent(wsOut);
      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["nodeId"] = node.nodeId; data["nodeName"] = node.nodeName; data["existed"] = existed;
        data["lastSeenLabel"] = node.lastSeenLabel; data["lastSeenIso"] = node.lastSeenIso;
      }));
    }));

  server.on("/api/node/heartbeat", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      DynamicJsonDocument doc(1024);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      NodeRecord* node = nullptr; String err;
      if (!heartbeatNodeFromJson(doc, node, err)) { sendJson(req, 404, makeErrorResponse(err.c_str())); return; }
      nodesDirty = true;
      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["nodeId"] = node->nodeId; data["nodeName"] = node->nodeName;
        data["lastSeenLabel"] = node->lastSeenLabel; data["lastSeenIso"] = node->lastSeenIso;
      }));
    }));

  server.on("/api/nodes/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    DataLock lock;
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
    DynamicJsonDocument doc(12288); doc["ok"] = true;
    JsonObject dataObj = doc.createNestedObject("data"); JsonArray arr = dataObj.createNestedArray("nodes");
    for (const auto& node : nodes) { JsonObject o = arr.createNestedObject(); serializeNode(o, node); }
    String out; serializeJson(doc, out); sendJson(req, 200, out);
  });

  server.on("/api/orders/create", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      DynamicJsonDocument doc(8192);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      OrderRecord order; String err;
      if (!createOrderFromJson(doc, order, err)) { sendJson(req, 400, makeErrorResponse(err.c_str())); return; }
      orders.push_back(order); ordersDirty = true;
      addLog("[ORDER] created " + order.orderId);
      StaticJsonDocument<512> evt;
      evt["event"] = "order_created"; evt["orderId"] = order.orderId; evt["status"] = order.status;
      String out; serializeJson(evt, out); emitEvent(out);
      sendJson(req, 200, makeOkResponse([&](JsonObject data) { data["orderId"] = order.orderId; data["status"] = order.status; }));
    }));

  server.on("/api/orders/pending", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      DynamicJsonDocument doc(12288); doc["ok"] = true;
      JsonObject dataObj = doc.createNestedObject("data"); JsonArray arr = dataObj.createNestedArray("orders");
      for (const auto& order : orders) { if (order.status == "pending") { JsonObject o = arr.createNestedObject(); serializeOrder(o, order); } }
      String out; serializeJson(doc, out); sendJson(req, 200, out);
    }));

  server.on("/api/orders/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    DataLock lock;
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
    DynamicJsonDocument doc(24576); doc["ok"] = true;
    JsonObject dataObj = doc.createNestedObject("data"); JsonArray arr = dataObj.createNestedArray("orders");
    for (const auto& order : orders) { JsonObject o = arr.createNestedObject(); serializeOrder(o, order); }
    if (doc.overflowed()) { sendJson(req, 507, makeErrorResponse("too_large")); return; }
    String out; serializeJson(doc, out); sendJson(req, 200, out);
  });

  server.on("/api/packages/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    DataLock lock;
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
    DynamicJsonDocument doc(32768); doc["ok"] = true;
    JsonObject dataObj = doc.createNestedObject("data"); JsonArray arr = dataObj.createNestedArray("packages");
    for (const auto& pkg : packages) { JsonObject p = arr.createNestedObject(); serializePackage(p, pkg); }
    if (doc.overflowed()) { sendJson(req, 507, makeErrorResponse("too_large")); return; }
    String out; serializeJson(doc, out); sendJson(req, 200, out);
  });

  server.on("/api/orders/claim-next-load", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      OrderRecord* selected = nullptr;
      for (auto& order : orders) { if (order.status == "packed") { selected = &order; break; } }
      if (!selected) {
        DynamicJsonDocument outDoc(1024); outDoc["ok"] = true;
        JsonObject dataObj = outDoc.createNestedObject("data"); dataObj["order"] = nullptr; dataObj.createNestedArray("packages");
        String out; serializeJson(outDoc, out); sendJson(req, 200, out); return;
      }
      selected->status = "loading"; selected->updated = nowStamp(); ordersDirty = true;
      addLog("[ORDER] claim load " + selected->orderId);
      emitOrderUpdate(selected->orderId, selected->status);
      sendOrderWithPackages(req, *selected);
    }));

  server.on("/api/orders/update", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      DynamicJsonDocument doc(4096);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String orderId = doc["orderId"] | ""; String status = doc["status"] | "";
      if (orderId.isEmpty() || status.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_fields")); return; }
      OrderRecord* order = findOrderById(orderId);
      if (!order) { sendJson(req, 404, makeErrorResponse("order_not_found")); return; }
      order->status = status; order->updated = nowStamp(); ordersDirty = true;
      addLog("[ORDER] update " + orderId + " -> " + status);
      StaticJsonDocument<1024> evt;
      evt["event"] = "order_updated"; evt["orderId"] = orderId; evt["status"] = status;
      if (doc.containsKey("meta")) evt["meta"] = doc["meta"].as<JsonVariantConst>();
      String out; serializeJson(evt, out); emitEvent(out);
      sendJson(req, 200, makeOkResponse([&](JsonObject data) { data["orderId"] = orderId; data["status"] = status; }));
    }));

  server.on("/api/orders/get", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String orderId = doc["orderId"] | "";
      if (orderId.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_orderId")); return; }
      OrderRecord* order = findOrderById(orderId);
      if (!order) { sendJson(req, 404, makeErrorResponse("order_not_found")); return; }
      DynamicJsonDocument outDoc(8192); outDoc["ok"] = true;
      JsonObject dataObj = outDoc.createNestedObject("data"); JsonObject o = dataObj.createNestedObject("order");
      serializeOrder(o, *order); String out; serializeJson(outDoc, out); sendJson(req, 200, out);
    }));

  server.on("/api/package/register", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      DynamicJsonDocument doc(8192);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      PackageRecord pkg; bool existed = false; String err;
      if (!registerPackageFromJson(doc, pkg, existed, err)) { sendJson(req, 400, makeErrorResponse(err.c_str())); return; }
      if (existed) { PackageRecord* existing = findPackageById(pkg.packageId); if (existing) *existing = pkg; } else { packages.push_back(pkg); }
      packagesDirty = true; addLog("[PKG] registered " + pkg.packageId);
      StaticJsonDocument<1024> evt;
      evt["event"] = "package_registered"; evt["packageId"] = pkg.packageId; evt["orderId"] = pkg.orderId; evt["status"] = pkg.status;
      String out; serializeJson(evt, out); emitEvent(out);
      sendJson(req, 200, makeOkResponse([&](JsonObject data) { data["packageId"] = pkg.packageId; data["orderId"] = pkg.orderId; data["status"] = pkg.status; }));
    }));

  server.on("/api/package/event", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      DynamicJsonDocument doc(2048);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String packageId = doc["packageId"] | ""; String nodeId = doc["nodeId"] | ""; String eventName = doc["event"] | "pass";
      if (packageId.isEmpty() || nodeId.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_fields")); return; }
      PackageRecord* pkg = findPackageById(packageId);
      if (!pkg) { sendJson(req, 404, makeErrorResponse("package_not_found")); return; }
      NodeRecord* node = findNodeById(nodeId);
      if (!node) { sendJson(req, 404, makeErrorResponse("node_not_found")); return; }
      touchNode(*node); nodesDirty = true;
      String err;
      if (!appendPackageEvent(*pkg, node->nodeId, node->nodeName, eventName, err)) { sendJson(req, 500, makeErrorResponse(err.c_str())); return; }
      packagesDirty = true;
      addLog("[PKG] event " + packageId + " @ " + node->nodeId + " " + pkg->lastSeenLabel);
      if (pkg->status == "delivered") {
        // vsechny baliky objednavky doruceny -> objednavka je delivered
        int totalPkgs = 0, deliveredPkgs = 0;
        if (allPackagesForOrderHaveStatus(pkg->orderId, "delivered", totalPkgs, deliveredPkgs)) {
          OrderRecord* ord = findOrderById(pkg->orderId);
          if (ord && ord->status != "delivered") {
            ord->status = "delivered"; ord->updated = nowStamp(); ordersDirty = true;
            addLog("[ORDER] delivered " + ord->orderId);
            emitOrderUpdate(ord->orderId, "delivered");
          }
        }
      }
      emitPackageEventWs(*pkg, eventName, node->nodeId, node->nodeName);
      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["packageId"] = packageId; data["orderId"] = pkg->orderId;
        data["nodeId"] = node->nodeId; data["nodeName"] = node->nodeName;
        data["event"] = eventName; data["status"] = pkg->status;
        data["timeLabel"] = pkg->lastSeenLabel; data["timeIso"] = pkg->lastSeenIso;
        data["timeSynced"] = !pkg->lastSeenIso.isEmpty();
      }));
    }));

  server.on("/api/package/loaded", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      DynamicJsonDocument doc(2048);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String orderId = doc["orderId"] | ""; String packageId = doc["packageId"] | ""; String loaderName = doc["loaderName"] | "Factory Loader";
      if (orderId.isEmpty() || packageId.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_fields")); return; }
      OrderRecord* order = findOrderById(orderId);
      if (!order) { sendJson(req, 404, makeErrorResponse("order_not_found")); return; }
      PackageRecord* pkg = findPackageById(packageId);
      if (!pkg) { sendJson(req, 404, makeErrorResponse("package_not_found")); return; }
      if (pkg->orderId != orderId) { sendJson(req, 400, makeErrorResponse("package_order_mismatch")); return; }
      if (!(order->status == "loading" || order->status == "loaded")) { sendJson(req, 409, makeErrorResponse("order_not_loading")); return; }
      String err;
      if (!appendPackageSystemEvent(*pkg, "LOADER", loaderName, "loaded", "loaded", err)) { sendJson(req, 500, makeErrorResponse(err.c_str())); return; }
      packagesDirty = true;
      int totalPkgs = 0, loadedPkgs = 0;
      bool complete = allPackagesForOrderHaveStatus(orderId, "loaded", totalPkgs, loadedPkgs);
      addLog("[PKG] loaded " + packageId + " " + String(loadedPkgs) + "/" + String(totalPkgs));
      emitPackageEventWs(*pkg, "loaded", "LOADER", loaderName);
      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["orderId"] = orderId; data["packageId"] = packageId; data["status"] = pkg->status;
        data["loaded"] = loadedPkgs; data["expected"] = totalPkgs; data["complete"] = complete;
        data["timeLabel"] = pkg->lastSeenLabel; data["timeIso"] = pkg->lastSeenIso;
      }));
    }));

  server.on("/api/orders/load-complete", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      StaticJsonDocument<512> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String orderId = doc["orderId"] | "";
      if (orderId.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_orderId")); return; }
      OrderRecord* order = findOrderById(orderId);
      if (!order) { sendJson(req, 404, makeErrorResponse("order_not_found")); return; }
      int totalPkgs = 0, loadedPkgs = 0;
      bool complete = allPackagesForOrderHaveStatus(orderId, "loaded", totalPkgs, loadedPkgs);
      if (!complete) {
        sendJson(req, 409, makeOkResponse([&](JsonObject data) {
          data["orderId"] = orderId; data["complete"] = false; data["loaded"] = loadedPkgs; data["expected"] = totalPkgs;
        }));
        return;
      }
      order->status = "loaded"; order->updated = nowStamp(); ordersDirty = true;
      addLog("[ORDER] load complete " + orderId);
      emitOrderUpdate(orderId, "loaded");
      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["orderId"] = orderId; data["status"] = "loaded"; data["complete"] = true;
        data["loaded"] = loadedPkgs; data["expected"] = totalPkgs;
      }));
    }));

  server.on("/api/package/get", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String packageId = doc["packageId"] | "";
      if (packageId.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_packageId")); return; }
      PackageRecord* pkg = findPackageById(packageId);
      if (!pkg) { sendJson(req, 404, makeErrorResponse("package_not_found")); return; }
      DynamicJsonDocument outDoc(12288); outDoc["ok"] = true;
      JsonObject dataObj = outDoc.createNestedObject("data"); JsonObject p = dataObj.createNestedObject("package");
      serializePackage(p, *pkg); String out; serializeJson(outDoc, out); sendJson(req, 200, out);
    }));

  server.on("/api/packages/by-order", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String orderId = doc["orderId"] | "";
      if (orderId.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_orderId")); return; }
      DynamicJsonDocument outDoc(24576); outDoc["ok"] = true;
      JsonObject dataObj = outDoc.createNestedObject("data"); JsonArray arr = dataObj.createNestedArray("packages");
      serializePackagesForOrder(arr, orderId); String out; serializeJson(outDoc, out); sendJson(req, 200, out);
    }));

  // ===== AUTH =====

  server.on("/api/auth/register", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
            DynamicJsonDocument doc(1024);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      UserRecord user;
      String err;
      if (!registerUserFromJson(doc, user, err)) { sendJson(req, 400, makeErrorResponse(err.c_str())); return; }
      users.push_back(user);
      usersDirty = true;
      addLog("[AUTH] registered: " + user.username + " / " + user.mcName);
      sendJson(req, 200, makeOkResponse([&](JsonObject d) {
        d["userId"]       = user.userId;
        d["username"]     = user.username;
        d["mcName"]       = user.mcName;
        d["displayName"]  = user.displayName;
        d["sessionToken"] = user.sessionToken;
      }));
    }));

  server.on("/api/auth/login", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
            DynamicJsonDocument doc(512);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      UserRecord* user = nullptr;
      String err;
      if (!loginUserFromJson(doc, user, err)) { sendJson(req, 401, makeErrorResponse(err.c_str())); return; }
      usersDirty = true;
      addLog("[AUTH] login: " + user->username);
      sendJson(req, 200, makeOkResponse([&](JsonObject d) {
        d["userId"]       = user->userId;
        d["username"]     = user->username;
        d["mcName"]       = user->mcName;
        d["displayName"]  = user->displayName;
        d["sessionToken"] = user->sessionToken;
      }));
    }));

  server.on("/api/auth/verify", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
            StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String token = doc["token"] | "";
      if (token.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_token")); return; }
      UserRecord* user = nullptr;
      if (!verifyToken(token, user)) { sendJson(req, 401, makeErrorResponse("invalid_token")); return; }
      usersDirty = true;
      sendJson(req, 200, makeOkResponse([&](JsonObject d) {
        serializeUser(d, *user, false);
      }));
    }));

  server.on("/api/auth/logout", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
            StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }
      String token = doc["token"] | "";
      UserRecord* user = nullptr;
      if (verifyToken(token, user)) {
        logoutUser(*user);
        usersDirty = true;
        addLog("[AUTH] logout: " + user->username);
      }
      sendJson(req, 200, makeOkResponse([](JsonObject d) { d["ok"] = true; }));
    }));

  server.on("/api/users/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    DataLock lock;
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
    DynamicJsonDocument doc(16384);
    doc["ok"] = true;
    JsonObject dataObj = doc.createNestedObject("data");
    JsonArray arr = dataObj.createNestedArray("users");
    for (const auto& user : users) {
      JsonObject o = arr.createNestedObject();
      serializeUser(o, user, false);
    }
    String out; serializeJson(doc, out);
    sendJson(req, 200, out);
  });

  // ===== PLAYERS =====

  server.on("/api/players/online", HTTP_GET, [](AsyncWebServerRequest* req) {
    DataLock lock;
    sendJson(req, 200, playersOnline);
  });

  server.on("/api/players/online", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      DynamicJsonDocument chk(4096);
      if (deserializeJson(chk, body) || !chk.is<JsonArray>()) { sendJson(req, 400, makeErrorResponse("invalid_players")); return; }
      String list; serializeJson(chk, list);
      playersOnline = list;
      addLog("[PLAYERS] updated");

      // Verejne: remote cekal {"event":"players_online","players":[...]}
      mqttBroadcastPublic("{\"event\":\"players_online\",\"players\":" + list + "}");

      sendJson(req, 200, makeOkResponse([](JsonObject d) { d["updated"] = true; }));
    }));

  // ===== MQTT COMMAND HANDLERS =====
  // These handle commands coming from remote clients via MQTT (axis/cmd topic)
  // They are also reachable via REST for internal use

  server.on("/api/mqtt/get_me", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    bodyHandler([](AsyncWebServerRequest* req, const String& body) {
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
      mqttBroadcastPublic(meStorage);
      sendJson(req, 200, makeOkResponse([](JsonObject d) { d["broadcast"] = true; }));
    }));

  server.onNotFound([](AsyncWebServerRequest* req) { sendJson(req, 404, makeErrorResponse("not_found")); });

  server.begin();
  addLog("[WEB] server started");
}

void webTask(void* pvParameters) {
  (void)pvParameters;
  setupWeb();
  for (;;) { ws.cleanupClients(); vTaskDelay(pdMS_TO_TICKS(100)); }
}
