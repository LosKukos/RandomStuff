# ESP32 Dashboard (Dashboard climate V1) – architektura

Domácí dashboard na ESP32-S3: network-ops panel (WLED, Minecraft controller, panic-button), denní svátková hláška na Discord, SOS/panic alarm, klima (DHT11/DHT22), IR ovládání přímotopu, počasí + měsíc, hodinový report na ntfy a Wake-on-LAN + health check desktopu.

Stav k 2026-11-13. Aktuální rozpracované věci a pravidla pro práci jsou v [HANDOFF.md](HANDOFF.md), úkoly v [To-Do.md](To-Do.md), tento dokument popisuje strukturu kódu. IR ovládání topení: [ir.md](ir.md), RFID čtečka: [rfid.md](rfid.md).

* * *

## Platforma

- ESP32-S3 (`esp32-s3-devkitc-1`, `qio_opi`, 8 MB PSRAM), PlatformIO + Arduino, jeden env `[env:esp32s3]`, LittleFS.
- Knihovny: AsyncTCP, ESPAsyncWebServer, ArduinoJson v7, Adafruit NeoPixel, PubSubClient, IRremoteESP8266 (DHT čtení je vlastní, knihovny DHT sensor library / Adafruit Unified Sensor zůstávají v `platformio.ini`, ale nepoužívají se).
- GPIO 26–37 jsou na S3 s octal PSRAM nepoužitelné (flash/PSRAM).
- UI se nahrává do LittleFS (`data/` → `pio run -t uploadfs`).

## Struktura repa

```
Dashboard/
├── platformio.ini
├── src/                 firmware (moduly níže)
├── data/                UI + data na LittleFS: index.html, app.js, style.css, favicon.ico, names.json
├── remote_setup.bat     ZeroTier port forward (spouštět jako admin)
├── remote_teardown.bat  zrušení port forwardu
└── docs/                veškerá dokumentace (.md)
    ├── ARCHITECTURE.md  tento soubor
    ├── HANDOFF.md       stav + pravidla pro nový chat
    ├── To-Do.md         úkoly
    ├── ir.md            IR kódy ovladače topení, zapojení, API
    └── rfid.md          RC522 čtečka, tagy, zapojení
```

Za běhu vznikají na LittleFS ještě `/state.json` (`last_sent`), `/watch.json` (sledovaná jména) a `/heater.json` (odhad stavu topení, cíl a čas AUTO).

* * *

## Moduly (`src/`)

