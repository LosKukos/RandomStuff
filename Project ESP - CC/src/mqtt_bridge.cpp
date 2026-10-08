#include "mqtt_bridge.h"
#include "app_state.h"
#include "utils.h"
#include "commands.h"
#include "users.h"
#include "orders.h"
#include "packages.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <deque>
#include "sync.h"

#include "secrets.h"   // MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS (mimo git)
#define MQTT_ID    "axis-esp32"

#define TOPIC_OUT       "axis/broadcast"
#define TOPIC_CMD       "axis/cmd"
#define TOPIC_AUTH      "axis/auth"
#define TOPIC_AUTH_RES  "axis/auth/res/"
#define TOPIC_RES       "axis/res/"      // soukrome odpovedi na dotazy: axis/res/<clientId>

#define MQTT_BUF 6144   // i prichozi zpravy: vetsi nez buffer PubSubClient tise zahodi
#define OUTBOX_MAX 16

static WiFiClientSecure tlsClient;
static PubSubClient mqtt(tlsClient);

// MQTT klienta pouziva VYHRADNE commandTask (mqttLoop + callbacky).
// Ostatni tasky jen davaji zpravy do outboxu (mqttBroadcast) - zadne sdileni klienta.
static std::deque<String> outbox;
static SemaphoreHandle_t outboxMutex = nullptr;
static volatile bool mqttUp = false;

static void publishOut(const String& topic, const String& json);

// ===== AUTH HANDLER =====

static void handleAuthMessage(const String& msg) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, msg)) {
    addLog("[MQTT] auth: invalid JSON");
    return;
  }

  String clientId = doc["clientId"] | "";
  String action   = doc["action"]   | "";

  if (clientId.isEmpty()) {
    addLog("[MQTT] auth: missing clientId");
    return;
  }

  String responseTopic = String(TOPIC_AUTH_RES) + clientId;

  if (action == "login") {
    String username = doc["username"] | "";
    String password = doc["password"] | "";

    UserRecord* user = nullptr;
    String err;

    // Reuse loginUserFromJson via manual check
    UserRecord* found = findUserByUsername(username);
    if (!found || found->password != password) {
      StaticJsonDocument<256> res;
      res["ok"]    = false;
      res["error"] = found ? "wrong_password" : "user_not_found";
      String out; serializeJson(res, out);
      mqtt.publish(responseTopic.c_str(), out.c_str());
      addLog("[MQTT] auth: login failed for " + username);
      return;
    }

    found->sessionToken = generateToken();
    found->lastSeen     = millis();
    usersDirty = true;

    StaticJsonDocument<512> res;
    res["ok"]           = true;
    res["userId"]       = found->userId;
    res["username"]     = found->username;
    res["mcName"]       = found->mcName;
    res["displayName"]  = found->displayName;
    res["sessionToken"] = found->sessionToken;
    String out; serializeJson(res, out);
    mqtt.publish(responseTopic.c_str(), out.c_str());
    addLog("[MQTT] auth: login OK " + username);
    return;
  }

  if (action == "players") {
    // Verejne: seznam hracu online pro vyber v registraci (jeste nejsme prihlaseni, token nemame)
    // Jmena, ktera uz maji ucet, se nenabizeji (server je stejne odmitne: mcname_taken)
    String freeList = "[]";
    DynamicJsonDocument pl(4096);
    if (!deserializeJson(pl, playersOnline) && pl.is<JsonArray>()) {
      DynamicJsonDocument res(4096);
      JsonArray arr = res.to<JsonArray>();
      for (JsonVariant v : pl.as<JsonArray>()) {
        const char* n = v.as<const char*>();
        if (!n || !n[0] || findUserByMcName(String(n))) continue;
        arr.add(String(n));
      }
      serializeJson(res, freeList);
    }
    publishOut(responseTopic, String("{\"ok\":true,\"event\":\"players_online\",\"players\":") + freeList + "}");
    return;
  }

  if (action == "register") {
    DynamicJsonDocument regDoc(512);
    regDoc["username"] = doc["username"] | "";
    regDoc["password"] = doc["password"] | "";
    regDoc["mcName"]   = doc["mcName"]   | "";

    UserRecord user;
    String err;

    if (!registerUserFromJson(regDoc, user, err)) {
      StaticJsonDocument<256> res;
      res["ok"]    = false;
      res["error"] = err;
      String out; serializeJson(res, out);
      mqtt.publish(responseTopic.c_str(), out.c_str());
      addLog("[MQTT] auth: register failed: " + err);
      return;
    }

    users.push_back(user);
    usersDirty = true;

    StaticJsonDocument<512> res;
    res["ok"]           = true;
    res["userId"]       = user.userId;
    res["username"]     = user.username;
    res["mcName"]       = user.mcName;
    res["displayName"]  = user.displayName;
    res["sessionToken"] = user.sessionToken;
    String out; serializeJson(res, out);
    mqtt.publish(responseTopic.c_str(), out.c_str());
    addLog("[MQTT] auth: registered " + user.username);
    return;
  }

  addLog("[MQTT] auth: unknown action: " + action);
}

