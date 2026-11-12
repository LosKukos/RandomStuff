#include "config.h"

// =====================================================
// SECRETS / CONFIG VALUES
// Hodnoty jsou v src/secrets.h, ktery NENI v gitu (.gitignore).
// Soubor vytvor sam, potrebuje 4 defines: SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD,
// SECRET_DISCORD_WEBHOOK, SECRET_DISCORD_ROLE_ID (kazdy jako retezec v uvozovkach).
// =====================================================

#if !__has_include("secrets.h")
#error "Chybi src/secrets.h - vytvor ho a definuj SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD, SECRET_DISCORD_WEBHOOK, SECRET_DISCORD_ROLE_ID."
#endif
#include "secrets.h"

const char* ssid     = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASSWORD;

const char* webhook       = SECRET_DISCORD_WEBHOOK;
const char* discordRoleId = SECRET_DISCORD_ROLE_ID;
