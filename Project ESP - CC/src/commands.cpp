#include "commands.h"
#include "app_state.h"
#include "utils.h"
#include <ArduinoJson.h>

Command* findCommandById(const String& id) {
  for (auto& cmd : commandQueue) {
    if (cmd.id == id) return &cmd;
  }
  return nullptr;
}

void emitCommandWS(const Command& cmd) {
  StaticJsonDocument<1024> doc;
  doc["event"] = "command";
  doc["id"] = cmd.id;
  doc["type"] = cmd.type;

  StaticJsonDocument<512> payloadDoc;
  DeserializationError err = deserializeJson(payloadDoc, cmd.payload);
  if (!err) {
    doc["payload"] = payloadDoc.as<JsonVariantConst>();
  } else {
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
