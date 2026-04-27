#include "nodes.h"
#include "time_service.h"
#include <Arduino.h>

NodeRecord* findNodeById(const String& nodeId) {
  for (auto& node : nodes) {
    if (node.nodeId == nodeId) return &node;
  }
  return nullptr;
}

NodeRecord* findNodeByName(const String& nodeName) {
  for (auto& node : nodes) {
    if (node.nodeName == nodeName) return &node;
  }
  return nullptr;
}

String makeNodeId() {
  uint32_t maxId = 0;

  for (const auto& node : nodes) {
    if (node.nodeId.startsWith("NODE")) {
      uint32_t n = node.nodeId.substring(4).toInt();
      if (n > maxId) maxId = n;
    }
  }

  char buf[16];
  snprintf(buf, sizeof(buf), "NODE%04lu", (unsigned long)(maxId + 1));
  return String(buf);
}

void touchNode(NodeRecord& node) {
  TimeSnapshot t = getTimeSnapshot();
  node.updated = millis();
  node.lastSeenMs = t.uptimeMs;
  node.lastSeenIso = t.iso;
  node.lastSeenLabel = t.label;
}

void serializeNode(JsonObject o, const NodeRecord& node) {
  o["nodeId"] = node.nodeId;
  o["nodeName"] = node.nodeName;
  o["created"] = node.created;
  o["updated"] = node.updated;
  o["lastSeenMs"] = node.lastSeenMs;
  if (!node.lastSeenIso.isEmpty()) o["lastSeenIso"] = node.lastSeenIso;
  if (!node.lastSeenLabel.isEmpty()) o["lastSeenLabel"] = node.lastSeenLabel;

  uint32_t now = millis();
  bool online = node.lastSeenMs > 0 && (now - node.lastSeenMs) < 120000;
  o["online"] = online;
}

bool registerNodeFromJson(JsonDocument& doc, NodeRecord& outNode, bool& existed, String& err) {
  String nodeName = doc["nodeName"] | "";
  String requestedNodeId = doc["nodeId"] | "";

  if (nodeName.isEmpty()) {
    err = "missing_nodeName";
    return false;
  }

  NodeRecord* existing = nullptr;

  if (!requestedNodeId.isEmpty()) {
    existing = findNodeById(requestedNodeId);
  }

  if (!existing) {
    existing = findNodeByName(nodeName);
  }

  existed = existing != nullptr;

  if (existing) {
    outNode = *existing;
    outNode.nodeName = nodeName;
    touchNode(outNode);
    return true;
  }

  outNode.nodeId = makeNodeId();
  outNode.nodeName = nodeName;
  outNode.created = millis();
  touchNode(outNode);

  return true;
}

bool heartbeatNodeFromJson(JsonDocument& doc, NodeRecord*& outNode, String& err) {
  String nodeId = doc["nodeId"] | "";

  if (nodeId.isEmpty()) {
    err = "missing_nodeId";
    return false;
  }

  NodeRecord* node = findNodeById(nodeId);
  if (!node) {
    err = "node_not_found";
    return false;
  }

  String nodeName = doc["nodeName"] | "";
  if (!nodeName.isEmpty()) {
    node->nodeName = nodeName;
  }

  touchNode(*node);
  outNode = node;
  return true;
}
