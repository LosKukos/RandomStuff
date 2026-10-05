#pragma once
#include <Arduino.h>

void initMqttBridge();

// Lokalni WebSocket (admin panel na STA IP): dostane VSE beze zmeny.
// MQTT (GitHub Pages): jen sanitizovane "refresh" udalosti ({"event":"order_updated"} apod.), zadna data.
void mqttBroadcast(const String& json);

// Verejna data pro vsechny (ME snapshot, seznam hracu): WS i MQTT beze zmeny.
void mqttBroadcastPublic(const String& json);

void mqttLoop();
