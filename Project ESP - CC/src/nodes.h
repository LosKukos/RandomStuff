#pragma once

#include "app_state.h"
#include <ArduinoJson.h>

NodeRecord* findNodeById(const String& nodeId);
NodeRecord* findNodeByName(const String& nodeName);
String makeNodeId();
void touchNode(NodeRecord& node);
void serializeNode(JsonObject o, const NodeRecord& node);
bool registerNodeFromJson(JsonDocument& doc, NodeRecord& outNode, bool& existed, String& err);
bool heartbeatNodeFromJson(JsonDocument& doc, NodeRecord*& outNode, String& err);
