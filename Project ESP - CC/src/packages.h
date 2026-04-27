#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "app_state.h"

PackageRecord* findPackageById(const String& packageId);
void serializePackage(JsonObject o, const PackageRecord& pkg);
bool registerPackageFromJson(JsonDocument& doc, PackageRecord& outPkg, bool& existed, String& err);
bool appendPackageEvent(PackageRecord& pkg, const String& nodeId, const String& nodeName, const String& eventName, String& err);
