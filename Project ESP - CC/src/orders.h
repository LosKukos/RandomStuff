#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "app_state.h"

OrderRecord* findOrderById(const String& orderId);
void serializeOrderItem(JsonObject o, const OrderItem& item);
void serializeOrder(JsonObject o, const OrderRecord& order);
bool createOrderFromJson(JsonDocument& doc, OrderRecord& outOrder, String& err);

// Vlastnictvi (pro remote pohled): ownerId ma prednost, jinak shoda recipient s mcName/username.
bool userOwnsOrder(const OrderRecord& order, const UserRecord& user);
bool userOwnsPackage(const PackageRecord& pkg, const UserRecord& user);
