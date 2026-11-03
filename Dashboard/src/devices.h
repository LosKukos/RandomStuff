#pragma once
#include <Arduino.h>
#include "state.h"

// =====================================================
// mDNS
// =====================================================

void startMdns();
IPAddress resolveMdnsHost(const char* hostName);

// resolvuje IP zarizeni: mDNS -> cached IP -> staticky fallback.
// pri uplnem selhani oznaci device jako offline a vraci prazdny string.
String resolveDeviceAddress(DeviceState &device, const char* hostName, const char* fallbackIp);

// =====================================================
// WLED
// =====================================================

void pollWLED();
bool wledSetPower(bool on);
bool wledTogglePower();
bool wledSetBrightness(int bri);
bool wledIdentify();

// =====================================================
// CUSTOM ESP POLLING (mc / panic-button)
// =====================================================

void pollCustomEsp(DeviceState &device, const char* hostName, const char* fallbackIp, const char* endpoint);
