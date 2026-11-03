# ESP32 Dashboard (V1 / Legacy) – architektura

Network-ops control panel pro domácí síť: sleduje stav ESPček (WLED,
Minecraft controller, panic-button), řeší denní hlášku "Dnes má
svátek..." na Discord a panic/SOS alarm s ntfy notifikacemi.

Tento dokument popisuje stav **po refaktoru** z jednoho `main.cpp`
(1465 řádků) do modulů. Logika je 1:1 stejná jako v původní verzi,
jen rozdělená + s vylepšenou task architekturou (viz [Emergency mode](#emergency-mode)).

---

## Přehled modulů

| Soubor | Co řeší |
|---|---|
| `config.h/.cpp` | WiFi credentials, Discord webhook, piny, konstanty/intervaly. **Tady jsou natvrdo secrets** – OK pro priv repo. |
| `state.h/.cpp` | Veškerý globální stav: mutexy, `DeviceState`, `AlarmState`, gettery/settery pro flagy (`netReady`, `timeReady`, ...). Jediné místo, kde se sahá na globální proměnné přímo. |
| `log_util.h/.cpp` | `logLine`/`logValue`/`printDivider` – mutex-protected Serial výstup. |
| `led.h/.cpp` | NeoPixel stavová LED (zelená/modrá/červená podle `state.h` flagů). |
| `discord.h/.cpp` | `sendMessage()` – POST na Discord webhook s role mention. |
| `ntfy.h/.cpp` | `sendNtfyPanic()` – SOS push na ntfy.sh. |
| `time_svatky.h/.cpp` | NTP, čtení `/names.json` (jmeniny), `/state.json` (last_sent), `shouldAutoSend()`, `doManualSend()`. |
| `devices.h/.cpp` | mDNS resolve s fallbackem, WLED polling/ovládání, polling vlastních ESPček (mc, panic-button). |
| `wifi_setup.h/.cpp` | `scanWifi()`, `connectWiFi()`. |
| `web_server.h/.cpp` | AsyncWebServer routy, websocket (`/ws`), `buildDashboardJson()`, `/api/panic` handler. |
| `diagnostics.h/.cpp` | `printDiagnostics()`, `doDiagnostics()` (DIAG tlačítko), `heartbeat()`. |
| `tasks.h/.cpp` | ISR pro tlačítka, 4 statické RTOS tasky (viz níže). |
| `main.cpp` | Jen `setup()` a `loop()` – orchestrace, žádná byznys logika. |

**Pravidlo:** globální proměnné ze `state.h` se nikdy nečtou/nepíšou
přímo odjinud – vždy přes `getX()`/`setX()`, které si hlídají
`stateMutex`/`deviceMutex`/`alarmMutex`. Když potřebuješ novou
hodnotu stavu, přidej pár getter/setter do `state.h/.cpp`, ne novou
globálku do jiného souboru.

---

## RTOS tasky

Vše běží na **core 1** (core 0 je vyhrazený WiFi/BT stacku), staticky
alokované (`xTaskCreateStaticPinnedToCore` – žádné heap fragmentace
z task creation).

| Task | Stack | Priorita | Interval | Co dělá |
|---|---|---|---|---|
| `WledPollTask` | 4096 B | 1 | 5 s | poll `/json/info` + `/json/state` z WLED |
| `EspPollTask` | 4096 B | 1 | 5 s | poll `mc.local` a `panic-button.local` (`/api/dash-status`) |
| `AutoLogicTask` | 8192 B | 1 | 60 s | NTP refresh, kontrola okna 00:01–00:15, denní Discord hláška |
| `EmergencyTask` | 8192 B | **2** (vyšší) | 1–2 s | viz Emergency mode níže |

Plus hlavní `loop()` (Arduino task, core 1): websocket cleanup,
zpracování tlačítek (SEND/DIAG), heartbeat log co 10 s, LED update.

**Proč rozdělené pollery:** původně jeden task dělal WLED + mc +
panic-button sekvenčně (až ~4.5 s blokování na 5s cyklus). Teď pomalá/
nedostupná WLED nezdrží monitoring panic-buttonu a naopak.

---

## Emergency mode

Cíl: když přijde SOS z panic-buttonu, dashboard se má **soustředit
jen na to** – žádný zbytečný polling/HTTP provoz, jen opakované
notifikace, dokud to někdo fyzicky neresetuje.

### Flow

1. Panic-button POSTne `/api/panic` s `type=panic`.
2. `web_server.cpp` handler nastaví `activeAlarm.active = true`,
   uloží eventId/deviceId/deviceName, hned odešle **první** ntfy
   notifikaci a broadcastne stav přes websocket.
3. Frontend (`app.js`/`style.css`) na `alarm.active === true`
   zobrazí fullscreen červený lockscreen overlay – uživatel nevidí
   normální dashboard, jen "SOS" kartu.
4. `EmergencyTask` na dalším ticku zjistí `active == true` poprvé a:
   - `vTaskSuspend()` na `WledPollTask`, `EspPollTask`, `AutoLogicTask`
   - přebírá repeat ntfy notifikace (každých `NTFY_REPEAT_INTERVAL_MS`
     = 5 min), čítá `ntfyCount`
   - zrychlí svůj vlastní tick na 1 s (jinak idle 2 s)
5. Reset přijde **fyzicky z panic-button node** (mimo tento ESP) jako
   `/api/panic` s `type=panic_reset` → `activeAlarm.active = false`.
6. `EmergencyTask` na to zareaguje: `vTaskResume()` na všechny tři
   tasky, vrátí se do idle tiku.

### Co zůstává funkční během emergency

- **Web server běží dál** (AsyncWebServer/AsyncTCP má vlastní
  kontext, není to žádný z výše suspendovaných tasků) → `/api/status`,
  `/api/panic` (reset), websocket – vše live.
- Hlavní `loop()` (tlačítka, heartbeat, websocket cleanup) běží dál.

### Co je suspendnuté

- WLED/ESP polling → dashboard během emergency ukazuje **stale data**
  pro tyto zařízení (poslední známý stav před alarmem). To je
  záměr – jakmile emergency skončí, polling se obnoví a data se
  doplní během pár sekund.
- Denní svátková hláška – pokud by emergency padla přesně do
  00:01–00:15 okna, hláška se odešle až po resolvnutí (při dalším
  běhu `AutoLogicTask`u v rámci stejné minuty/okna, pokud okno ještě
  trvá).

---

## Datový tok / JSON přes websocket

`buildDashboardJson(true)` se posílá:
- při connectu nového websocket klienta
- po každém pollu zařízení (`WledPollTask`, `EspPollTask`)
- po `AutoLogicTask` akci, manual sendu, diag, panic eventu, WLED
  ovládání

Struktura (`{"type":"devices", ...}`):

```jsonc
{
  "type": "devices",
  "devices": [ /* wledPokoj, mcEsp, panicNode */ ],
  "alarm": { "active": false, "eventId": "...", ... },
  "system": {
    "ip": "...", "wifiStatus": "connected", "heap": 123456,
    "netReady": true, "timeReady": true, "fatalError": false,
    "needsUserAttention": false,
    "currentFullDate": "2026-06-13", "currentNames": "Antonín",
    "lastSent": "2026-06-13", ...
  }
}
```

---

## LED stavy

| Barva | Význam |
|---|---|
| 🟢 Zelená | vše OK |
| 🔵 Modrá | `needsUserAttention` – něco čeká na ruční zásah (failed send, NTP fail, FS error...) |
| 🔴 Červená | `fatalError` – LittleFS mount fail nebo WiFi timeout při bootu |

---

## Endpointy (`/api/*`)

| Endpoint | Metoda | Co dělá |
|---|---|---|
| `/api/status`, `/api/devices` | GET | snapshot `buildDashboardJson` |
| `/api/diagnostics` | GET | vypíše diagnostiku do Serial + snapshot |
| `/api/refresh` | POST | force-poll všech zařízení |
| `/api/panic` | POST | příjem panic / panic_reset eventu |
| `/api/send` | POST | manuální odeslání svátkové hlášky |
| `/api/update-time` | POST | force NTP refresh |
| `/api/reset-last-sent` | POST | vynuluje `last_sent` (pro testování auto-send) |
| `/api/wled/{toggle,on,off,brightness,identify}` | POST | ovládání WLED |

---

## Známé limity / co řešit ve V2

- WiFi credentials a Discord webhook natvrdo v `config.cpp` – OK pro
  priv repo, ale při sdílení/refaktoru do veřejného repa přesunout do
  `secrets.h` v `.gitignore`.
- `MC_FALLBACK_IP` a `PANIC_NODE_FALLBACK_IP` jsou prázdné – pokud
  mDNS selže a není cached IP, zařízení spadne do "offline" bez
  fallbacku. Funguje to, jen to zmiňuju, ať víš proč se to někdy
  ukáže offline po čerstvém bootu.
- SolaX/heat pump/kamery a další V2 věci sem záměrně nepatří – tohle
  zůstává čistě "network ops + svátky + panic" legacy panel.
