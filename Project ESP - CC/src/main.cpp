#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include <algorithm>

// ===================== AP CONFIG =====================
#define AP_SSID "ESP-Setup"
#define AP_PASS "12345678"

// ===================== SERVER =====================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ===================== TASK HANDLES =====================
TaskHandle_t wifiTaskHandle = nullptr;
TaskHandle_t webTaskHandle = nullptr;
TaskHandle_t commandTaskHandle = nullptr;

// ===================== STATE =====================
volatile bool staConnected = false;
String staSsid = "";
String staPass = "";

// ===================== LOG BUFFER =====================
std::vector<String> logs;

void addLog(const String& msg) {
  Serial.println(msg);
  logs.push_back(msg);
  if (logs.size() > 80) {
    logs.erase(logs.begin());
  }
}

// ===================== COMMAND QUEUE =====================
struct Command {
  String id;
  String type;
  String payload;   // serialized JSON payload
  String status;    // queued, sent, accepted, done, failed, timeout
  uint32_t created;
  uint32_t updated;
};

std::vector<Command> commandQueue;

// ===================== ME CACHE =====================
String meStorage = R"({"items":[]})";
uint32_t meLastUpdate = 0;

// ===================== DIRTY FLAGS =====================
bool queueDirty = false;
bool meDirty = false;

// ===================== UTILS =====================
String genId() {
  static uint32_t counter = 0;
  counter++;
  return "cmd_" + String(millis()) + "_" + String(counter);
}

bool isSTA(AsyncWebServerRequest* req) {
  return req->client()->localIP() == WiFi.localIP();
}

bool isAP(AsyncWebServerRequest* req) {
  return req->client()->localIP() == WiFi.softAPIP();
}

Command* findCommandById(const String& id) {
  for (auto& cmd : commandQueue) {
    if (cmd.id == id) return &cmd;
  }
  return nullptr;
}

// ===================== JSON RESPONSE HELPERS =====================
String makeOkResponse(std::function<void(JsonObject)> fill) {
  StaticJsonDocument<768> doc;
  doc["ok"] = true;
  JsonObject data = doc.createNestedObject("data");
  fill(data);

  String out;
  serializeJson(doc, out);
  return out;
}

String makeErrorResponse(const char* err) {
  StaticJsonDocument<256> doc;
  doc["ok"] = false;
  doc["error"] = err;

  String out;
  serializeJson(doc, out);
  return out;
}

// ===================== PERSISTENCE =====================
void saveConfig() {
  StaticJsonDocument<256> doc;
  doc["ssid"] = staSsid;
  doc["pass"] = staPass;

  File f = LittleFS.open("/config.json", "w");
  if (!f) {
    addLog("[FS] failed to open /config.json for write");
    return;
  }

  serializeJson(doc, f);
  f.close();
  addLog("[FS] config saved");
}

void loadConfig() {
  if (!LittleFS.exists("/config.json")) {
    addLog("[FS] config.json not found");
    return;
  }

  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    addLog("[FS] failed to open /config.json for read");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    addLog("[FS] config.json parse failed");
    return;
  }

  staSsid = doc["ssid"] | "";
  staPass = doc["pass"] | "";
  addLog("[FS] config loaded");
}

void saveQueue() {
  StaticJsonDocument<8192> doc;
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& cmd : commandQueue) {
    if (cmd.status == "done" || cmd.status == "failed" || cmd.status == "timeout") {
      continue;
    }

    JsonObject o = arr.createNestedObject();
    o["id"] = cmd.id;
    o["type"] = cmd.type;
    o["payload"] = cmd.payload;
    o["status"] = cmd.status;
    o["created"] = cmd.created;
    o["updated"] = cmd.updated;
  }

  File f = LittleFS.open("/queue.json", "w");
  if (!f) {
    addLog("[FS] failed to open /queue.json for write");
    return;
  }

  serializeJson(doc, f);
  f.close();
  queueDirty = false;
  addLog("[FS] queue saved");
}

void loadQueue() {
  if (!LittleFS.exists("/queue.json")) {
    addLog("[FS] queue.json not found");
    return;
  }

  File f = LittleFS.open("/queue.json", "r");
  if (!f) {
    addLog("[FS] failed to open /queue.json for read");
    return;
  }

  StaticJsonDocument<8192> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    addLog("[FS] queue.json parse failed");
    return;
  }

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
  if (!f) {
    addLog("[FS] failed to open /me.json for write");
    return;
  }

  f.print(meStorage);
  f.close();
  meDirty = false;
  addLog("[FS] me cache saved");
}

void loadME() {
  if (!LittleFS.exists("/me.json")) {
    addLog("[FS] me.json not found");
    return;
  }

  File f = LittleFS.open("/me.json", "r");
  if (!f) {
    addLog("[FS] failed to open /me.json for read");
    return;
  }

  meStorage = f.readString();
  f.close();
  addLog("[FS] me cache loaded");
}

