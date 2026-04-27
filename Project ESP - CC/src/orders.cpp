#include "orders.h"
#include "utils.h"

OrderRecord* findOrderById(const String& orderId) {
  for (auto& order : orders) {
    if (order.orderId == orderId) return &order;
  }
  return nullptr;
}

void serializeOrderItem(JsonObject o, const OrderItem& item) {
  o["name"] = item.name;
  o["count"] = item.count;
  if (!item.nbt.isEmpty()) o["nbt"] = item.nbt;
  if (!item.fingerprint.isEmpty()) o["fingerprint"] = item.fingerprint;
}

void serializeOrder(JsonObject o, const OrderRecord& order) {
  o["orderId"] = order.orderId;
  o["status"] = order.status;
  o["destination"] = order.destination;
  o["deliveryMode"] = order.deliveryMode;
  o["recipient"] = order.recipient;
  o["created"] = order.created;
  o["updated"] = order.updated;

  JsonArray arr = o.createNestedArray("items");
  for (const auto& item : order.items) {
    JsonObject io = arr.createNestedObject();
    serializeOrderItem(io, item);
  }
}

bool createOrderFromJson(JsonDocument& doc, OrderRecord& outOrder, String& err) {
  String destination = doc["destination"] | "";
  String deliveryMode = doc["deliveryMode"] | "";
  String recipient = doc["recipient"] | "";

  if (destination.isEmpty()) {
    err = "missing_destination";
    return false;
  }
  if (deliveryMode.isEmpty()) {
    err = "missing_deliveryMode";
    return false;
  }
  if (!doc["items"].is<JsonArrayConst>() || doc["items"].as<JsonArrayConst>().size() == 0) {
    err = "missing_items";
    return false;
  }

  outOrder.orderId = genOrderId();
  outOrder.status = "pending";
  outOrder.destination = destination;
  outOrder.deliveryMode = deliveryMode;
  outOrder.recipient = recipient;
  outOrder.created = millis();
  outOrder.updated = millis();

  for (JsonObjectConst itemObj : doc["items"].as<JsonArrayConst>()) {
    OrderItem item;
    item.name = itemObj["name"] | "";
    item.count = itemObj["count"] | 0;
    item.nbt = itemObj["nbt"] | "";
    item.fingerprint = itemObj["fingerprint"] | "";

    if (item.name.isEmpty() || item.count <= 0) {
      err = "invalid_item";
      return false;
    }

    outOrder.items.push_back(item);
  }

  return true;
}
