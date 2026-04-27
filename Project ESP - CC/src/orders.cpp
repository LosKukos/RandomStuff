#include "orders.h"

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
