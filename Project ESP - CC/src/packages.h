#pragma once

#include <ArduinoJson.h>
#include "app_state.h"

void serializePackage(JsonObject o, const PackageRecord& pkg);
