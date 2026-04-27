#include "web.h"
#include "app_state.h"
#include "utils.h"
#include "commands.h"
#include "persistence.h"
#include "orders.h"
#include "packages.h"
#include "nodes.h"
#include "time_service.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static void sendJson(AsyncWebServerRequest* req, int code, const String& body) {
  req->send(code, "application/json", body);
}

void setupWeb() {
  ws.onEvent([](AsyncWebSocket* server,
                AsyncWebSocketClient* client,
                AwsEventType type,
                void* arg,
                uint8_t* data,
                size_t len) {
    (void)server;
    (void)arg;
    (void)data;
    (void)len;

    if (type == WS_EVT_CONNECT) {
      addLog("[WS] client connected #" + String(client->id()));
    } else if (type == WS_EVT_DISCONNECT) {
      addLog("[WS] client disconnected #" + String(client->id()));
    }
  });

  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (isAP(req)) {
      req->send(LittleFS, "/config.html", "text/html");
      return;
    }

    if (isSTA(req)) {
      req->send(LittleFS, "/index.html", "text/html");
      return;
    }

    sendJson(req, 500, makeErrorResponse("unknown_interface"));
  });

  server.serveStatic("/css/", LittleFS, "/css/");
  server.serveStatic("/js/", LittleFS, "/js/");

  server.on("/api/config", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isAP(req)) { sendJson(req, 403, makeErrorResponse("ap_only")); return; }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      String ssid = doc["ssid"] | "";
      String pass = doc["pass"] | "";
      if (ssid.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_ssid")); return; }

      staSsid = ssid;
      staPass = pass;
      saveConfig();
      sendJson(req, 200, makeOkResponse([](JsonObject data) { data["saved"] = true; }));
    }
  );

  server.on("/api/debug", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAP(req)) { sendJson(req, 403, makeErrorResponse("ap_only")); return; }

    TimeSnapshot t = getTimeSnapshot();
    sendJson(req, 200, makeOkResponse([&](JsonObject data) {
      data["heap"] = ESP.getFreeHeap();
      data["uptime"] = millis();
      data["wifi"] = staConnected;
      data["sta_ip"] = WiFi.localIP().toString();
      data["ap_ip"] = WiFi.softAPIP().toString();
      data["ws"] = ws.count();
      data["queue"] = commandQueue.size();
      data["orders"] = orders.size();
      data["packages"] = packages.size();
      data["nodes"] = nodes.size();
      data["me_age"] = meLastUpdate == 0 ? -1 : (int32_t)(millis() - meLastUpdate);
      data["time_synced"] = t.synced;
      data["time_label"] = t.label;
      data["time_iso"] = t.iso;
    }));
  });

  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAP(req)) { sendJson(req, 403, makeErrorResponse("ap_only")); return; }

    StaticJsonDocument<6144> doc;
    doc["ok"] = true;
    JsonArray arr = doc.createNestedArray("data");
    for (const auto& line : logs) arr.add(line);

    String out;
    serializeJson(doc, out);
    sendJson(req, 200, out);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

    TimeSnapshot t = getTimeSnapshot();
    sendJson(req, 200, makeOkResponse([&](JsonObject data) {
      data["wifi"] = staConnected;
      data["queue"] = commandQueue.size();
      data["orders"] = orders.size();
      data["packages"] = packages.size();
      data["nodes"] = nodes.size();
      data["me_age"] = meLastUpdate == 0 ? -1 : (int32_t)(millis() - meLastUpdate);
      data["time_synced"] = t.synced;
      data["time_label"] = t.label;
      data["time_iso"] = t.iso;
    }));
  });

  server.on("/api/command", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      StaticJsonDocument<768> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      String type = doc["type"] | "";
      if (type.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_type")); return; }

      String payloadJson = "{}";
      if (doc["payload"].is<JsonVariantConst>()) serializeJson(doc["payload"], payloadJson);

      pushCommand(type, payloadJson);
      const String queuedId = commandQueue.back().id;
      sendJson(req, 200, makeOkResponse([&queuedId](JsonObject data) {
        data["queued"] = true;
        data["id"] = queuedId;
      }));
    }
  );

  server.on("/api/ack", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      String id = doc["id"] | "";
      if (id.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_id")); return; }

      Command* cmd = findCommandById(id);
      if (!cmd) { sendJson(req, 404, makeErrorResponse("command_not_found")); return; }

      cmd->status = "accepted";
      cmd->updated = millis();
      queueDirty = true;

      sendJson(req, 200, makeOkResponse([&id](JsonObject data) {
        data["id"] = id;
        data["status"] = "accepted";
      }));
    }
  );

  server.on("/api/result", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      StaticJsonDocument<4096> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      String id = doc["id"] | "";
      String status = doc["status"] | "";
      if (id.isEmpty() || status.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_fields")); return; }

      Command* cmd = findCommandById(id);
      if (!cmd) { sendJson(req, 404, makeErrorResponse("command_not_found")); return; }

      cmd->status = status;
      cmd->updated = millis();
      queueDirty = true;

      StaticJsonDocument<4096> evt;
      evt["event"] = "command_result";
      evt["id"] = id;
      evt["status"] = status;
      if (doc.containsKey("requested")) evt["requested"] = doc["requested"];
      if (doc.containsKey("accepted")) evt["accepted"] = doc["accepted"];
      if (doc.containsKey("reason")) evt["reason"] = doc["reason"];

      if (doc.containsKey("missing") && doc["missing"].is<JsonArrayConst>()) {
        JsonArray dst = evt.createNestedArray("missing");
        for (JsonObjectConst srcItem : doc["missing"].as<JsonArrayConst>()) {
          JsonObject d = dst.createNestedObject();
          d["name"] = srcItem["name"] | "";
          d["displayName"] = srcItem["displayName"] | "";
          d["count"] = srcItem["count"] | 0;
        }
      }

      String out;
      serializeJson(evt, out);
      ws.textAll(out);

      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["id"] = id;
        data["status"] = status;
        if (doc.containsKey("requested")) data["requested"] = doc["requested"];
        if (doc.containsKey("accepted")) data["accepted"] = doc["accepted"];
        if (doc.containsKey("reason")) data["reason"] = doc["reason"];
      }));

      addLog("[CMD] result " + id + " -> " + status);
    }
  );

  server.on("/api/me/list", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      meStorage = readBody(data, len);
      meLastUpdate = millis();
      meDirty = true;
      addLog("[ME] storage updated");
      sendJson(req, 200, makeOkResponse([](JsonObject data) { data["updated"] = true; }));
    }
  );

  server.on("/api/me/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }
    sendJson(req, 200, meStorage);
  });

  server.on("/api/node/register", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      DynamicJsonDocument doc(1024);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      NodeRecord node;
      bool existed = false;
      String err;
      if (!registerNodeFromJson(doc, node, existed, err)) {
        sendJson(req, 400, makeErrorResponse(err.c_str()));
        return;
      }

      if (existed) {
        NodeRecord* existing = findNodeById(node.nodeId);
        if (existing) *existing = node;
      } else {
        nodes.push_back(node);
      }

      nodesDirty = true;
      addLog(String("[NODE] ") + (existed ? "registered existing " : "registered new ") + node.nodeId + " " + node.nodeName);

      StaticJsonDocument<512> evt;
      evt["event"] = "node_registered";
      evt["nodeId"] = node.nodeId;
      evt["nodeName"] = node.nodeName;
      evt["existed"] = existed;
      String wsOut;
      serializeJson(evt, wsOut);
      ws.textAll(wsOut);

      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["nodeId"] = node.nodeId;
        data["nodeName"] = node.nodeName;
        data["existed"] = existed;
        data["lastSeenLabel"] = node.lastSeenLabel;
        data["lastSeenIso"] = node.lastSeenIso;
      }));
    }
  );

  server.on("/api/node/heartbeat", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      DynamicJsonDocument doc(1024);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      NodeRecord* node = nullptr;
      String err;
      if (!heartbeatNodeFromJson(doc, node, err)) {
        sendJson(req, 404, makeErrorResponse(err.c_str()));
        return;
      }

      nodesDirty = true;
      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["nodeId"] = node->nodeId;
        data["nodeName"] = node->nodeName;
        data["lastSeenLabel"] = node->lastSeenLabel;
        data["lastSeenIso"] = node->lastSeenIso;
      }));
    }
  );

  server.on("/api/nodes/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

    DynamicJsonDocument doc(12288);
    doc["ok"] = true;
    JsonObject dataObj = doc.createNestedObject("data");
    JsonArray arr = dataObj.createNestedArray("nodes");

    for (const auto& node : nodes) {
      JsonObject o = arr.createNestedObject();
      serializeNode(o, node);
    }

    String out;
    serializeJson(doc, out);
    sendJson(req, 200, out);
  });

  server.on("/api/orders/create", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      DynamicJsonDocument doc(8192);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      OrderRecord order;
      String err;
      if (!createOrderFromJson(doc, order, err)) {
        sendJson(req, 400, makeErrorResponse(err.c_str()));
        return;
      }

      orders.push_back(order);
      ordersDirty = true;
      addLog("[ORDER] created " + order.orderId);

      StaticJsonDocument<512> evt;
      evt["event"] = "order_created";
      evt["orderId"] = order.orderId;
      evt["status"] = order.status;
      String out;
      serializeJson(evt, out);
      ws.textAll(out);

      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["orderId"] = order.orderId;
        data["status"] = order.status;
      }));
    }
  );

  server.on("/api/orders/pending", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)data; (void)len; (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      DynamicJsonDocument doc(12288);
      doc["ok"] = true;
      JsonObject dataObj = doc.createNestedObject("data");
      JsonArray arr = dataObj.createNestedArray("orders");
      for (const auto& order : orders) {
        if (order.status == "pending") {
          JsonObject o = arr.createNestedObject();
          serializeOrder(o, order);
        }
      }
      String out;
      serializeJson(doc, out);
      sendJson(req, 200, out);
    }
  );

  server.on("/api/orders/update", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      DynamicJsonDocument doc(4096);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      String orderId = doc["orderId"] | "";
      String status = doc["status"] | "";
      if (orderId.isEmpty() || status.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_fields")); return; }

      OrderRecord* order = findOrderById(orderId);
      if (!order) { sendJson(req, 404, makeErrorResponse("order_not_found")); return; }

      order->status = status;
      order->updated = millis();
      ordersDirty = true;
      addLog("[ORDER] update " + orderId + " -> " + status);

      StaticJsonDocument<1024> evt;
      evt["event"] = "order_updated";
      evt["orderId"] = orderId;
      evt["status"] = status;
      if (doc.containsKey("meta")) evt["meta"] = doc["meta"].as<JsonVariantConst>();
      String out;
      serializeJson(evt, out);
      ws.textAll(out);

      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["orderId"] = orderId;
        data["status"] = status;
      }));
    }
  );

  server.on("/api/orders/get", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      String orderId = doc["orderId"] | "";
      if (orderId.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_orderId")); return; }

      OrderRecord* order = findOrderById(orderId);
      if (!order) { sendJson(req, 404, makeErrorResponse("order_not_found")); return; }

      DynamicJsonDocument outDoc(8192);
      outDoc["ok"] = true;
      JsonObject dataObj = outDoc.createNestedObject("data");
      JsonObject o = dataObj.createNestedObject("order");
      serializeOrder(o, *order);
      String out;
      serializeJson(outDoc, out);
      sendJson(req, 200, out);
    }
  );

  server.on("/api/package/register", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      DynamicJsonDocument doc(8192);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      PackageRecord pkg;
      bool existed = false;
      String err;
      if (!registerPackageFromJson(doc, pkg, existed, err)) {
        sendJson(req, 400, makeErrorResponse(err.c_str()));
        return;
      }

      if (existed) {
        PackageRecord* existing = findPackageById(pkg.packageId);
        if (existing) *existing = pkg;
      } else {
        packages.push_back(pkg);
      }

      packagesDirty = true;
      addLog("[PKG] registered " + pkg.packageId);

      StaticJsonDocument<1024> evt;
      evt["event"] = "package_registered";
      evt["packageId"] = pkg.packageId;
      evt["orderId"] = pkg.orderId;
      evt["status"] = pkg.status;
      String out;
      serializeJson(evt, out);
      ws.textAll(out);

      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["packageId"] = pkg.packageId;
        data["orderId"] = pkg.orderId;
        data["status"] = pkg.status;
      }));
    }
  );

  server.on("/api/package/event", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      DynamicJsonDocument doc(2048);
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      String packageId = doc["packageId"] | "";
      String nodeId = doc["nodeId"] | "";
      String eventName = doc["event"] | "pass";

      if (packageId.isEmpty() || nodeId.isEmpty()) {
        sendJson(req, 400, makeErrorResponse("missing_fields"));
        return;
      }

      PackageRecord* pkg = findPackageById(packageId);
      if (!pkg) {
        sendJson(req, 404, makeErrorResponse("package_not_found"));
        return;
      }

      NodeRecord* node = findNodeById(nodeId);
      if (!node) {
        sendJson(req, 404, makeErrorResponse("node_not_found"));
        return;
      }

      touchNode(*node);
      nodesDirty = true;

      String err;
      if (!appendPackageEvent(*pkg, node->nodeId, node->nodeName, eventName, err)) {
        sendJson(req, 500, makeErrorResponse(err.c_str()));
        return;
      }

      packagesDirty = true;
      addLog("[PKG] event " + packageId + " @ " + node->nodeId + " " + pkg->lastSeenLabel);

      StaticJsonDocument<1024> evt;
      evt["event"] = "package_event";
      evt["packageId"] = packageId;
      evt["orderId"] = pkg->orderId;
      evt["nodeId"] = node->nodeId;
      evt["nodeName"] = node->nodeName;
      evt["packageEvent"] = eventName;
      evt["status"] = pkg->status;
      evt["timeLabel"] = pkg->lastSeenLabel;
      evt["timeIso"] = pkg->lastSeenIso;
      String wsOut;
      serializeJson(evt, wsOut);
      ws.textAll(wsOut);

      sendJson(req, 200, makeOkResponse([&](JsonObject data) {
        data["packageId"] = packageId;
        data["orderId"] = pkg->orderId;
        data["nodeId"] = node->nodeId;
        data["nodeName"] = node->nodeName;
        data["event"] = eventName;
        data["status"] = pkg->status;
        data["timeLabel"] = pkg->lastSeenLabel;
        data["timeIso"] = pkg->lastSeenIso;
        data["timeSynced"] = !pkg->lastSeenIso.isEmpty();
      }));
    }
  );

  server.on("/api/package/get", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      String packageId = doc["packageId"] | "";
      if (packageId.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_packageId")); return; }

      PackageRecord* pkg = findPackageById(packageId);
      if (!pkg) { sendJson(req, 404, makeErrorResponse("package_not_found")); return; }

      DynamicJsonDocument outDoc(12288);
      outDoc["ok"] = true;
      JsonObject dataObj = outDoc.createNestedObject("data");
      JsonObject p = dataObj.createNestedObject("package");
      serializePackage(p, *pkg);
      String out;
      serializeJson(outDoc, out);
      sendJson(req, 200, out);
    }
  );

  server.on("/api/packages/by-order", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index; (void)total;
      if (!isSTA(req)) { sendJson(req, 403, makeErrorResponse("sta_only")); return; }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) { sendJson(req, 400, makeErrorResponse("invalid_json")); return; }

      String orderId = doc["orderId"] | "";
      if (orderId.isEmpty()) { sendJson(req, 400, makeErrorResponse("missing_orderId")); return; }

      DynamicJsonDocument outDoc(24576);
      outDoc["ok"] = true;
      JsonObject dataObj = outDoc.createNestedObject("data");
      JsonArray arr = dataObj.createNestedArray("packages");

      for (const auto& pkg : packages) {
        if (pkg.orderId == orderId) {
          JsonObject p = arr.createNestedObject();
          serializePackage(p, pkg);
        }
      }

      String out;
      serializeJson(outDoc, out);
      sendJson(req, 200, out);
    }
  );

  server.onNotFound([](AsyncWebServerRequest* req) {
    sendJson(req, 404, makeErrorResponse("not_found"));
  });

  server.begin();
  addLog("[WEB] server started");
}

void webTask(void* pvParameters) {
  (void)pvParameters;

  setupWeb();

  for (;;) {
    ws.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
