#include "camera.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "config.h"
#include "state.h"
#include "log_util.h"
#include "ntfy.h"
#include "web_server.h"

// true = stroj na IP zije: port se otevrel NEBO odpovedel RST (ECONNREFUSED).
// false = zadna odpoved do timeoutu (vypnuto / mimo WiFi).
static bool hostAlive(const IPAddress &ip, uint16_t port, int timeoutMs) {
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return false;
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = (uint32_t)ip;

  bool alive = false;
  const int r = connect(s, (struct sockaddr *)&addr, sizeof(addr));
  if (r == 0) {
    alive = true;
  } else if (errno == EINPROGRESS) {
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    if (select(s + 1, NULL, &wfds, NULL, &tv) > 0) {
      int err = 0;
      socklen_t len = sizeof(err);
      getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
      alive = (err == 0 || err == ECONNREFUSED);
    }
  } else if (errno == ECONNREFUSED) {
    alive = true;
  }
  close(s);
  return alive;
}

// kruhovy buffer zmen stavu (RAM, ztrati se po restartu), chraneno deviceMutex
static uint32_t histEpoch[CAMERA_HISTORY_MAX];
static bool     histOnline[CAMERA_HISTORY_MAX];
static int      histCount = 0, histHead = 0;   // histHead = kam se zapise dalsi

static void recordChangeLocked(bool online) {
  const time_t now = time(nullptr);
  histEpoch[histHead]  = (now > 1700000000) ? (uint32_t)now : 0;
  histOnline[histHead] = online;
  histHead = (histHead + 1) % CAMERA_HISTORY_MAX;
  if (histCount < CAMERA_HISTORY_MAX) histCount++;
}

int cameraHistory(uint32_t *epoch, bool *online, int max) {
  int n = 0;
  if (lockMutex(deviceMutex)) {
    for (int i = 0; i < histCount && n < max; i++) {
      const int idx = (histHead - 1 - i + 2 * CAMERA_HISTORY_MAX) % CAMERA_HISTORY_MAX;
      epoch[n] = histEpoch[idx];
      online[n] = histOnline[idx];
      n++;
    }
    unlockMutex(deviceMutex);
  }
  return n;
}

void cameraPoll() {
  if (WiFi.status() != WL_CONNECTED) return;

  IPAddress ip;
  if (!ip.fromString(CAMERA_IP)) return;

  const bool up = hostAlive(ip, CAMERA_PORT_A, CAMERA_CHECK_TIMEOUT_MS) ||
                  hostAlive(ip, CAMERA_PORT_B, CAMERA_CHECK_TIMEOUT_MS);

  // debounce: do "online" hned, do "offline" az po 2 po sobe jdoucich neuspechech
  static int offStreak = 0;
  bool changed = false, nowOnline = false, first = false;

  if (lockMutex(deviceMutex)) {
    cameraState.lastCheckMs = millis() ? millis() : 1;
    if (!cameraState.known) {
      cameraState.known = true;
      cameraState.online = up;
      first = true;
      offStreak = 0;
    } else if (up) {
      offStreak = 0;
      if (!cameraState.online) { cameraState.online = true; changed = true; }
    } else {
      if (cameraState.online && ++offStreak >= 2) { cameraState.online = false; changed = true; offStreak = 0; }
    }
    nowOnline = cameraState.online;
    if (first || changed) recordChangeLocked(nowOnline);
    unlockMutex(deviceMutex);
  }

  if (first || changed) {
    logLine(String("[CAMERA] ") + CAMERA_IP + (nowOnline ? " online" : " offline"));
    broadcastDashboard();
  }
  if (changed) {
    ntfyPostQuiet(nowOnline ? "Kamera ONLINE" : "Kamera OFFLINE",
                  String(CAMERA_NAME) + " " + CAMERA_IP + (nowOnline ? " je online." : " je offline."),
                  "default", NTFY_CMD_TOPIC);
  }
}