// ===================== COMMAND EMIT =====================
void emitCommandWS(const Command& cmd) {
  StaticJsonDocument<512> doc;
  doc["event"] = "command";
  doc["id"] = cmd.id;
  doc["type"] = cmd.type;

  JsonVariant payloadVariant = doc.createNestedObject("payload");
  DeserializationError err = deserializeJson(payloadVariant, cmd.payload);
  if (err) {
    // fallback when payload isn't valid JSON for some reason
    doc.remove("payload");
    doc["payload_raw"] = cmd.payload;
  }

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

void pushCommand(const String& type, const String& payloadJson) {
  Command cmd;
  cmd.id = genId();
  cmd.type = type;
  cmd.payload = payloadJson;
  cmd.status = "queued";
  cmd.created = millis();
  cmd.updated = millis();

  commandQueue.push_back(cmd);
  queueDirty = true;

  emitCommandWS(commandQueue.back());
  addLog("[CMD] queued " + cmd.id + " type=" + cmd.type);
}

// ===================== WIFI TASK =====================
void wifiTask(void* pvParameters) {
  (void)pvParameters;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  addLog("[WiFi] AP started");
  addLog("[WiFi] AP IP: " + WiFi.softAPIP().toString());

  bool connectInProgress = false;

  for (;;) {
    wl_status_t st = WiFi.status();
    bool connectedNow = (st == WL_CONNECTED);

    if (connectedNow && !staConnected) {
      staConnected = true;
      connectInProgress = false;
      addLog("[WiFi] STA connected: " + WiFi.localIP().toString());
    } else if (!connectedNow && staConnected) {
      staConnected = false;
      addLog("[WiFi] STA disconnected");
    }

    if (!staSsid.isEmpty() && !connectedNow && !connectInProgress) {
      WiFi.begin(staSsid.c_str(), staPass.c_str());
      connectInProgress = true;
      addLog("[WiFi] connecting to STA...");
    }

    if (!connectedNow && connectInProgress) {
      // allow retries later
      connectInProgress = false;
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// ===================== WEB SETUP =====================
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

  // ---------- ROOT ----------
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

  // ---------- STATIC CSS ----------
  server.serveStatic("/css/", LittleFS, "/css/");

  // ---------- AP: SAVE CONFIG ----------
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

  // ---------- AP: DEBUG ----------
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

  // ---------- AP: LOGS ----------
  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAP(req)) {
      req->send(403, "application/json", makeErrorResponse("ap_only"));
      return;
    }

    StaticJsonDocument<4096> doc;
    doc["ok"] = true;
    JsonArray arr = doc.createNestedArray("data");

    for (const auto& line : logs) {
      arr.add(line);
    }

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // ---------- STA: COMMAND ----------
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

      StaticJsonDocument<512> doc;
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

  // ---------- STA: ACK ----------
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

  // ---------- STA: RESULT ----------
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

      StaticJsonDocument<256> doc;
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

      StaticJsonDocument<256> evt;
      evt["event"] = "command_result";
      evt["id"] = id;
      evt["status"] = status;

      String out;
      serializeJson(evt, out);
      ws.textAll(out);

      req->send(200, "application/json", makeOkResponse([&id, &status](JsonObject data) {
        data["id"] = id;
        data["status"] = status;
      }));
    }
  );

  // ---------- STA: ME LIST UPDATE FROM CC ----------
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

  // ---------- STA: ME LIST READ FOR UI ----------
  server.on("/api/me/list", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isSTA(req)) {
      req->send(403, "application/json", makeErrorResponse("sta_only"));
      return;
    }

    req->send(200, "application/json", meStorage);
  });

  // ---------- NOT FOUND ----------
  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "application/json", makeErrorResponse("not_found"));
  });

  server.begin();
  addLog("[WEB] server started");
}

// ===================== WEB TASK =====================
void webTask(void* pvParameters) {
  (void)pvParameters;

  setupWeb();

  for (;;) {
    ws.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ===================== COMMAND TASK =====================
void commandTask(void* pvParameters) {
  (void)pvParameters;

  uint32_t lastQueueSave = 0;
  uint32_t lastMeSave = 0;

  for (;;) {
    uint32_t now = millis();

    // queued -> sent
    for (auto& cmd : commandQueue) {
      if (cmd.status == "queued") {
        cmd.status = "sent";
        cmd.updated = now;
        queueDirty = true;
      }

      if (cmd.status == "sent" && (now - cmd.updated > 5000)) {
        cmd.status = "timeout";
        cmd.updated = now;
        queueDirty = true;
        addLog("[CMD] timeout " + cmd.id);
      }
    }

    // cleanup old finished commands
    commandQueue.erase(
      std::remove_if(commandQueue.begin(), commandQueue.end(),
        [now](const Command& cmd) {
          bool finished = (cmd.status == "done" || cmd.status == "failed" || cmd.status == "timeout");
          return finished && (now - cmd.updated > 60000);
        }),
      commandQueue.end()
    );

    // delayed queue save
    if (queueDirty && (now - lastQueueSave > 2000)) {
      saveQueue();
      lastQueueSave = now;
    }

    // delayed ME save
    if (meDirty && (now - lastMeSave > 5000)) {
      saveME();
      lastMeSave = now;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(300);

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed");
  }

  addLog("[BOOT] starting");

  loadConfig();
  loadQueue();
  loadME();

  xTaskCreatePinnedToCore(
    wifiTask,
    "wifiTask",
    4096,
    nullptr,
    1,
    &wifiTaskHandle,
    0
  );

  xTaskCreatePinnedToCore(
    webTask,
    "webTask",
    8192,
    nullptr,
    1,
    &webTaskHandle,
    1
  );

  xTaskCreatePinnedToCore(
    commandTask,
    "commandTask",
    6144,
    nullptr,
    1,
    &commandTaskHandle,
    1
  );
}

// ===================== LOOP =====================
void loop() {
  vTaskDelay(portMAX_DELAY);
}
