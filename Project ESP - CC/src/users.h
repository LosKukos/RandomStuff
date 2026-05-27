#pragma once

#include "app_state.h"
#include <ArduinoJson.h>

UserRecord* findUserById(const String& userId);
UserRecord* findUserByUsername(const String& username);
UserRecord* findUserByMcName(const String& mcName);
UserRecord* findUserByToken(const String& token);

String generateToken();
String generateUserId();

void serializeUser(JsonObject o, const UserRecord& user, bool includePrivate = false);

bool registerUserFromJson(JsonDocument& doc, UserRecord& outUser, String& err);
bool loginUserFromJson(JsonDocument& doc, UserRecord*& outUser, String& err);
bool verifyToken(const String& token, UserRecord*& outUser);
void logoutUser(UserRecord& user);
