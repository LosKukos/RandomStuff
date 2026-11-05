#include "config.h"

// =====================================================
// SECRETS / CONFIG VALUES
// Hodnoty jsou v src/secrets.h, ktery NENI v gitu (.gitignore).
// Vzor: src/secrets.h.example (zkopiruj na secrets.h a vypln).
// =====================================================

#if !__has_include("secrets.h")
#error "Chybi src/secrets.h - zkopiruj src/secrets.h.example na src/secrets.h a vypln WiFi, Discord webhook a role ID."
#endif
#include "secrets.h"

const char* ssid     = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASSWORD;

const char* webhook       = SECRET_DISCORD_WEBHOOK;
const char* discordRoleId = SECRET_DISCORD_ROLE_ID;
