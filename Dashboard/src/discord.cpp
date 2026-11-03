#include "discord.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "state.h"
#include "log_util.h"

String jsonEscape(const String &input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;      break;
    }
  }
  return out;
}

bool sendMessage(const String &msg) {
  if (WiFi.status() != WL_CONNECTED) {
    logLine(F("[WEBHOOK] WiFi not connected"));
    setNeedsUserAttention(true);
    return false;
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  String roleId  = String(discordRoleId);
  String payload = "{";
  payload += "\"content\":\"<@&" + roleId + "> " + jsonEscape(msg) + "\",";
  payload += "\"allowed_mentions\":{\"roles\":[\"" + roleId + "\"]}";
  payload += "}";

  if (!http.begin(client, webhook)) {
    logLine(F("[WEBHOOK] http.begin failed"));
    setNeedsUserAttention(true);
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(payload);
  logValue(F("[WEBHOOK] HTTP code: "), httpCode);

  if (httpCode > 0) logValue(F("[WEBHOOK] response: "), http.getString());
  http.end();

  bool ok = (httpCode >= 200 && httpCode < 300);
  if (!ok) setNeedsUserAttention(true);
  return ok;
}
