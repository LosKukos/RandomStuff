#include "wol.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "log_util.h"

bool sendMagicPacket(const char *macStr) {
  if (WiFi.status() != WL_CONNECTED) return false;

  unsigned int m[6];
  if (sscanf(macStr, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) {
    logLine(F("[WOL] spatny format MAC (cekam AA:BB:CC:DD:EE:FF)"));
    return false;
  }

  // 6x 0xFF + 16x MAC = 102 bajtu
  uint8_t pkt[102];
  memset(pkt, 0xFF, 6);
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 6; j++) pkt[6 + i * 6 + j] = (uint8_t)m[j];

  WiFiUDP udp;
  bool ok = false;
  const uint16_t ports[] = { 9, 7 };
  for (uint16_t port : ports) {
    if (udp.beginPacket(WiFi.broadcastIP(), port)) {
      udp.write(pkt, sizeof(pkt));
      ok |= (udp.endPacket() == 1);
    }
  }
  logValue(F("[WOL] magic packet -> "), String(macStr) + (ok ? " (odeslano)" : " (SELHALO)"));
  return ok;
}
