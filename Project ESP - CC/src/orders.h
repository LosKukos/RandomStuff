#pragma once

#include <ArduinoJson.h>
#include "app_state.h"

void serializeOrderItem(JsonObject o, const OrderItem& item);
void serializeOrder(JsonObject o, const OrderRecord& order);
