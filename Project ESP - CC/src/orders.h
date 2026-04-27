#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "app_state.h"

OrderRecord* findOrderById(const String& orderId);
void serializeOrderItem(JsonObject o, const OrderItem& item);
void serializeOrder(JsonObject o, const OrderRecord& order);
bool createOrderFromJson(JsonDocument& doc, OrderRecord& outOrder, String& err);
