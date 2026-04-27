#include "web.h"
#include "app_state.h"
#include "utils.h"
#include "commands.h"
#include "persistence.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

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

      String body;
      body.reserve(len);
      for (size_t i = 0; i < len; i++) body += (char)data[i];

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

      String body;
      body.reserve(len);
      for (size_t i = 0; i < len; i++) body += (char)data[i];

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

      String body;
      body.reserve(len);
      for (size_t i = 0; i < len; i++) body += (char)data[i];

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

      String body;
      body.reserve(len);
      for (size_t i = 0; i < len; i++) body += (char)data[i];

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

      meStorage = "";
      meStorage.reserve(len);
      for (size_t i = 0; i < len; i++) meStorage += (char)data[i];

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
