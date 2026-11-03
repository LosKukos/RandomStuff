#include "desktop.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include "config.h"
#include "state.h"
#include "log_util.h"
#include "ntfy.h"
#include "wol.h"
#include "web_server.h"

static bool tcpReachable(const IPAddress &ip, uint16_t port) {
  WiFiClient c;
  const bool ok = c.connect(ip, port, DESKTOP_CHECK_TIMEOUT_MS);
  c.stop();
  return ok;
}

bool wakeDesktop() {
  const bool ok = sendMagicPacket(WOL_TARGET_MAC);
  if (ok && lockMutex(deviceMutex)) {
    desktop.wolSentAtMs = millis() ? millis() : 1;
    unlockMutex(deviceMutex);
  }
  return ok;
}

bool desktopWakeWindowActive() {
  bool active = false;
  if (lockMutex(deviceMutex)) {
    active = desktop.wolSentAtMs && (millis() - desktop.wolSentAtMs < DESKTOP_WAKE_WINDOW_MS);
    unlockMutex(deviceMutex);
  }
  return active;
}

void desktopPoll() {
  if (WiFi.status() != WL_CONNECTED) return;

  IPAddress ip;
  if (!ip.fromString(DESKTOP_IP)) return;

  const bool up = tcpReachable(ip, DESKTOP_PORT_A) || tcpReachable(ip, DESKTOP_PORT_B);

  // debounce: do "online" hned, do "offline" az po 2 po sobe jdoucich neuspechech
  static int offStreak = 0;
  bool changed = false, nowOnline = false, first = false;

  if (lockMutex(deviceMutex)) {
    desktop.lastCheckMs = millis() ? millis() : 1;
    if (!desktop.known) {
      desktop.known = true;
      desktop.online = up;
      first = true;
      offStreak = 0;
    } else if (up) {
      offStreak = 0;
      if (!desktop.online) { desktop.online = true; changed = true; }
    } else {
      if (desktop.online && ++offStreak >= 2) { desktop.online = false; changed = true; offStreak = 0; }
    }
    nowOnline = desktop.online;
    unlockMutex(deviceMutex);
  }

  if (first || changed) {
    logValue(F("[DESKTOP] online: "), nowOnline ? String("true") : String("false"));
    broadcastDashboard();
  }
  if (changed) {
    ntfyPostQuiet(nowOnline ? "Desktop ONLINE" : "Desktop OFFLINE",
                  String("Desktop ") + DESKTOP_IP + (nowOnline ? " je online." : " je offline."));
  }
}
