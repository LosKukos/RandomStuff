# Dashboard – handoff pro nový chat (stav 2026-11-05)

Kuba, česky, neformálně, stručně. Nepiš kód ani soubory, dokud o to nežádá. Posílej jen měněné soubory (ne celý zip), pokud neřekne jinak.

## Projekt
ESP32-S3 domácí dashboard (esp32-s3-devkitc-1, qio_opi, 8 MB PSRAM), PlatformIO + Arduino, FreeRTOS tasky na jádru 1, ESPAsyncWebServer + WebSocket `/ws`, ArduinoJson v7, LittleFS (`data/` = UI, `/names.json`, `/state.json`, `/watch.json`), NTP (`CET-1CEST,M3.5.0/2,M10.5.0/3`).
Verze „Dashboard climate V1". Repo: https://github.com/LosKukos/RandomStuff/tree/main/Dashboard

## Pravidla
- `platformio.ini` je jeho (jediný `[env:esp32s3]`), nepřepisovat.
- `src/config.h` je jeho (piny DHT22 outdoor=4, DHT11 indoor=5, WOL MAC 74:56:3C:A6:15:33, ntfy topicy). Jen přidávat nové defines, nikdy nepřepisovat hodnoty. `remote_setup.bat` taky jeho (ZT IP 172.27.167.45, dash → 10.0.1.24).
- GPIO 26–37 jsou na S3 s octal PSRAM nepoužitelné. LED = onboard (PIN_NEOPIXEL, fallback 48). Žádná tlačítka zatím.
- V async handlerech nikdy neblokovat (task WDT) – používat `xTaskNotifyGive`/`ulTaskNotifyTake`.
- Nechce: boot zprávu na ntfy, OTA, zabezpečení ntfy topicu, varování na vlhkost.

## Hotovo
DHT11/DHT22 karty + trend/stale + „Pozor, zima" (vločka), karta fáze měsíce, hodinový ntfy report (zarovnaný na celou hodinu), reset reason diagnostika, neblokující `/api/refresh`, `/api/ntfy-test`, WOL (ntfy zpráva „WOL" i tlačítko v dashboardu `/api/wol`), health check desktopu 10.0.1.50 (RDP 3389 / SMB 445, ntfy ONLINE/OFFLINE), `names.json` s víc jmény na den, sledované svátky (`/api/watch?names=A,B` → `/watch.json`, collapsible sekce, 7 dní dopředu).
Ntfy: reporty + Desktop ONLINE/OFFLINE → `loskukos_dash`; příkaz „WOL" + potvrzení → `loskukos_cmd` (`NTFY_CMD_TOPIC`).
WOL i health check ověřené na zařízení. Sledované svátky a topic `_cmd` zatím jen ověřené v kódu/prohlížeči, ne na desce.
RTC breadcrumbs (commit `323d724` v `main`, `src/breadcrumbs.*`): krok každého tasku, ring 48 kroků, heap/stack snapshot každých 10 s a hodinový vzorek heapu v `RTC_NOINIT` paměti, která přežije WDT/panic. Po restartu souhrn na Serial, do hodinového ntfy reportu (prvních 3 h po abnormálním restartu) a na `GET /api/crash`; report má navíc řádek s heapem. **Zatím nezkompilováno ani nenahráno na desku** (jen syntax-check na stubech).

## Otevřené
- ESP se restartovalo kvůli watchdogu: 2026-11-03 ~12:41 (interrupt WDT, uptime pak 1 h 8 min) a znovu po cca 10 h. Příčina neznámá. Další krok: nahrát breadcrumbs, počkat na další pád a podle posledního kroku tasků (`/api/crash`, ntfy report) poznat, co visí (TLS, blokující handler, únik heapu).
- Podezřelí do doby, než breadcrumbs něco ukážou: blokující handlery v `async_tcp` (`/api/panic`, `/api/send`, `/api/update-time`, `/api/wled/*`), TLS tasky a klesající min heap.
- Credentials (WiFi heslo, Discord webhook, role ID) jsou natvrdo v `src/config.cpp` ve veřejném repu. Přesunout do `secrets.h` v `.gitignore` a webhook rotovat.
- UI: projít vzhled a přehlednost (pořadí karet, jednotný styl, mobil, sekce Systém s diagnostikou, grafy teploty/vlhkosti). Zatím jen návrh, nic neděláno.
- TFT sloučení (dash → odjezdy → Discord bot, přepínání tlačítkem, statické obrazovky, bez animací): čeká na Lyriq bundle + starý kód odjezdovky. Zatím jen diskuse.
- Rozhodnout přístup k mobilnímu zrcadlení/bridge přes ntfy.
- Otestovat na desce: sledované svátky, `loskukos_cmd` (přihlásit odběr v ntfy appce).
