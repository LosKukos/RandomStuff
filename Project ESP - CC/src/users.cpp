#include "users.h"
#include "time_service.h"
#include "utils.h"
#include <Arduino.h>

// ===== LOOKUP =====

UserRecord* findUserById(const String& userId) {
  for (auto& u : users) { if (u.userId == userId) return &u; }
  return nullptr;
}

UserRecord* findUserByUsername(const String& username) {
  for (auto& u : users) { if (u.username == username) return &u; }
  return nullptr;
}

UserRecord* findUserByMcName(const String& mcName) {
  for (auto& u : users) { if (u.mcName == mcName) return &u; }
  return nullptr;
}

UserRecord* findUserByToken(const String& token) {
  if (token.isEmpty()) return nullptr;
  for (auto& u : users) { if (u.sessionToken == token) return &u; }
  return nullptr;
}

// ===== ID / TOKEN GENERATION =====

String generateToken() {
  static uint32_t counter = 0;
  counter++;
  String raw = "tok_" + String(millis()) + "_" + String(counter) + "_" + String(esp_random());
  // Simple hex encode
  String result = "";
  for (size_t i = 0; i < raw.length() && result.length() < 48; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", (uint8_t)raw[i]);
    result += buf;
  }
  return result;
}

String generateUserId() {
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04x", (unsigned)(esp_random() & 0xFFFF));
  return "USR" + String(nowStamp()) + "_" + String(suffix);
}

// ===== SERIALIZATION =====

void serializeUser(JsonObject o, const UserRecord& user, bool includePrivate) {
  o["userId"]      = user.userId;
  o["username"]    = user.username;
  o["mcName"]      = user.mcName;
  o["displayName"] = user.displayName;
  o["created"]     = user.created;
  o["lastSeen"]    = user.lastSeen;
  o["online"]      = user.sessionToken.length() > 0;

  if (includePrivate) {
    o["sessionToken"] = user.sessionToken;
  }
}

// ===== REGISTER =====

bool registerUserFromJson(JsonDocument& doc, UserRecord& outUser, String& err) {
  String username = doc["username"] | "";
  String password = doc["password"] | "";
  String mcName   = doc["mcName"]   | "";
  String displayName = doc["displayName"] | "";
  if (displayName.isEmpty()) displayName = username;

  if (username.isEmpty()) { err = "missing_username"; return false; }
  if (password.isEmpty()) { err = "missing_password"; return false; }
  if (mcName.isEmpty())   { err = "missing_mcName";   return false; }

  if (username.length() < 3)  { err = "username_too_short";  return false; }
  if (password.length() < 4)  { err = "password_too_short";  return false; }

  // Uniqueness checks
  if (findUserByUsername(username)) { err = "username_taken"; return false; }
  if (findUserByMcName(mcName))     { err = "mcname_taken";   return false; }

  outUser.userId       = generateUserId();
  outUser.username     = username;
  outUser.password     = password;
  outUser.mcName       = mcName;
  outUser.displayName  = displayName;
  outUser.sessionToken = generateToken();
  outUser.created      = millis();
  outUser.lastSeen     = millis();

  return true;
}

// ===== LOGIN =====

bool loginUserFromJson(JsonDocument& doc, UserRecord*& outUser, String& err) {
  String username = doc["username"] | "";
  String password = doc["password"] | "";

  if (username.isEmpty() || password.isEmpty()) { err = "missing_fields"; return false; }

  UserRecord* user = findUserByUsername(username);
  if (!user)                    { err = "user_not_found"; return false; }
  if (user->password != password) { err = "wrong_password";  return false; }

  // Refresh token on each login
  user->sessionToken = generateToken();
  user->lastSeen     = millis();

  outUser = user;
  return true;
}

// ===== VERIFY =====

bool verifyToken(const String& token, UserRecord*& outUser) {
  UserRecord* user = findUserByToken(token);
  if (!user) return false;
  user->lastSeen = millis();
  outUser = user;
  return true;
}

// ===== LOGOUT =====

void logoutUser(UserRecord& user) {
  user.sessionToken = "";
  user.lastSeen     = millis();
}
