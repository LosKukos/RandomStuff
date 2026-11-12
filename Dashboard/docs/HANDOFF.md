# Dashboard – handoff pro nový chat (stav 2026-11-12)

Kuba, česky, neformálně, stručně. Nepiš kód ani soubory, dokud o to nežádá. Posílej jen měněné soubory (ne celý zip), pokud neřekne jinak.

## Projekt
ESP32-S3 domácí dashboard (esp32-s3-devkitc-1, qio_opi, 8 MB PSRAM), PlatformIO + Arduino, FreeRTOS tasky na jádru 1, ESPAsyncWebServer + WebSocket `/ws`, ArduinoJson v7, LittleFS (`data/` = UI, `/names.json`, `/state.json`, `/watch.json`), NTP (`CET-1CEST,M3.5.0/2,M10.5.0/3`).
Verze „Dashboard climate V1". Repo: https://github.com/LosKukos/RandomStuff/tree/main/Dashboard. Dokumentace je ve složce `docs/` (ARCHITECTURE, HANDOFF, To-Do, ir, rfid).

## Pravidla
- `platformio.ini` je jeho (jediný `[env:esp32s3]`), nepřepisovat.
- `src/config.h` je jeho (piny DHT22 outdoor=4, DHT11 indoor=5, WOL MAC 74:56:3C:A6:15:33, ntfy topicy). Jen přidávat nové defines, nikdy nepřepisovat hodnoty. `remote_setup.bat` taky jeho (ZT IP 172.27.167.45, dash → 10.0.1.24).
- GPIO 26–37 jsou na S3 s octal PSRAM nepoužitelné. LED = onboard (PIN_NEOPIXEL, fallback 48). Tlačítka SEND 16 / DIAG 17 zatím nezapojená. IR LED na GPIO 7 přes BC547 (3V3 → 47 Ω → LED → kolektor; GPIO 7 → 1 kΩ → báze; emitor GND). Pin 5V na desce napětí nedával, proto 3V3.
- V async handlerech nikdy neblokovat (task WDT) – používat `xTaskNotifyGive`/`ulTaskNotifyTake`.
- Nechce: boot zprávu na ntfy, OTA, zabezpečení ntfy topicu, varování na vlhkost.

## Hotovo
DHT11/DHT22 karty + trend/stale + „Pozor, zima" (vločka), karta fáze měsíce, hodinový ntfy report (zarovnaný na celou hodinu), reset reason diagnostika, neblokující `/api/refresh`, `/api/ntfy-test`, WOL (ntfy zpráva „WOL" i tlačítko v dashboardu `/api/wol`), health check desktopu 10.0.1.50 (RDP 3389 / SMB 445, ntfy ONLINE/OFFLINE), `names.json` s víc jmény na den, sledované svátky (`/api/watch?names=A,B` → `/watch.json`, collapsible sekce, 7 dní dopředu).
Ntfy: reporty + Desktop ONLINE/OFFLINE → `loskukos_dash`; příkaz „WOL" + potvrzení → `loskukos_cmd` (`NTFY_CMD_TOPIC`).
WOL i health check ověřené na zařízení. Sledované svátky a topic `_cmd` zatím jen ověřené v kódu/prohlížeči, ne na desce.
RTC breadcrumbs (commit `323d724` v `main`, `src/breadcrumbs.*`): krok každého tasku, ring 48 kroků, heap/stack snapshot každých 10 s a hodinový vzorek heapu v `RTC_NOINIT` paměti, která přežije WDT/panic. Po restartu souhrn na Serial, do hodinového ntfy reportu (prvních 3 h po abnormálním restartu) a na `GET /api/crash`; report má navíc řádek s heapem. Nahráno na desce 2026-11-07.
DHT: vlastní čtečka s timeouty místo Adafruit knihovny (commit `c4230f2`), příčina interrupt WDT (poslední krok vždy `climate_read`). Od nahrání bez pádu 10 h; skutečný test až s venkovním čidlem na zdi. Breadcrumby `dht_in` / `dht_out`, stacky wled/esp 6144 B.
Topení (IR, etapy 1–3, `heater.cpp`, UI sekce Topení v `data/`): manual + AUTO + Sync, nastavitelný počet IR opakování (`POST /api/heater/cfg?repeat=0..5`, default 3), ntfy `loskukos_cmd` jen při chybách. **Funguje na desce včetně dosahu** (po přidání tranzistoru; bez něj slabý signál způsoboval záměnu Mode/Spin a Power/Plus, protože kódy se liší jen ve dvou bitech a topení neplatný rámec zahodí, jen rozsvítí displej). AUTO se při změně cíle vyhodnotí hned; ladicí výpisy `[HEATER] auto: …` v Serialu. Detaily: `docs/ir.md`, `docs/ARCHITECTURE.md` (sekce Topení).

## Otevřené
- Watchdog restarty (2026-11-03 a dál): breadcrumbs ukázaly `climate_read`, opraveno vlastním DHT čtením. Potvrdit provozem; kdyby spadlo jinak, číst poslední krok z `/api/crash` / ntfy reportu. Dál podezřelé: blokující handlery v `async_tcp` a TLS tasky.
- Venkovní DHT22: čidlo je venku na zdi v bílé krabičce. Při slunci ukazovalo +8 až +9 °C (2026-11-12 poledne), v 15:00 už jen +2 °C proti Open-Meteo. Čeká se na noční reporty po západu (16:07): rozdíl <1 °C = OK, stálý rozdíl = offset čidla (pak přidat korekci do configu). Případně stínění / severní strana.
- Topení: vyzkoušet spolehlivost Mode ×2 a AUTO delší dobu, případně `repeat=1`. Po `uploadfs` se maže `/heater.json`, proto nastavit stav přes Sync. Prázdný `data/heater.json` v repu smazat. Nápad: pojistka proti rozjetému odhadu podle teploty (vypnuto, ale teplota roste → zkusit Power znovu). Neřešeno: stav otáčení (Spin) se v odhadu nesleduje. Do `docs/ir.md` hotové zapojení už je, ladicí logy možno vyčistit.
- Credentials jsou od 2026-11-05 v `src/secrets.h` (není v gitu, vzor `secrets.h.example` byl smazán; bez `secrets.h` build skončí `#error`). Discord webhook z veřejného repa smazal GitHub scanner (FriendlyScanner, 2026-11-04), takže je potřeba vytvořit nový a dát ho do `secrets.h`. Staré WiFi heslo zůstává ve veřejné historii, změnit na routeru. ntfy topicy (`loskukos_dash`, `loskukos_cmd`) jsou veřejně známé, zvážit přejmenování.
- UI: projít vzhled a přehlednost (pořadí karet, jednotný styl, mobil, sekce Systém s diagnostikou, grafy teploty/vlhkosti). Zatím jen návrh, nic neděláno.
- TFT sloučení (dash → odjezdy → Discord bot, přepínání tlačítkem, statické obrazovky, bez animací): čeká na Lyriq bundle + starý kód odjezdovky. Zatím jen diskuse.
- Rozhodnout přístup k mobilnímu zrcadlení/bridge přes ntfy.
- Otestovat na desce: sledované svátky, `loskukos_cmd` (přihlásit odběr v ntfy appce).
