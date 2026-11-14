#include "camera.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
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

static void pushLocked(uint32_t epoch, bool online) {
  histEpoch[histHead]  = epoch;
  histOnline[histHead] = online;
  histHead = (histHead + 1) % CAMERA_HISTORY_MAX;
  if (histCount < CAMERA_HISTORY_MAX) histCount++;
}

static void recordChangeLocked(bool online) {
  const time_t now = time(nullptr);
  pushLocked((now > 1700000000) ? (uint32_t)now : 0, online);
}

// Persistence: /camera.json = [[epoch, online(0/1)], ...] od nejnovejsi. Zapis jen pri zmene stavu.
#define CAMERA_HISTORY_FILE "/camera.json"

static void loadHistory() {
  if (!LittleFS.exists(CAMERA_HISTORY_FILE)) return;
  File f = LittleFS.open(CAMERA_HISTORY_FILE, "r");
  if (!f) return;
  JsonDocument d;
  const DeserializationError err = deserializeJson(d, f);
  f.close();
  if (err || !d.is<JsonArray>()) { logLine(F("[CAMERA] historie: neplatny soubor, ignoruji")); return; }
  JsonArray a = d.as<JsonArray>();
  if (lockMutex(deviceMutex)) {
    histCount = 0; histHead = 0;
    // soubor je od nejnovejsi, do bufferu se pise od nejstarsi
    const int keep = ((int)a.size() < CAMERA_HISTORY_MAX) ? (int)a.size() : CAMERA_HISTORY_MAX;
    for (int i = keep - 1; i >= 0; i--) {
      JsonArray e = a[i];
      pushLocked(e[0] | 0u, (e[1] | 0) != 0);
    }
    unlockMutex(deviceMutex);
  }
  logValue(F("[CAMERA] historie nactena, zmen: "), histCount);
}

static void persistHistory() {
  uint32_t ep[CAMERA_HISTORY_MAX];
  bool on[CAMERA_HISTORY_MAX];
  const int n = cameraHistory(ep, on, CAMERA_HISTORY_MAX);
  File f = LittleFS.open(CAMERA_HISTORY_FILE, "w");
  if (!f) return;
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonArray e = a.add<JsonArray>();
    e.add(ep[i]);
    e.add(on[i] ? 1 : 0);
  }
  serializeJson(d, f);
  f.close();
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

  static bool historyLoaded = false;
  if (!historyLoaded) { historyLoaded = true; loadHistory(); }

  const bool up = hostAlive(ip, CAMERA_PORT_A, CAMERA_CHECK_TIMEOUT_MS) ||
                  hostAlive(ip, CAMERA_PORT_B, CAMERA_CHECK_TIMEOUT_MS);

  // debounce: do "online" hned, do "offline" az po 2 po sobe jdoucich neuspechech
  static int offStreak = 0;
  bool changed = false, nowOnline = false, first = false, recorded = false;

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
    // po restartu se uvodni stav zapise jen kdyz se lisi od posledniho ulozeneho
    const int newest = (histHead - 1 + CAMERA_HISTORY_MAX) % CAMERA_HISTORY_MAX;
    if (changed || (first && (histCount == 0 || histOnline[newest] != nowOnline))) {
      recordChangeLocked(nowOnline);
      recorded = true;
    }
    unlockMutex(deviceMutex);
  }

  if (recorded) persistHistory();
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
