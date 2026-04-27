#include "packages.h"
#include <ArduinoJson.h>

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

  if (!pkg.contentsJson.isEmpty()) {
    StaticJsonDocument<4096> contentsDoc;
    DeserializationError err = deserializeJson(contentsDoc, pkg.contentsJson);
    if (!err) {
      o["contents"] = contentsDoc.as<JsonVariantConst>();
    } else {
      o["contents_raw"] = pkg.contentsJson;
    }
  }

  if (!pkg.filterJson.isEmpty()) {
    StaticJsonDocument<1024> filterDoc;
    DeserializationError err = deserializeJson(filterDoc, pkg.filterJson);
    if (!err) {
      o["filter"] = filterDoc.as<JsonVariantConst>();
    } else {
      o["filter_raw"] = pkg.filterJson;
    }
  }
}
