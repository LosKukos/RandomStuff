#include "packages.h"
#include "time_service.h"
#include "utils.h"

PackageRecord* findPackageById(const String& packageId) {
  for (auto& pkg : packages) {
    if (pkg.packageId == packageId) return &pkg;
  }
  return nullptr;
}

static void setJsonFieldFromString(JsonObject o, const char* key, const String& raw, size_t capacity) {
  if (raw.isEmpty()) return;

  DynamicJsonDocument doc(capacity);
  DeserializationError err = deserializeJson(doc, raw);
  if (!err) {
    o[key] = doc.as<JsonVariantConst>();
  } else {
    String rawKey = String(key) + "_raw";
    o[rawKey] = raw;
  }
}

void serializePackage(JsonObject o, const PackageRecord& pkg) {
  o["packageId"] = pkg.packageId;
  o["orderId"] = pkg.orderId;
  o["address"] = pkg.address;
  o["destination"] = pkg.destination;
  o["deliveryMode"] = pkg.deliveryMode;
  o["recipient"] = pkg.recipient;
  o["status"] = pkg.status;
  o["created"] = pkg.created;
  o["updated"] = pkg.updated;

  if (!pkg.currentNode.isEmpty()) o["currentNode"] = pkg.currentNode;
  if (!pkg.currentNodeName.isEmpty()) o["currentNodeName"] = pkg.currentNodeName;
  if (!pkg.lastEvent.isEmpty()) o["lastEvent"] = pkg.lastEvent;
  if (pkg.lastSeenMs > 0) o["lastSeenMs"] = pkg.lastSeenMs;
  if (!pkg.lastSeenIso.isEmpty()) o["lastSeenIso"] = pkg.lastSeenIso;
  if (!pkg.lastSeenLabel.isEmpty()) o["lastSeenLabel"] = pkg.lastSeenLabel;

  setJsonFieldFromString(o, "contents", pkg.contentsJson, 4096);
  setJsonFieldFromString(o, "filter", pkg.filterJson, 1024);
  setJsonFieldFromString(o, "history", pkg.historyJson, 8192);
}

bool registerPackageFromJson(JsonDocument& doc, PackageRecord& outPkg, bool& existed, String& err) {
  String packageId = doc["packageId"] | "";
  String orderId = doc["orderId"] | "";

  if (packageId.isEmpty() || orderId.isEmpty()) {
    err = "missing_fields";
    return false;
  }

  PackageRecord* existing = findPackageById(packageId);
  existed = existing != nullptr;

  if (existing) outPkg = *existing;

  outPkg.packageId = packageId;
  outPkg.orderId = orderId;
  outPkg.address = doc["address"] | packageId;
  outPkg.destination = doc["destination"] | "";
  outPkg.deliveryMode = doc["deliveryMode"] | "";
  outPkg.recipient = doc["recipient"] | "";
  outPkg.status = doc["status"] | "packed";
  if (outPkg.created == 0) outPkg.created = millis();
  outPkg.updated = millis();

  String contentsJson = "[]";
  if (doc["contents"].is<JsonVariantConst>()) {
    serializeJson(doc["contents"], contentsJson);
  }
  outPkg.contentsJson = contentsJson;

  String filterJson = "{}";
  if (doc["filter"].is<JsonVariantConst>()) {
    serializeJson(doc["filter"], filterJson);
  }
  outPkg.filterJson = filterJson;

  if (outPkg.historyJson.isEmpty()) outPkg.historyJson = "[]";

  return true;
}

bool appendPackageEvent(PackageRecord& pkg, const String& nodeId, const String& nodeName, const String& eventName, String& err) {
  if (pkg.packageId.isEmpty()) {
    err = "invalid_package";
    return false;
  }

  TimeSnapshot t = getTimeSnapshot();

  DynamicJsonDocument histDoc(8192);
  if (pkg.historyJson.isEmpty()) {
    histDoc.to<JsonArray>();
  } else {
    DeserializationError parseErr = deserializeJson(histDoc, pkg.historyJson);
    if (parseErr || !histDoc.is<JsonArray>()) {
      histDoc.clear();
      histDoc.to<JsonArray>();
    }
  }

  JsonArray arr = histDoc.as<JsonArray>();
  JsonObject e = arr.createNestedObject();
  e["event"] = eventName;
  e["nodeId"] = nodeId;
  if (!nodeName.isEmpty()) e["nodeName"] = nodeName;
  e["uptimeMs"] = t.uptimeMs;
  e["timeSynced"] = t.synced;
  if (t.synced) {
    e["epoch"] = (uint32_t)t.epoch;
    e["iso"] = t.iso;
    e["label"] = t.label;
  } else {
    e["label"] = t.label;
  }

  serializeJson(arr, pkg.historyJson);

  pkg.currentNode = nodeId;
  pkg.currentNodeName = nodeName;
  pkg.lastEvent = eventName;
  pkg.lastSeenMs = t.uptimeMs;
  pkg.lastSeenIso = t.iso;
  pkg.lastSeenLabel = t.label;
  pkg.status = "in_transit";
  pkg.updated = millis();

  return true;
}
