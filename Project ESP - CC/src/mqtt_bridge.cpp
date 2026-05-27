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

#define MQTT_HOST  "136587ce4f3f4e959935544be4c8ac46.s1.eu.hivemq.cloud"
#define MQTT_PORT  8883
#define MQTT_USER  "ESP-MC"
#define MQTT_PASS  "Admin123"
#define MQTT_ID    "axis-esp32"

#define TOPIC_OUT       "axis/broadcast"
#define TOPIC_CMD       "axis/cmd"
#define TOPIC_AUTH      "axis/auth"
#define TOPIC_AUTH_RES  "axis/auth/res/"

static WiFiClientSecure tlsClient;
static PubSubClient mqtt(tlsClient);

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
    res["sessionToken"] = found->sessionToken;
    String out; serializeJson(res, out);
    mqtt.publish(responseTopic.c_str(), out.c_str());
    addLog("[MQTT] auth: login OK " + username);
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
    res["sessionToken"] = user.sessionToken;
    String out; serializeJson(res, out);
    mqtt.publish(responseTopic.c_str(), out.c_str());
    addLog("[MQTT] auth: registered " + user.username);
    return;
  }

  addLog("[MQTT] auth: unknown action: " + action);
}

// ===== CMD HANDLER =====

static void handleCmdMessage(const String& msg) {
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, msg)) {
    addLog("[MQTT] cmd: invalid JSON");
    return;
  }

  // Token verification
  String token = doc["token"] | "";
  UserRecord* user = nullptr;
  if (!verifyToken(token, user)) {
    addLog("[MQTT] cmd: invalid token");
    return;
  }
  usersDirty = true;

  String type = doc["type"] | "";
  if (type.isEmpty()) return;

  // ===== DATA REQUEST HANDLERS =====

  if (type == "get_me") {
    // Broadcast current ME snapshot
    mqttBroadcast(meStorage);
    return;
  }

  if (type == "get_orders_by_user") {
    // Broadcast orders where recipient matches user's mcName or username
    DynamicJsonDocument outDoc(24576);
    outDoc["event"] = "orders_list";
    JsonArray arr = outDoc.createNestedArray("orders");
    for (const auto& order : orders) {
      if (order.recipient == user->mcName || order.recipient == user->username) {
        JsonObject o = arr.createNestedObject();
        serializeOrder(o, order);
      }
    }
    String out; serializeJson(outDoc, out);
    mqttBroadcast(out);
    return;
  }

  if (type == "get_packages_by_user") {
    // Broadcast packages for user's orders
    DynamicJsonDocument outDoc(32768);
    outDoc["event"] = "packages_list";
    JsonArray arr = outDoc.createNestedArray("packages");
    for (const auto& pkg : packages) {
      if (pkg.recipient == user->mcName || pkg.recipient == user->username) {
        JsonObject o = arr.createNestedObject();
        serializePackage(o, pkg);
      }
    }
    String out; serializeJson(outDoc, out);
    mqttBroadcast(out);
    return;
  }

  // ===== COMMAND QUEUE =====
  String payloadJson = "{}";
  if (doc["payload"].is<JsonVariantConst>())
    serializeJson(doc["payload"], payloadJson);

  pushCommand(type, payloadJson);
  addLog("[MQTT] cmd: " + type + " by " + user->username);
}

// ===== MQTT CALLBACK =====

static void onMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  String t = String(topic);

  if (t == TOPIC_AUTH) {
    handleAuthMessage(msg);
    return;
  }

  if (t == TOPIC_CMD) {
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
  mqtt.setBufferSize(4096);
  mqtt.setKeepAlive(30);
  addLog("[MQTT] bridge initialized");
}

void mqttBroadcast(const String& json) {
  ws.textAll(json);
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_OUT, json.c_str());
  }
}

void mqttLoop() {
  if (!staConnected) return;
  if (!mqtt.connected()) reconnect();
  mqtt.loop();
}