| Soubor               | Co řeší                                                                                                                                  |
|----------------------|------------------------------------------------------------------------------------------------------------------------------------------|
| `config.h/.cpp`      | Defines (piny, intervaly, hosty, topicy, souřadnice) v `.h`; WiFi, Discord webhook a role ID se do `.cpp` berou ze `src/secrets.h` (není v gitu, vzor neexistuje: defines `SECRET_WIFI_SSID`, `SECRET_WIFI_PASSWORD`, `SECRET_DISCORD_WEBHOOK`, `SECRET_DISCORD_ROLE_ID`).                    |
| `state.h/.cpp`       | Mutexy, boot counter (RTC), flagy s get/set, struktury `DeviceState`, `AlarmState`, `WeatherState`, `ClimateState`, `DesktopState`.      |
| `log_util.h/.cpp`    | `logLine`/`logValue`/`printDivider` – Serial výstup chráněný `serialMutex`.                                                              |
| `breadcrumbs.h/.cpp` | RTC breadcrumbs: `bcMark()` kroky tasků, heap/stack snapshot, hodinový vzorek heapu, souhrn předchozího běhu (viz [RTC breadcrumbs](#rtc-breadcrumbs)). |
| `led.h/.cpp`         | Onboard NeoPixel: stavová LED (viz [LED stavy](#led-stavy)).                                                                             |
| `wifi_setup.h/.cpp`  | `scanWifi()`, `connectWiFi()` (timeout `WIFI_TIMEOUT_MS`).                                                                               |
| `devices.h/.cpp`     | mDNS (`dash.local`), resolve s cache/fallbackem, polling WLED a vlastních ESP (mc, panic-button), ovládání WLED.                         |
| `time_svatky.h/.cpp` | NTP + TZ, `/names.json` (jmeniny), `/state.json` (last_sent), `shouldAutoSend()`, `doManualSend()`.                                      |
| `discord.h/.cpp`     | `sendMessage()` – POST na webhook s role mention.                                                                                        |
| `ntfy.h/.cpp`        | `sendNtfyPanic()` (SOS), `sendNtfyReport()` (hodinový report, ukládá poslední kód), `ntfyPostQuiet()` (potvrzení, ONLINE/OFFLINE).      |
| `ntfy_cmd.h/.cpp`    | Poll příkazového topicu (`NTFY_CMD_TOPIC`), zatím jediný příkaz `WOL`.                                                                   |
| `wol.h/.cpp`         | `sendMagicPacket()` – UDP broadcast na porty 9 a 7.                                                                                      |
| `camera.h/.cpp`      | Health check IP kamery Xiaomi C302 (`CAMERA_IP`, TCP connect, RST = online), debounce, ntfy ONLINE/OFFLINE na `NTFY_CMD_TOPIC`, historie změn stavu (RAM, 20 položek), rozklikávací karta.            |
| `desktop.h/.cpp`     | `wakeDesktop()`, health check desktopu (TCP connect), debounce, ntfy ONLINE/OFFLINE.                                                     |
| `sensors.h/.cpp`     | DHT11 (indoor) + DHT22 (outdoor) přes vlastní čtečku s omezenými timeouty, validace po sobě jdoucích selhání, kruhový buffer pro trend.   |
| `heater.h/.cpp`, `heater_logic.h` | IR ovládání přímotopu (viz [Topení](#topení-ir)); `heater_logic.h` je čistá logika bez HW (testovatelná na PC).                |
| `weather.h/.cpp`     | Open-Meteo (bez API klíče), WMO kód → ikona/popis, východ/západ, fáze měsíce počítaná lokálně.                                           |
| `diagnostics.h/.cpp` | `printDiagnostics()`, `doDiagnostics()`, `heartbeat()`, `resetReasonStr()`.                                                              |
| `web_server.h/.cpp`  | AsyncWebServer routy, websocket `/ws`, `buildDashboardJson()`, `broadcastDashboard()`.                                                   |
| `tasks.h/.cpp`       | ISR pro tlačítka, 8 statických RTOS tasků, `requestRefreshAll()`, `requestNtfyTest()`.                                                   |
| `main.cpp`           | Jen `setup()` a `loop()`.                                                                                                                |

* * *

## Boot (`setup()`)

1. Serial, `initMutexes()`, `bcInit()` (načte breadcrumbs z předchozího běhu a vynuluje RTC pro tento běh), `bootCounter = bcBootCount()` (počítadlo startů v `RTC_NOINIT`, přežije soft reset i WDT, ne výpadek napájení), LED, tlačítka (GPIO 16 SEND / 17 DIAG, `INPUT_PULLUP`, přerušení na FALLING). Po výpisu boot informací `bcPrintPrev()` vypíše souhrn předchozího běhu.
2. Mount LittleFS. Při selhání: `fatalError`, červená LED a `setup()` skončí – bez WiFi, serveru i tasků.
3. `scanWifi()` → `connectWiFi()`. Timeout nastaví `fatalError` a `netReady=false`.
4. Pokud WiFi běží: mDNS (`dash.local`).
5. `startDashboardServer()`.
6. Pokud `netReady`: `setupTime()` (NTP, až 20 × 500 ms, TZ `CET-1CEST,M3.5.0/2,M10.5.0/3`) + načtení `last_sent`.
7. `startBackgroundTasks()` (nejdřív `initSensors()`), první `broadcastDashboard()`.

`loop()` (Arduino task, core 1) dělá jen: `ws.cleanupClients()`, zpracování tlačítek (SEND → `doManualSend`, DIAG → `doDiagnostics`), heartbeat log + `bcHeartbeat()` + broadcast každých 10 s, update LED, `delay(20)`. Tlačítka zatím nejsou fyzicky zapojená.

* * *

## Stav a mutexy

| Mutex           | Chrání                                                              |
|-----------------|---------------------------------------------------------------------|
| `serialMutex`   | Serial výstup                                                       |
| `ledMutex`      | NeoPixel                                                            |
| `stateMutex`    | skalární flagy, `timeinfo`, řetězce svátků (`currentNames`, `lastSent`, …) |
| `deviceMutex`   | `wledPokoj`, `mcEsp`, `panicNode` a `desktop`                       |
| `alarmMutex`    | `activeAlarm`                                                       |
| `weatherMutex`  | `weather`                                                           |
| `climateMutex`  | `climate`                                                           |

`lockMutex()` má default timeout 1 s a mutexy nejsou rekurzivní (get/set funkce si berou `stateMutex` samy, takže se nesmí volat uvnitř jeho bloku).

**Pravidlo:** skalární flagy a řetězce svátků se čtou/píšou jen přes `getX()`/`setX()`. Strukturované stavy (`DeviceState`, `AlarmState`, `WeatherState`, `ClimateState`, `DesktopState`) jsou `extern` a přistupuje se k nim jen mezi `lockMutex`/`unlockMutex` jejich mutexu. Blokující operace (DHT čtení, HTTP) se dělají mimo mutex.

* * *

## RTOS tasky

Všechny jsou staticky alokované (`xTaskCreateStaticPinnedToCore`, core 1; core 0 je pro WiFi/BT stack). Celkem asi 76 KiB stacků v interní RAM.

| Task            | Stack    | Prio  | Interval                                   | Co dělá                                                                                          |
|-----------------|----------|-------|--------------------------------------------|--------------------------------------------------------------------------------------------------|
| `WledPollTask`  | 6144 B   | 1     | 5 s (`DEVICE_POLL_INTERVAL_MS`) + notify   | `pollWLED()` + broadcast                                                                         |
| `EspPollTask`   | 6144 B   | 1     | 5 s + notify                               | poll `mc` a `panic-button` (`/api/dash-status`, HTTP timeout 1,5 s) + broadcast                  |
| `AutoLogicTask` | 8192 B   | 1     | 60 s                                       | aktualizace času, auto-odeslání svátku na Discord                                                |
| `EmergencyTask` | 8192 B   | **2** | 2 s idle / 1 s při alarmu                  | viz [Emergency mode](#emergency-mode)                                                            |
| `WeatherTask`   | 16384 B  | 1     | 15 min (`WEATHER_POLL_INTERVAL_MS`) + notify | `pollWeather()` (TLS)                                                                          |
| `ClimateTask`   | 12288 B  | 1     | 60 s + notify                              | DHT čtení, hodinový ntfy report (TLS)                                                            |
| `NtfyCmdTask`   | 12288 B  | 1     | tick 1 s                                   | ntfy příkazy každých 15 s, health check desktopu každých 15 s (3 s po WOL, 180 s)                |
| `HeaterTask`    | 12288 B  | 1     | fronta + 5 s                               | odesílání IR, AUTO logika, limity, ntfy chyby (TLS)                                              |

Tasky s „notify“ se vzbudí přes `xTaskNotifyGive` (manuální refresh, ntfy test). Důvod rozdělení pollerů: pomalá WLED nezdrží monitoring panic-buttonu a naopak.

* * *

## Emergency mode

Cíl: po SOS z panic-buttonu omezit zbytečný provoz a držet se opakovaných notifikací, dokud někdo fyzicky neresetuje.

1. Panic-button POSTne `/api/panic` s `type=panic`.
2. Handler nastaví `activeAlarm`, hned odešle **první** ntfy (`sendNtfyPanic`) a broadcastne stav.
3. Frontend při `alarm.active` zobrazí fullscreen červený overlay.
4. `EmergencyTask` při náběhu alarmu zavolá `vTaskSuspend()` na `WledPollTask`, `EspPollTask` a `AutoLogicTask`, přebírá opakování ntfy (`NTFY_REPEAT_INTERVAL_MS`, 5 min, čítá `ntfyCount`) a zrychlí tick na 1 s.
5. Reset přijde z panic-button node jako `/api/panic` s `type=panic_reset` → `active=false` → `EmergencyTask` všechny tři tasky `vTaskResume()`.

**Co běží dál:** web server + websocket (vlastní AsyncTCP kontext), `loop()`, a také `WeatherTask`, `ClimateTask` a `NtfyCmdTask` (ty se nepozastavují). Hodinový report se během alarmu přeskakuje (DHT se čte dál), ntfy příkazy a health check desktopu běží normálně.

**Co je pozastavené:** WLED/ESP polling (dashboard ukazuje poslední známý stav) a svátková hláška. Pokud alarm padne do okna 00:01–00:15, hláška se odešle po resolvnutí, dokud okno trvá.

* * *

## Datové toky

### Websocket / JSON

`buildDashboardJson(true)` (`{"type":"devices", ...}`) se broadcastuje při connectu klienta, po každém pollu (WLED, ESP, počasí, klima), při změně stavu desktopu, každých 10 s z heartbeatu a po akcích z handlerů.

```
{
  "type": "devices",
  "devices": [ /* wledPokoj, mcEsp, panicNode */ ],
  "alarm":   { "active", "eventId", "deviceId", "deviceName", "receivedAtMs", "acked", "type" },
  "system":  { "ip", "mdns", "gateway", "subnet", "dns", "wifiStatus", "rssi", "heap", "uptimeMs",
               "netReady", "timeReady", "logicDone", "fatalError", "needsUserAttention",
               "autoSendDone", "lastSendOk", "currentDateKey", "currentFullDate", "currentNames",
               "lastSent", "resetReason", "bootCount", "minHeap", "maxBlock",
               "ntfyReport": { "code", "ageS" } },
  "weather": { "valid", "temp", "apparentTemp", "windSpeed", "code", "condition", "icon",
               "sunrise", "sunset", "sunriseMin", "sunsetMin", "moonPhase", "moonEmoji", "moonName",
               "updatedAtMs", "forecast": [ { "hour", "temp", "code", "icon", "date" } ] },
  "climate": { "indoor": {...}, "outdoor": { "valid", "temp", "hum", "updatedAtMs", "ageS", "trend", "trendMin" } },
  "desktop": { "known", "online", "ip", "checkAgeS", "wolAgeS" }
}
```

Poznámky: `alarm.receivedAtMs` je ve skutečnosti epoch v sekundách (`mktime`), 0 když není čas. `ntfyReport.code`: 200 = OK, 0 = zatím neodesláno, -100 = WiFi, -101 = `http.begin` selhal, ostatní záporné = chyba HTTPClientu. Forecast se do JSON ořezává na 48 hodin.

### Klima (`sensors.cpp`)

DHT11 indoor na GPIO 5, DHT22 outdoor na GPIO 4, čtení každou minutu. Čtení je vlastní (`dhtRead`), ne Adafruit knihovna: ta na ESP32 vypínala přerušení a četla všech 80 pulzů bez předčasného konce, což vedlo k interrupt watchdogu (poslední krok vždy `climate_read`). Vlastní čtečka hlídá každou hranu přes `micros()` s timeouty (200/150/100/150 µs), při chybě skončí hned, kontroluje checksum a rozsah a drží min. interval 2 s; chyby se logují jako `[SENSORS] read failed: <senzor> (<důvod>)`. Venkovní čidlo v černé krabičce na slunci ukazovalo o několik °C víc (solární ohřev), řešení: bílý nátěr / stínítko. Měření je `valid`, dokud po sobě neselže `CLIMATE_MAX_FAILS` (3) čtení. Trend = rozdíl proti nejstaršímu vzorku v okně (kruhový buffer 60 minutových vzorků, jen v RAM), ukazuje se až od 30 vzorků (`trendMin` = délka okna, 0 = málo dat). Frontend označí data starší než 180 s jako stará.

**Automatický power-cycle venkovního DHT22:** senzor je napájený z GPIO (`PIN_DHT_OUTDOOR_PWR`, default 6, `-1` = vypnuto). Pokud venkovní čtení selže `DHT_POWER_CYCLE_FAILS` (3) krát po sobě, `ClimateTask` zaloguje stav datového pinu (LOW = zaseknutý senzor nebo zkrat, HIGH = linka volná), na `DHT_PWR_OFF_MS` (2 s) vypne napájení, zapne, počká `DHT_PWR_SETTLE_MS` (2 s), zavolá `begin()` a hned čte znovu. Další cycle nejdřív po 10 min (`DHT_POWER_CYCLE_COOLDOWN_MS`). Blokuje ClimateTask asi 4 s. V JSON `climate.<senzor>` jsou `fails`, `pwrCycles` (od startu) a `cycleAgeS`. Důvod: DHT22 se občas zasekne a pomůže jen odpojit napájení; kabel je cca 3 m a pull-up na modulu je 10k, což je na tu délku slabé.

### Počasí a měsíc (`weather.cpp`)

Open-Meteo (`current` + `hourly` + `daily` sunrise/sunset, `forecast_days=2`, `Europe/Prague`, souřadnice z `config.h`). Fáze měsíce se počítá lokálně z Julian Date (synodický měsíc 29,530588853 d), výsledek je lunární den 0–29. Frontend si navíc sám dopočítává countdown na další nov/úplněk.

### Hodinový ntfy report

`ClimateTask` posílá report na `NTFY_REPORT_TOPIC` jednou za hodinu v minutách :00–:02 podle reálného času (NTP), ne podle `millis()`, a čekání zkracuje tak, aby vstal těsně po celé hodině. Obsah: Venku/Uvnitř (teplota, vlhkost), počasí (teplota, pocitová, stav, vítr), východ/západ, fáze měsíce, uptime + reset reason + boot #, řádek `Heap: volný / min / max blok`, po abnormálním restartu (PANIC / WDT / BROWNOUT) prvních 3 h běhu souhrn z RTC breadcrumbs a při venkovní teplotě ≤ 1,0 °C řádek „POZOR, ZIMA“. Priorita `min` (tichý). Ruční test: `POST /api/ntfy-test`.

### ntfy – témata

| Topic (`config.h`)              | Obsah                                                                  |
|---------------------------------|------------------------------------------------------------------------|
| `NTFY_REPORT_TOPIC`             | hodinové reporty, Desktop ONLINE/OFFLINE                               |
| `NTFY_CMD_TOPIC`                | příkaz `WOL` (case-insensitive) a potvrzení odeslání                   |
| SOS topic (natvrdo v `ntfy.cpp`) | SOS + opakování při aktivním alarmu, priorita `urgent`                |

`ntfyCmdPoll()` startuje od času spuštění (staré zprávy ignoruje, čeká na NTP), pak posouvá okno přes `id` poslední zprávy a zpracovává jen události typu `message`.

### Desktop: WOL a health check

- **Health check:** TCP connect na RDP (3389) nebo SMB (445) s timeoutem 1 s. Online se projeví hned, offline až po 2 neúspěších po sobě. ntfy ONLINE/OFFLINE jde jen při změně (ne při první kontrole).
- **WOL:** magic packet přes UDP broadcast (porty 9 a 7). Spouští se příkazem `WOL` na `NTFY_CMD_TOPIC` nebo `POST /api/wol`. Po odeslání se `DESKTOP_WAKE_WINDOW_MS` (180 s) kontroluje každé 3 s místo 15 s a UI ukazuje „probouzím“.

### Topení (IR)

IR LED na `PIN_IR_TX` (GPIO 7, přes BC547), protokol SYMPHONY 12 bit 38 kHz, 3 opakování. Ovladač je jednosmerný, takže stav (`on`, `mode`) je jen **odhad**: Power je toggle, Mode cykluje Ventilátor → I (1000 W) → II (2000 W), po Power vždy začíná ventilátor. Nosná 38 kHz se generuje softwarově (busy-wait), proto `sendOne()` na dobu odeslání zvyšuje prioritu HeaterTasku (jinak ho FreeRTOS střídá s `loopTask` a bity se přehazují). Odhad se persistuje v `/heater.json`; po výpadku napájení se topení bere jako vypnuté (stejný okruh), po jiném restartu se použije uložený odhad. Pokud odhad nesedí, oprava přes Sync (`/api/heater/sync`).

`HeaterTask` zpracovává frontu příkazů (HTTP handlery jen zařazují, nikdy neblokují) a každých 5 s vyhodnocuje limity a AUTO. ntfy se posílá jen z tasku přes frontu zpráv a jen u chyb (`NTFY_CMD_TOPIC`).

- **Manual:** 7 tlačítek ovladače, limit 4 h (`HEATER_MANUAL_LIMIT_S`), pak se vypne.
- **AUTO:** cíl 15–35 °C, časovač 1–480 min (UI tlačítka −15 −5 −1 +1 +5 +15). Podle indoor DHT11 zapne pod cílem (Power + Mode×2 = režim II) a vypne od cíl + 2 °C. Při cíl + 4 °C pošle Power; když 3 měření po sobě neklesají ani nestagnují, zkusí znovu (max 3×), pak AUTO ukončí. Chyba čidla (neplatné nebo starší než 5 min) AUTO ukončí a topení vypne. Při běžícím AUTO se vnitřní DHT11 čte každých 10 s (`CLIMATE_POLL_AUTO_MS`, bez zápisu do trendu a bez počítání chyb čidla), venkovní a dashboard dál 1×/min; zásah proti přehřátí počítá „měření“ po minutách. Změna času za běhu posune zbývající čas o rozdíl (min. zbývá 1 min), konec → vypnout a zpět na manual. Restart AUTO ruší.
- **Pravidlo:** AUTO neběží přes restart, tlačítka jsou za AUTO zamčená (409/503).

Endpointy v tabulce níže, podrobnosti a zapojení v [ir.md](ir.md).

### RTC breadcrumbs

Diagnostika pádů (watchdog, panic, brownout). Data jsou v `RTC_NOINIT` paměti, takže přežijí tyto resety, ale ne výpadek napájení (po power-on se zahodí). Při každém startu `bcInit()` zkopíruje starý obsah do obyčejné RAM (`prev`) a RTC vynuluje.

Co se zapisuje:
- **`bcMark(task, step)`** – lock-free, volatelné z kteréhokoli tasku. Zapíše krok do ringu 48 záznamů (čas, task, krok, jádro) a jako „poslední krok“ daného tasku. Tasky ho volají kolem blokujících operací (poll WLED/ESP, Discord, weather, klima včetně kroků `dht_in` / `dht_out`, IR odeslání `heater_send`, ntfy report, ntfy příkazy, health check desktopu) a handlery `/api/panic`, `/api/send`, `/api/update-time` a WLED POST značí krok pod taskem `web`. Běžný stav je `idle`.
- **`bcHeartbeat()`** (z `loop()` každých 10 s) – snapshot: uptime, volný internal heap, minimum od startu, největší souvislý blok, volná PSRAM, RSSI a volný stack tasků i `loop`.
- **`bcHourlySample()`** (z `ClimateTask` při hodinovém reportu) – vzorek heapu, drží se posledních 24.

Kde se to čte po restartu:
- Serial při bootu (`[BC] …`).
- Hodinový ntfy report po abnormálním restartu (posledních 8 kroků, poslední krok každého tasku, stack a heap z posledního heartbeatu, heap po hodinách).
- `GET /api/crash` → `{"prev": {…}, "now": {…}}` včetně celého ringu a hodinových vzorků.

Jak to číst: pád v kroku `ntfy_report`, `discord` nebo `weather_poll` ukazuje na TLS, `h_panic` / `h_send` / `h_time` / `h_wled` na blokující handler v `async_tcp`. Klesající `min` heapu po hodinách značí únik paměti, malý `max blok` fragmentaci.

Omezení: ring ukazuje, co se dělalo naposled, ne co přesně spadlo. Heartbeat je jen každých 10 s, takže stav těsně před pádem nemusí být přesný. Počítadlo zápisů je v obyčejné RAM, v RTC se jen udržuje nejvyšší hodnota. Souběžné zápisy z více tasků nejsou chráněné (benigní race, záznam může být občas přepsaný).

### Svátky

- `data/names.json`: objekt `"MM-DD" → [jména]` (víc jmen na den).
- **Auto-odeslání:** `AutoLogicTask` každou minutu; v okně `AUTO_SEND_HOUR:AUTO_SEND_START_MIN` + `AUTO_SEND_WINDOW_MIN` (00:01–00:15), pokud `last_sent` ≠ dnešní datum, pošle „Dnes ma svatek: …“ na Discord webhook s role mention a zapíše `/state.json`. Při selhání zkouší znovu každou minutu do konce okna.
- **Ruční odeslání:** `POST /api/send` nebo tlačítko SEND.
- **Sledované svátky:** UI ukládá seznam jmen přes `POST /api/watch` do `/watch.json` (max 30 jmen, každé ≤ 30 znaků). Párování se jmény a výpočet následujících 7 dní (dnes + 6) dělá výhradně frontend z `names.json`.

* * *

## Endpointy

Statické soubory: `serveStatic("/", LittleFS, "/")` s `index.html` jako default (tedy i `/names.json`, `/watch.json`, `/state.json`). WebSocket na `/ws`.

⚠ = handler dělá blokující síťovou operaci přímo v `async_tcp` kontextu (viz [limity](#známé-limity)).

| Endpoint                                        | Metoda | Co dělá                                                                       |
|-------------------------------------------------|--------|-------------------------------------------------------------------------------|
| `/api/status`, `/api/devices`                   | GET    | snapshot `buildDashboardJson`                                                 |
| `/api/diagnostics`                              | GET    | diagnostika do Serialu + snapshot                                             |
| `/api/crash`                                    | GET    | RTC breadcrumbs: předchozí běh (kroky, heap, stacky) + aktuální heap          |
| `/api/refresh`                                  | POST   | neblokující: vzbudí WLED/ESP/weather/climate tasky, vrací 202                 |
| `/api/ntfy-test`                                | POST   | neblokující: zařadí hodinový report, vrací 202                                |
| `/api/wol`                                      | POST   | pošle magic packet (UDP, rychlé)                                              |
| `/api/heater`                                   | GET    | stav topení (odhad, AUTO, teplota, upozornění)                                |
| `/api/heater/btn?btn=power\|mode\|spin\|clock\|eco\|minus\|plus` | POST | zařadí odeslání IR (neblokuje), jen v manuálu                |
| `/api/heater/sync?on=1&mode=0..2`               | POST   | opraví odhad stavu, nic neposílá                                              |
| `/api/heater/auto/{start,update,stop}`          | POST   | AUTO (`target`, `minutes`); start vrací 409 bez platného čidla                |
| `/api/watch?names=A,B`                          | POST   | uloží sledovaná jména do `/watch.json`                                        |
| `/api/panic` ⚠                                  | POST   | JSON `type` = `panic` / `panic_reset`, vrací ack `espdash.ack.v1`             |
| `/api/send` ⚠                                   | POST   | ruční odeslání svátku na Discord                                              |
| `/api/update-time` ⚠                            | POST   | vynutí NTP/čas refresh                                                        |
| `/api/reset-last-sent`                          | POST   | vynuluje `last_sent` (testování auto-sendu)                                   |
| `/api/wled/{toggle,on,off,brightness,identify}` ⚠ | POST | ovládání WLED (`brightness` bere `value` jako query/form)                     |

* * *

## Frontend (`data/`)

Jedna stránka (`index.html` + `app.js` + `style.css`), data přijímá přes websocket (po odpojení reconnect po 2 s) a jako zálohu dotahuje `GET /api/status` každých 5 s. Sekce: hodiny + dnešní jména, karty klimatu (trend, stará data, „Pozor, zima“ při venku ≤ 1,0 °C), počasí + hodinová předpověď, karta měsíce, sekce Topení (sbalená ON/OFF + Auto/Manual; rozbalená: ruční ovladač, AUTO nastavení a běh, Sync pop-up, pruh s upozorněním), karta desktopu (stav + tlačítko WOL), karty zařízení (WLED ovládání, odkazy na zařízení), stat karty (IP, heap, WS), sbalitelné sekce Svátek a Sledované svátky, fullscreen alarm overlay.

**Access mode (Local / Remote):** při prvním načtení se vybírá a pamatuje v `localStorage` (`dashAccessMode`). Local otevírá zařízení přes `*.local`, Remote přes ZeroTier IP a porty z `DEVICE_ACCESS` v `app.js` (WLED 7070, MC 9090). Dashboard samotný je přes remote na portu 8080.

## Vzdálený přístup

`remote_setup.bat` (admin, Windows s ZeroTier adaptérem) nastaví `netsh portproxy` + firewall pravidla: 8080 → dashboard, 7070 → WLED, 9090 → MC ESP (vše na port 80 cíle). `remote_teardown.bat` je vrátí zpět. ZeroTier IP je duplicitně v `remote_setup.bat` a `REMOTE_BASE_IP` v `app.js`.

* * *

## LED stavy

| Barva     | Význam                                                                                    |
|-----------|-------------------------------------------------------------------------------------------|
| 🟢 Zelená  | vše OK                                                                                    |
| 🔵 Modrá   | `needsUserAttention` – něco čeká na zásah (failed send, NTP fail, FS error při čtení jmen…) |
| 🔴 Červená | `fatalError` – LittleFS mount fail nebo WiFi timeout při bootu                            |

Priorita: červená > modrá > zelená. Pin podle `PIN_NEOPIXEL` z Arduino core (fallback 48).

* * *

## Konvence pro nový kód

- Nový globální stav: skalár/řetězec → getter/setter v `state.h/.cpp`; struktura → vlastní mutex v `state.*`.
- Nový periodický úkol: statický task (buffer + stack) v `tasks.cpp`, start v `startBackgroundTasks()`; TLS tasky mají 12–16 KB stack.
- Nový endpoint s pomalou prací: handler jen nastaví flag a `xTaskNotifyGive()` na task, výsledek jde přes websocket (vzor `/api/refresh`, `/api/ntfy-test`).
- Do `config.h` jen přidávat nové defines, hodnoty existujících neměnit.
- Serial log: žádné opakující se výpisy. Periodické věci logovat jen při změně stavu nebo zřídka (heartbeat 1 řádek / 5 min, mDNS jen změna hosta). `platformio.ini` má `-DCORE_DEBUG_LEVEL=0`, takže `log_e/log_w` z Arduino jádra (např. `no permits for creation` ze `serveStatic`) se nevypisují; vlastní `logLine()` to neovlivní.

## Záměrně není

OTA, zabezpečení ntfy topicu, boot zpráva na ntfy, varování na vlhkost. Tlačítka jsou v kódu, ale nezapojená. ntfy oznámení u topení jen pro chyby.

* * *

## Známé limity

- **Credentials:** jsou v `src/secrets.h` mimo git. Ve veřejné historii repa ale zůstávají staré hodnoty (WiFi heslo, Discord webhook, role ID), webhook je zneplatněný (smazal ho GitHub scanner 2026-11-04) a WiFi heslo bylo změněno na routeru. Názvy ntfy topiců jsou v kódu a repo je veřejné, takže kdokoli může psát na `NTFY_CMD_TOPIC` (WOL) a číst reporty.
- **Blokující handlery v `async_tcp`:** `/api/panic` (první ntfy, TLS), `/api/send` (Discord TLS), `/api/update-time` (`getLocalTime`, default 5 s), `/api/wled/*` (HTTP timeouty 1,5 s, po POST navíc `pollWLED()`; `identify` dělá dva POSTy + `delay(350)`). Porušuje pravidlo „v async handlerech neblokovat“ z HANDOFF a hrozí task WDT při pomalé síti. Správný vzor je `/api/refresh`.
- **TLS bez ověření certifikátu:** všude `setInsecure()` (Discord, ntfy, Open-Meteo).
- **Žádná autentizace:** všechny endpointy i celý LittleFS root jsou dostupné komukoli v síti / přes ZeroTier.
- **Fallback IP prázdné:** `MC_FALLBACK_IP` a `PANIC_NODE_FALLBACK_IP`. Když mDNS selže a není cache, zařízení je offline (typicky po čerstvém bootu).
- **Duplicitní konstanty:** práh mrazu 1,0 °C je natvrdo v `tasks.cpp` (report) i `app.js` (UI); SOS topic je natvrdo v `ntfy.cpp`; ZeroTier IP je v `app.js` i `remote_setup.bat`.
- **Předpověď:** `WEATHER_FORECAST_HOURS` je 168, ale s `forecast_days=2` API vrací nejvýš 48 hodin a JSON ořezává na 48.
- **Perzistence:** historie trendu je jen v RAM, `bootCounter` i breadcrumbs se nulují při výpadku napájení.
- **Ověřeno na zařízení:** breadcrumbs, vlastní DHT čtečka, modul topení (včetně dosahu IR po přidání tranzistoru), WOL, health check, sledované svátky i `NTFY_CMD_TOPIC`. Watchdog restarty vyřešené změnou DHT čtení.
- **Otevřené:** blokující handlery v `async_tcp` (viz výše), TFT sloučení, mobilní zrcadlení přes ntfy a úprava vzhledu UI včetně grafů: viz [HANDOFF.md](HANDOFF.md) a [To-Do.md](To-Do.md).
- SolaX, tepelné čerpadlo a kamery sem záměrně nepatří, jsou to samostatné projekty.
