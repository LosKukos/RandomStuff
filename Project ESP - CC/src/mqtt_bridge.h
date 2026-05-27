#pragma once
#include <Arduino.h>

void initMqttBridge();
void mqttBroadcast(const String& json);
void mqttLoop();
