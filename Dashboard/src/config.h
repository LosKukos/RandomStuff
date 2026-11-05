#pragma once
#include <Arduino.h>

// =====================================================
// CONFIG
// =====================================================

extern const char* ssid;
extern const char* password;

extern const char* webhook;
extern const char* discordRoleId;

#define AUTO_SEND_HOUR          0
#define AUTO_SEND_START_MIN     1
#define AUTO_SEND_WINDOW_MIN    15

#define HEARTBEAT_INTERVAL_MS   10000UL
#define WIFI_TIMEOUT_MS         10000UL
#define DEVICE_POLL_INTERVAL_MS 5000UL

#define NTFY_REPEAT_INTERVAL_MS 300000
#define EMERGENCY_TICK_ACTIVE_MS   1000
#define EMERGENCY_TICK_IDLE_MS     2000

#define DASHBOARD_MDNS_NAME     "dash"

#define WLED_POKOJ_HOST         "wled-pokoj"
#define WLED_POKOJ_FALLBACK_IP  "10.0.1.21"

#define MC_HOST                 "mc"
#define MC_FALLBACK_IP          ""
#define MC_STATUS_ENDPOINT      "/api/dash-status"

#define PANIC_NODE_HOST            "panic-button"
#define PANIC_NODE_FALLBACK_IP     ""
#define PANIC_NODE_STATUS_ENDPOINT "/api/dash-status"

// =====================================================
// PINS
// =====================================================

// Onboard RGB LED (WS2812) na ESP32-S3-DevKitC-1: pin dodava Arduino core
// podle varianty desky (GPIO48 u v1.0, GPIO38 u v1.1). Fallback 48.
// Puvodni 27 je na S3 s octal PSRAM nepouzitelny (GPIO26-37 = flash/PSRAM).
#ifndef PIN_NEOPIXEL
#define PIN_NEOPIXEL 48
#endif
#define PIN_BTN_SEND 16
#define PIN_BTN_DIAG 17
#define PIXEL_COUNT  1

// DHT senzory (vnitrni DHT11, venkovni DHT22) - uprav podle zapojeni
#define PIN_DHT_INDOOR  5    // DHT11
#define PIN_DHT_OUTDOOR 4   // DHT22
#define CLIMATE_POLL_INTERVAL_MS 60000UL   // 1 min
// Kolikrat po sobe selze cteni, nez se senzor oznaci jako nevalidni
#define CLIMATE_MAX_FAILS 3
// Napajeni venkovniho DHT22 z GPIO (senzor bere ~1,5 mA, LED s 1-2,2k rezistorem ~1 mA navic).
// Kdyz se senzor zasekne, dashboard na chvili vypne napajeni = automaticke "odpojit/pripojit".
// -1 = vypnuto (senzor napajen napevno). Pozor: ne GPIO 26-37 na S3.
#define PIN_DHT_OUTDOOR_PWR        6
#define DHT_PWR_OFF_MS             2000      // jak dlouho je napajeni vypnute
#define DHT_PWR_SETTLE_MS          2000      // cekani po zapnuti, nez se cte (DHT22 min. interval 2 s)
#define DHT_POWER_CYCLE_FAILS      3         // po kolika selhanich po sobe se zkusi power-cycle
#define DHT_POWER_CYCLE_COOLDOWN_MS 600000UL // min. odstup mezi dvema power-cycly (10 min)
// Hodinovy report na ntfy (https://ntfy.sh/<topic>), posila se v :00-:02 kazde hodiny
#define NTFY_REPORT_TOPIC        "loskukos_dash"
#define NTFY_CMD_TOPIC           "loskukos_cmd"          // prikazy (WOL) + jejich potvrzeni

// Wake-on-LAN: zprava "WOL" v ntfy topicu NTFY_CMD_TOPIC probudi desktop
#define WOL_TARGET_MAC           "74:56:3C:A6:15:33"   // <-- DOPLN MAC desktopu (format AA:BB:CC:DD:EE:FF)
#define NTFY_CMD_POLL_INTERVAL_MS 15000UL              // jak casto ESP kontroluje nove zpravy

// Health check desktopu (online/offline): TCP connect na tyto porty
#define DESKTOP_IP                  "10.0.1.50"
#define DESKTOP_CHECK_INTERVAL_MS   15000UL    // bezne kontroly
#define DESKTOP_CHECK_FAST_MS       3000UL     // po odeslani WOL (rychle zjistit, ze nabehl)
#define DESKTOP_WAKE_WINDOW_MS      180000UL   // jak dlouho po WOL se kontroluje rychle / zobrazuje "probouzim"
#define DESKTOP_CHECK_TIMEOUT_MS    1000
#define DESKTOP_PORT_A              3389       // RDP
#define DESKTOP_PORT_B              445        // SMB
// Trend teploty: okno = CLIMATE_TREND_SAMPLES minut (1 vzorek/min), RAM only
#define CLIMATE_TREND_SAMPLES   60
#define CLIMATE_TREND_MIN_SAMPLES 30   // min. historie, nez se trend ukaze

// =====================================================
// WEATHER (Open-Meteo, no API key needed)
// =====================================================

#define WEATHER_LAT  "49.74"
#define WEATHER_LON  "18.35"
#define WEATHER_POLL_INTERVAL_MS 900000UL   // 15 min
#define WEATHER_FORECAST_HOURS   168
