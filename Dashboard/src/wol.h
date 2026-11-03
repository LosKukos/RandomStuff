#pragma once
#include <Arduino.h>

// Odesle Wake-on-LAN magic packet (UDP broadcast, port 9) na zadanou MAC
// ve tvaru "AA:BB:CC:DD:EE:FF". Vraci true, pokud se paket odeslal.
bool sendMagicPacket(const char *macStr);
