#include "orders.h"
#include "time_service.h"
#include <algorithm>
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
  o["ownerId"] = order.ownerId;
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
  if (destination.isEmpty()) { err = "missing_destination"; return false; }
  if (deliveryMode.isEmpty()) { err = "missing_deliveryMode"; return false; }
  if (!doc["items"].is<JsonArrayConst>() || doc["items"].as<JsonArrayConst>().size() == 0) { err = "missing_items"; return false; }
  do { outOrder.orderId = genOrderId(); } while (findOrderById(outOrder.orderId));
  outOrder.status = "pending";
  outOrder.destination = destination;
  outOrder.deliveryMode = deliveryMode;
  outOrder.recipient = recipient;
  outOrder.created = nowStamp();
  outOrder.updated = nowStamp();
  for (JsonObjectConst itemObj : doc["items"].as<JsonArrayConst>()) {
    OrderItem item;
    item.name = itemObj["name"] | "";
    item.count = itemObj["count"] | 0;
    item.nbt = itemObj["nbt"] | "";
    item.fingerprint = itemObj["fingerprint"] | "";
    if (item.name.isEmpty() || item.count <= 0) { err = "invalid_item"; return false; }
    outOrder.items.push_back(item);
  }
  return true;
}

bool userOwnsOrder(const OrderRecord& order, const UserRecord& user) {
  if (!order.ownerId.isEmpty()) return order.ownerId == user.userId;
  return order.recipient == user.mcName || order.recipient == user.username;
}

bool userOwnsPackage(const PackageRecord& pkg, const UserRecord& user) {
  if (!pkg.orderId.isEmpty()) {
    OrderRecord* order = findOrderById(pkg.orderId);
    if (order) return userOwnsOrder(*order, user);
  }
  return pkg.recipient == user.mcName || pkg.recipient == user.username;
}

// Drzime jen poslednich KEEP_FINISHED_ORDERS dokoncenych (delivered/failed) objednavek
// vcetne jejich baliku - jinak by orders.json/packages.json casem prekrocily limit dokumentu.
#define KEEP_FINISHED_ORDERS 20

bool pruneFinishedOrders() {
  std::vector<size_t> finished;
  for (size_t i = 0; i < orders.size(); i++) {
    if (orders[i].status == "delivered" || orders[i].status == "failed") finished.push_back(i);
  }
  if (finished.size() <= KEEP_FINISHED_ORDERS) return false;

  std::sort(finished.begin(), finished.end(), [](size_t a, size_t b) { return orders[a].updated < orders[b].updated; });
  size_t toRemove = finished.size() - KEEP_FINISHED_ORDERS;

  std::vector<String> removedIds;
  for (size_t r = 0; r < toRemove; r++) removedIds.push_back(orders[finished[r]].orderId);

  auto isRemoved = [&](const String& id) {
    for (const auto& rid : removedIds) if (rid == id) return true;
    return false;
  };
  orders.erase(std::remove_if(orders.begin(), orders.end(), [&](const OrderRecord& o) { return isRemoved(o.orderId); }), orders.end());
  packages.erase(std::remove_if(packages.begin(), packages.end(), [&](const PackageRecord& p) { return isRemoved(p.orderId); }), packages.end());

  ordersDirty = true; packagesDirty = true;
  addLog("[PRUNE] removed " + String(toRemove) + " finished orders");
  return true;
}
