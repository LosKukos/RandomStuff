#include "persistence.h"
#include "app_state.h"
#include "utils.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

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

  if (f.size() == 0) {
    addLog("[FS] config.json empty");
    f.close();
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
  StaticJsonDocument<12288> doc;
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& cmd : commandQueue) {
    if (cmd.status == "done" || cmd.status == "failed" || cmd.status == "partial" || cmd.status == "timeout") {
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

  if (f.size() == 0) {
    addLog("[FS] queue.json empty");
    f.close();
    return;
  }

  StaticJsonDocument<12288> doc;
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

  if (f.size() == 0) {
    addLog("[FS] me.json empty");
    f.close();
    return;
  }

  meStorage = f.readString();
  f.close();
  addLog("[FS] me cache loaded");
}