// ===== CMD HANDLER =====
// Odpovedi jdou VZDY soukrome na axis/res/<clientId> (nikdy na broadcast).
// Povolene typy (whitelist): get_me, get_orders_by_user, get_packages_by_user, create_order.

#define MAX_ORDER_ITEMS      30
#define MAX_ITEM_COUNT       10000
#define MAX_OPEN_ORDERS_USER 10

static void replyTo(const String& clientId, const String& json) {
  publishOut(String(TOPIC_RES) + clientId, json);
}

static void replyResult(const String& clientId, const String& type, bool ok, const char* error, const String& orderId = "") {
  StaticJsonDocument<256> r;
  r["event"] = "cmd_result";
  r["type"]  = type;
  r["ok"]    = ok;
  if (error && error[0]) r["error"] = error;
  if (!orderId.isEmpty()) r["orderId"] = orderId;
  String out; serializeJson(r, out);
  replyTo(clientId, out);
}

static void handleCreateOrder(const String& clientId, JsonDocument& doc, UserRecord* user) {
  JsonObjectConst p = doc["payload"].as<JsonObjectConst>();
  if (p.isNull()) { replyResult(clientId, "create_order", false, "invalid_payload"); return; }

  String destination  = p["destination"]  | "";
  String deliveryMode = p["deliveryMode"] | "";
  if (destination.isEmpty() || destination.length() > 64)   { replyResult(clientId, "create_order", false, "invalid_destination");  return; }
  if (deliveryMode.isEmpty() || deliveryMode.length() > 32) { replyResult(clientId, "create_order", false, "invalid_deliveryMode"); return; }

  JsonArrayConst items = p["items"].as<JsonArrayConst>();
  if (items.isNull() || items.size() == 0)        { replyResult(clientId, "create_order", false, "missing_items");  return; }
  if (items.size() > MAX_ORDER_ITEMS)             { replyResult(clientId, "create_order", false, "too_many_items"); return; }

  int open = 0;
  for (const auto& o : orders)
    if (o.ownerId == user->userId && (o.status == "pending" || o.status == "processing")) open++;
  if (open >= MAX_OPEN_ORDERS_USER) { replyResult(clientId, "create_order", false, "too_many_open_orders"); return; }

  // Sestavime cisty dokument - recipient a owner urcuje SERVER (z tokenu), ne klient.
  DynamicJsonDocument od(4096);
  od["destination"]  = destination;
  od["deliveryMode"] = deliveryMode;
  od["recipient"]    = user->mcName;
  JsonArray arr = od.createNestedArray("items");
  for (JsonObjectConst it : items) {
    int count = it["count"] | 0;
    if (count <= 0 || count > MAX_ITEM_COUNT) { replyResult(clientId, "create_order", false, "invalid_item"); return; }
    JsonObject o = arr.createNestedObject();
    o["name"]  = it["name"] | "";
    o["count"] = count;
  }

  OrderRecord order; String err;
  if (!createOrderFromJson(od, order, err)) { replyResult(clientId, "create_order", false, err.c_str()); return; }
  order.ownerId = user->userId;
  orders.push_back(order);
  ordersDirty = true;
  addLog("[ORDER] created " + order.orderId + " by " + user->username + " (MQTT)");

  StaticJsonDocument<256> evt;
  evt["event"] = "order_created"; evt["orderId"] = order.orderId; evt["status"] = order.status;
  String evtOut; serializeJson(evt, evtOut);
  mqttBroadcast(evtOut);   // lokalni WS plny, MQTT jen {"event":"order_created"}

  replyResult(clientId, "create_order", true, nullptr, order.orderId);
}

static void handleCmdMessage(const String& msg) {
  DynamicJsonDocument doc(8192);
  DeserializationError jerr = deserializeJson(doc, msg);
  if (jerr) {
    addLog(String("[MQTT] cmd: invalid JSON (") + jerr.c_str() + ")");
    return;
  }

  String clientId = doc["clientId"] | "";
  String token    = doc["token"]    | "";
  String type     = doc["type"]     | "";

  if (clientId.isEmpty()) { addLog("[MQTT] cmd: missing clientId"); return; }

  UserRecord* user = nullptr;
  if (!verifyToken(token, user)) {
    addLog("[MQTT] cmd: invalid token");
    replyResult(clientId, type, false, "invalid_token");
    return;
  }

  if (type == "get_me") {
    replyTo(clientId, meStorage);
    return;
  }

  if (type == "get_orders_by_user") {
    DynamicJsonDocument outDoc(24576);
    outDoc["event"] = "orders_list";
    JsonArray arr = outDoc.createNestedArray("orders");
    for (const auto& order : orders) {
      if (userOwnsOrder(order, *user)) {
        JsonObject o = arr.createNestedObject();
        serializeOrder(o, order);
      }
    }
    String out; serializeJson(outDoc, out);
    replyTo(clientId, out);
    return;
  }

  if (type == "get_packages_by_user") {
    DynamicJsonDocument outDoc(32768);
    outDoc["event"] = "packages_list";
    JsonArray arr = outDoc.createNestedArray("packages");
    for (const auto& pkg : packages) {
      if (userOwnsPackage(pkg, *user)) {
        JsonObject o = arr.createNestedObject();
        serializePackage(o, pkg);
      }
    }
    String out; serializeJson(outDoc, out);
    replyTo(clientId, out);
    return;
  }

  if (type == "create_order") {
    handleCreateOrder(clientId, doc, user);
    return;
  }

  // Cokoli jineho z internetu se do fronty prikazu pro CC NEDOSTANE.
  addLog("[MQTT] cmd: rejected type '" + type + "' from " + user->username);
  replyResult(clientId, type, false, "unknown_type");
}

