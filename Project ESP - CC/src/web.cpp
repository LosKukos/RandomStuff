#include "web.h"
#include "app_state.h"
#include "utils.h"
#include "commands.h"
#include "persistence.h"
#include "orders.h"
#include "packages.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static String readBody(uint8_t* data, size_t len) {
  String body;
  body.reserve(len);
  for (size_t i = 0; i < len; i++) body += (char)data[i];
  return body;
}

static void emitOrderEvent(const char* eventName, const String& orderId, const String& status, JsonVariantConst meta = JsonVariantConst()) {
  StaticJsonDocument<1024> evt;
  evt["event"] = eventName;
  evt["orderId"] = orderId;
  evt["status"] = status;
  if (!meta.isNull()) evt["meta"] = meta;
  String out;
  serializeJson(evt, out);
  ws.textAll(out);
}

static void emitPackageEvent(const String& packageId, const String& orderId, const String& status) {
  StaticJsonDocument<1024> evt;
  evt["event"] = "package_registered";
  evt["packageId"] = packageId;
  evt["orderId"] = orderId;
  evt["status"] = status;
  String out;
  serializeJson(evt, out);
  ws.textAll(out);
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

    req->send(500, "application/json", makeErrorResponse("unknown_interface"));
  });

  server.serveStatic("/css/", LittleFS, "/css/");
  server.serveStatic("/js/", LittleFS, "/js/");

  server.on("/api/config", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;

      if (!isAP(req)) {
        req->send(403, "application/json", makeErrorResponse("ap_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String ssid = doc["ssid"] | "";
      String pass = doc["pass"] | "";

      if (ssid.isEmpty()) {
        req->send(400, "application/json", makeErrorResponse("missing_ssid"));
        return;
      }

      staSsid = ssid;
      staPass = pass;
      saveConfig();

      req->send(200, "application/json", makeOkResponse([](JsonObject data) {
        data["saved"] = true;
      }));
    }
  );

  server.on("/api/debug", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAP(req)) {
      req->send(403, "application/json", makeErrorResponse("ap_only"));
      return;
    }

    req->send(200, "application/json", makeOkResponse([](JsonObject data) {
      data["heap"] = ESP.getFreeHeap();
      data["uptime"] = millis();
      data["wifi"] = staConnected;
      data["sta_ip"] = WiFi.localIP().toString();
      data["ap_ip"] = WiFi.softAPIP().toString();
      data["ws"] = ws.count();
      data["queue"] = commandQueue.size();
      data["orders"] = orders.size();
      data["packages"] = packages.size();
      data["me_age"] = meLastUpdate == 0 ? -1 : (int32_t)(millis() - meLastUpdate);
    }));
  });

  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAP(req)) {
      req->send(403, "application/json", makeErrorResponse("ap_only"));
      return;
    }

    StaticJsonDocument<6144> doc;
    doc["ok"] = true;
    JsonArray arr = doc.createNestedArray("data");

    for (const auto& line : logs) {
      arr.add(line);
    }

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isSTA(req)) {
      req->send(403, "application/json", makeErrorResponse("sta_only"));
      return;
    }

    req->send(200, "application/json", makeOkResponse([](JsonObject data) {
      data["wifi"] = staConnected;
      data["queue"] = commandQueue.size();
      data["orders"] = orders.size();
      data["packages"] = packages.size();
      data["me_age"] = meLastUpdate == 0 ? -1 : (int32_t)(millis() - meLastUpdate);
    }));
  });

  server.on("/api/command", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;

      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<768> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String type = doc["type"] | "";
      if (type.isEmpty()) {
        req->send(400, "application/json", makeErrorResponse("missing_type"));
        return;
      }

      String payloadJson = "{}";
      if (doc["payload"].is<JsonVariantConst>()) {
        serializeJson(doc["payload"], payloadJson);
      }

      pushCommand(type, payloadJson);

      const String queuedId = commandQueue.back().id;
      req->send(200, "application/json", makeOkResponse([&queuedId](JsonObject data) {
        data["queued"] = true;
        data["id"] = queuedId;
      }));
    }
  );

  server.on("/api/ack", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;

      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String id = doc["id"] | "";
      if (id.isEmpty()) {
        req->send(400, "application/json", makeErrorResponse("missing_id"));
        return;
      }

      Command* cmd = findCommandById(id);
      if (!cmd) {
        req->send(404, "application/json", makeErrorResponse("command_not_found"));
        return;
      }

      cmd->status = "accepted";
      cmd->updated = millis();
      queueDirty = true;

      req->send(200, "application/json", makeOkResponse([&id](JsonObject data) {
        data["id"] = id;
        data["status"] = "accepted";
      }));
    }
  );

  server.on("/api/result", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;

      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<4096> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String id = doc["id"] | "";
      String status = doc["status"] | "";

      if (id.isEmpty() || status.isEmpty()) {
        req->send(400, "application/json", makeErrorResponse("missing_fields"));
        return;
      }

      Command* cmd = findCommandById(id);
      if (!cmd) {
        req->send(404, "application/json", makeErrorResponse("command_not_found"));
        return;
      }

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

      req->send(200, "application/json", makeOkResponse([&](JsonObject data) {
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
      (void)index;
      (void)total;

      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      meStorage = readBody(data, len);
      meLastUpdate = millis();
      meDirty = true;

      addLog("[ME] storage updated");

      req->send(200, "application/json", makeOkResponse([](JsonObject data) {
        data["updated"] = true;
      }));
    }
  );

  server.on("/api/me/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isSTA(req)) {
      req->send(403, "application/json", makeErrorResponse("sta_only"));
      return;
    }

    req->send(200, "application/json", meStorage);
  });

  server.on("/api/orders/create", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;

      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<8192> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String destination = doc["destination"] | "";
      String deliveryMode = doc["deliveryMode"] | "";
      String recipient = doc["recipient"] | "";

      if (destination.isEmpty()) {
        req->send(400, "application/json", makeErrorResponse("missing_destination"));
        return;
      }

      if (deliveryMode.isEmpty()) {
        req->send(400, "application/json", makeErrorResponse("missing_deliveryMode"));
        return;
      }

      if (!doc["items"].is<JsonArrayConst>() || doc["items"].as<JsonArrayConst>().size() == 0) {
        req->send(400, "application/json", makeErrorResponse("missing_items"));
        return;
      }

      OrderRecord order;
      order.orderId = genOrderId();
      order.status = "pending";
      order.destination = destination;
      order.deliveryMode = deliveryMode;
      order.recipient = recipient;
      order.created = millis();
      order.updated = millis();

      for (JsonObjectConst itemObj : doc["items"].as<JsonArrayConst>()) {
        OrderItem item;
        item.name = itemObj["name"] | "";
        item.count = itemObj["count"] | 0;
        item.nbt = itemObj["nbt"] | "";
        item.fingerprint = itemObj["fingerprint"] | "";

        if (item.name.isEmpty() || item.count <= 0) {
          req->send(400, "application/json", makeErrorResponse("invalid_item"));
          return;
        }

        order.items.push_back(item);
      }

      orders.push_back(order);
      ordersDirty = true;
      addLog("[ORDER] created " + order.orderId);
      emitOrderEvent("order_created", order.orderId, order.status);

      req->send(200, "application/json", makeOkResponse([&](JsonObject data) {
        data["orderId"] = order.orderId;
        data["status"] = order.status;
      }));
    }
  );

  server.on("/api/orders/pending", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)data;
      (void)len;
      (void)index;
      (void)total;

      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      StaticJsonDocument<12288> doc;
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
      req->send(200, "application/json", out);
    }
  );

  server.on("/api/orders/update", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;

      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<4096> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String orderId = doc["orderId"] | "";
      String status = doc["status"] | "";

      if (orderId.isEmpty() || status.isEmpty()) {
        req->send(400, "application/json", makeErrorResponse("missing_fields"));
        return;
      }

      OrderRecord* order = findOrderById(orderId);
      if (!order) {
        req->send(404, "application/json", makeErrorResponse("order_not_found"));
        return;
      }

      order->status = status;
      order->updated = millis();
      ordersDirty = true;
      addLog("[ORDER] update " + orderId + " -> " + status);

      JsonVariantConst meta;
      if (doc.containsKey("meta")) meta = doc["meta"].as<JsonVariantConst>();
      emitOrderEvent("order_updated", orderId, status, meta);

      req->send(200, "application/json", makeOkResponse([&](JsonObject data) {
        data["orderId"] = orderId;
        data["status"] = status;
      }));
    }
  );

  server.on("/api/orders/get", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;
      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String orderId = doc["orderId"] | "";
      OrderRecord* order = findOrderById(orderId);
      if (!order) {
        req->send(404, "application/json", makeErrorResponse("order_not_found"));
        return;
      }

      req->send(200, "application/json", makeOkResponse([&](JsonObject data) {
        JsonObject o = data.createNestedObject("order");
        serializeOrder(o, *order);
      }));
    }
  );

  server.on("/api/package/register", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;

      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<8192> doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String packageId = doc["packageId"] | "";
      String orderId = doc["orderId"] | "";

      if (packageId.isEmpty() || orderId.isEmpty()) {
        req->send(400, "application/json", makeErrorResponse("missing_fields"));
        return;
      }

      PackageRecord* existing = findPackageById(packageId);
      PackageRecord pkg;
      if (existing) pkg = *existing;

      pkg.packageId = packageId;
      pkg.orderId = orderId;
      pkg.address = doc["address"] | packageId;
      pkg.destination = doc["destination"] | "";
      pkg.deliveryMode = doc["deliveryMode"] | "";
      pkg.recipient = doc["recipient"] | "";
      pkg.status = doc["status"] | "packed";
      if (pkg.created == 0) pkg.created = millis();
      pkg.updated = millis();

      String contentsJson = "[]";
      if (doc["contents"].is<JsonVariantConst>()) serializeJson(doc["contents"], contentsJson);
      pkg.contentsJson = contentsJson;

      String filterJson = "{}";
      if (doc["filter"].is<JsonVariantConst>()) serializeJson(doc["filter"], filterJson);
      pkg.filterJson = filterJson;

      if (existing) {
        *existing = pkg;
      } else {
        packages.push_back(pkg);
      }

      packagesDirty = true;
      addLog("[PKG] registered " + packageId);
      emitPackageEvent(packageId, orderId, pkg.status);

      req->send(200, "application/json", makeOkResponse([&](JsonObject data) {
        data["packageId"] = packageId;
        data["orderId"] = orderId;
        data["status"] = pkg.status;
      }));
    }
  );

  server.on("/api/package/get", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;
      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String packageId = doc["packageId"] | "";
      PackageRecord* pkg = findPackageById(packageId);
      if (!pkg) {
        req->send(404, "application/json", makeErrorResponse("package_not_found"));
        return;
      }

      req->send(200, "application/json", makeOkResponse([&](JsonObject data) {
        JsonObject p = data.createNestedObject("package");
        serializePackage(p, *pkg);
      }));
    }
  );

  server.on("/api/packages/by-order", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      (void)index;
      (void)total;
      if (!isSTA(req)) {
        req->send(403, "application/json", makeErrorResponse("sta_only"));
        return;
      }

      String body = readBody(data, len);
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, body)) {
        req->send(400, "application/json", makeErrorResponse("invalid_json"));
        return;
      }

      String orderId = doc["orderId"] | "";
      StaticJsonDocument<12288> outDoc;
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
      req->send(200, "application/json", out);
    }
  );

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "application/json", makeErrorResponse("not_found"));
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