// ===== MQTT CALLBACK =====

static void onMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  String t = String(topic);

  if (t == TOPIC_AUTH) {
    DataLock lock;
    handleAuthMessage(msg);
    return;
  }

  if (t == TOPIC_CMD) {
    DataLock lock;
    handleCmdMessage(msg);
    return;
  }
}

// ===== RECONNECT =====

static void reconnect() {
  if (!staConnected || mqtt.connected()) return;

  tlsClient.setInsecure();

  if (mqtt.connect(MQTT_ID, MQTT_USER, MQTT_PASS)) {
    mqtt.subscribe(TOPIC_CMD);
    mqtt.subscribe(TOPIC_AUTH);
    addLog("[MQTT] connected to broker");
  } else {
    addLog("[MQTT] connect failed, rc=" + String(mqtt.state()));
  }
}

// ===== PUBLIC =====

void initMqttBridge() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(MQTT_BUF);
  if (!outboxMutex) outboxMutex = xSemaphoreCreateMutex();
  mqtt.setKeepAlive(30);
  addLog("[MQTT] bridge initialized");
}

static void enqueueOut(const String& json) {
  if (!mqttUp || !outboxMutex) return;    // MQTT neni pripojene -> nic nehromadime
  xSemaphoreTake(outboxMutex, portMAX_DELAY);
  if (outbox.size() >= OUTBOX_MAX) outbox.pop_front();   // zahodit nejstarsi
  outbox.push_back(json);
  xSemaphoreGive(outboxMutex);
}

// Co smi ven na MQTT broadcast: jen "refresh" signaly bez dat. Hledame podretezec,
// takze zadny parsing a zadna pamet. Vse ostatni zustava jen na lokalnim WS.
static bool remoteView(const String& json, String& out) {
  static const char* const EVENTS[] = {"order_created", "order_updated", "package_registered", "package_event"};
  for (const char* ev : EVENTS) {
    if (json.indexOf(String("\"event\":\"") + ev + "\"") >= 0) {
      out = String("{\"event\":\"") + ev + "\"}";
      return true;
    }
  }
  return false;
}

void mqttBroadcast(const String& json) {
  ws.textAll(json);                       // lokalni admin panel: vse
  String r;
  if (remoteView(json, r)) enqueueOut(r); // remote: jen signal
}

void mqttBroadcastPublic(const String& json) {
  ws.textAll(json);
  enqueueOut(json);
}

// Zpravy vetsi nez buffer (ME snapshot, seznamy baliku) se posilaji streamem.
static void publishOut(const String& topic, const String& json) {
  size_t len = json.length();
  if (len + topic.length() + 16 < MQTT_BUF) {
    mqtt.publish(topic.c_str(), json.c_str());
    return;
  }
  if (!mqtt.beginPublish(topic.c_str(), len, false)) {
    addLog("[MQTT] beginPublish failed, len=" + String(len));
    return;
  }
  const uint8_t* p = (const uint8_t*)json.c_str();
  size_t off = 0;
  while (off < len) {
    size_t n = len - off < 512 ? len - off : 512;
    mqtt.write(p + off, n);
    off += n;
  }
  mqtt.endPublish();
}

static void drainOutbox() {
  for (int i = 0; i < 8 && mqttUp; i++) {
    String msg;
    xSemaphoreTake(outboxMutex, portMAX_DELAY);
    if (outbox.empty()) { xSemaphoreGive(outboxMutex); return; }
    msg = outbox.front();
    outbox.pop_front();
    xSemaphoreGive(outboxMutex);
    publishOut(TOPIC_OUT, msg);
  }
}

void mqttLoop() {
  if (!staConnected) { mqttUp = false; return; }
  if (!mqtt.connected()) { mqttUp = false; reconnect(); }
  mqttUp = mqtt.connected();
  if (!mqttUp) return;
  mqtt.loop();
  drainOutbox();
}