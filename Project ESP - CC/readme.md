# AXIS patch – stabilita firmwaru (kroky 2 + 3)

Složka kopíruje strukturu repa `Project ESP - CC/`. Stačí soubory přepsat (nový je jen `sync.h/.cpp`).

## Co kam

| Soubor | Stav | Co se změnilo |
|---|---|---|
| `src/sync.h`, `src/sync.cpp` | **NOVÝ** | Zámky: `DataLock` (sdílená data) a `LogLock` (logy) |
| `src/web.cpp` | přepsat | POST těla se skládají z chunků (`bodyHandler`), POST i GET handlery běží pod `DataLock` |
| `src/mqtt_bridge.cpp` | přepsat | Odchozí MQTT přes frontu; **odpovědi jen soukromě**, whitelist příkazů, `create_order` přes MQTT, sanitizovaný broadcast |
| `src/command_task.cpp` | přepsat | Zámek jen kolem fronty a ukládání, `mqttLoop()` běží mimo zámek |
| `src/utils.h`, `src/utils.cpp` | přepsat | `addLog` pod `LogLock`, nová `getLogsCopy()` |
| `src/main.cpp` | přepsat | Na začátku `initSync()` |
| `src/app_state.h`, `src/orders.h`, `src/orders.cpp` | přepsat | Objednávka má `ownerId`; funkce `userOwnsOrder/Package` (vlastník = `ownerId`, jinak shoda `recipient` s mcName/username) |
| `src/mqtt_bridge.h` | přepsat | Nové `mqttBroadcastPublic()` |
| `src/secrets.h` | **NOVÝ** (nekomitovat!) | MQTT host/port/uživatel/heslo pro EMQX. Testovací údaje jsou vyplněné, před ostrým provozem změnit |
| `src/secrets.example.h` | **NOVÝ** | Šablona bez hesel (tu komitovat) |
| `gitignore.txt` | obsah přidat do `.gitignore` | Řádek `src/secrets.h` (zbytek souboru je stejný) |
| `data/remote.html` | přepsat + nahrát na GitHub Pages | Soukromý topic `axis/res/<clientId>`, hráči pro registraci bez tokenu, bez klientského filtru, hlášení výsledku `create_order` |
| `src/persistence.cpp` | přepsat | `queue.json` dokumenty na heap (ne na stack) |
| `platformio.ini` | přepsat | `ArduinoJson@^6.21.5` |
| `data/index.html.gz`, `data/config.html.gz`, `data/css/demo.css.gz` | přepsat | Znovu vygenerované ze zdrojových HTML/CSS (staré `.gz` se od nich lišily, takže ESP mohl servírovat starý dashboard) |

## Co jsem ověřil (a co ne)

- Všech 15 `.cpp` souborů prošlo `g++ -fsyntax-only` proti reálným hlavičkám ArduinoJson 6.21.5 a PubSubClient a proti zjednodušeným stubům ESP/Arduino/webserveru. Podpisy `ArBodyHandlerFunction`, `_tempObject`, `textAll(String)` a `beginPublish/write` jsem ověřil přímo ve zdrojích knihoven.
- **Neověřeno:** skutečný build pro ESP32 (toolchain odsud stáhnout nejde) a chování na hardwaru. Proto je níže test po flashi.

## Flash (pořadí)

1. `pio run` – musí projít.
2. `pio run -t uploadfs` – nahraje celou složku `data/`. **Pozor:** přepíše i `config.json` a `*.json` na ESP, takže po něm bude potřeba znovu nastavit WiFi přes AP (`AXIS` hotspot) a objednávky/balíky začnou prázdné. Pro test to je v pořádku.
3. `pio run -t upload` – nahraje firmware.
4. `pio device monitor` – sleduj log při bootu.

## Přechod na EMQX (HiveMQ Serverless končí 31. 12. 2026)

- Broker: EMQX Cloud Serverless, ESP jde na port 8883 (TLS), web na `wss://…:8084/mqtt`. Adresa a testovací účet jsou už v `secrets.h` a `remote.html`.
- Topicy se nezakládají, vzniknou při prvním použití. Zbývá jen volitelné ACL.
- Test: po flashi `/api/logs` ukáže `[MQTT] connected to broker`; na webu se rozsvítí tečka připojení a jde Register/Login.
- Ostrý provoz: dva účty v EMQX (`esp`, `web`) s ACL (viz tabulka v konverzaci), nová hesla do `secrets.h` a `remote.html`.

## Oprávnění (krok 2)

- **Lokální admin panel (STA IP):** vidí všechno jako dřív (REST + WebSocket, plné události).
- **Remote (GitHub Pages přes MQTT):** na `axis/broadcast` jdou jen signály `{"event":"order_updated"}` bez dat, ME snapshot a seznam hráčů. Objednávky a balíky dostane uživatel jen jako soukromou odpověď na `axis/res/<clientId>`, vždy filtrované podle uživatele z tokenu (`userId` poslaný klientem se ignoruje).
- **Whitelist `axis/cmd`:** `get_me`, `get_orders_by_user`, `get_packages_by_user`, `create_order`. Cokoli jiného se odmítne a do fronty příkazů pro CC se z internetu nedostane nic.
- **`create_order` přes MQTT:** `recipient` a `ownerId` určuje server z tokenu. Limity: 30 položek, 10000 ks na položku, max 10 otevřených objednávek na uživatele.
- **Seznam hráčů pro registraci:** akce `players` na `axis/auth` (bez tokenu). ESP navíc při změně posílá `players_online` na broadcast.
- Nový formát v `orders.json` (`ownerId`) je zpětně kompatibilní; staré objednávky se přiřazují přes `recipient`.

**Omezení do ostrého provozu:** webový klient i ESP mají zatím stejný MQTT účet, takže soukromí odpovědí stojí na tom, že `clientId` nikdo nezná. Skutečnou izolaci dá až oddělený účet s ACL (už v seznamu před produkcí).

## Jak to teď funguje (3 pravidla)

1. Kdo sahá na `orders / packages / nodes / users / commandQueue / meStorage / playersOnline`, drží `DataLock`.
   V handlerech to děláš automaticky – `bodyHandler` už zámek bere za tebe. Nový GET handler: přidej `DataLock lock;` na první řádek.
2. Do MQTT píšeš jen přes `mqttBroadcast(json)`. Nikdy nevolej `mqtt.publish` mimo `mqtt_bridge.cpp`.
3. Uvnitř `DataLock` nevolej nic, co čeká na síť (HTTP/TLS). Přesně proto `mqttLoop()` běží mimo něj.

## Git (jednorázově, v kořeni repa)

```
git rm --cached "Project ESP - CC/data/config.json" "Project ESP - CC/data/me.json" "Project ESP - CC/data/nodes.json" "Project ESP - CC/data/orders.json" "Project ESP - CC/data/packages.json" "Project ESP - CC/data/queue.json"
git commit -m "stop tracking runtime data"
```
Soubory zůstanou na disku, jen je git přestane sledovat.

## Test po flashi

1. `pio run` projde bez erroru (warningy `unused parameter 'body'` jsou v pořádku).
2. ESP naběhne, v `/api/logs` (AP) je `[BOOT] starting` a `[MQTT] connected to broker`.
3. Dashboard načte ME seznam a objednávku jde založit.
4. Pošli z CC masteru větší `package/register` (více položek) – dřív se rozbil, teď musí projít.
5. Nechej to běžet 30 min a koukni, jestli ESP neresetuje (`/api/debug` → `uptime` roste, `heap` nekleská).

## Test oprávnění (po flashi + nahrání `remote.html`)

1. Admin panel na STA IP: vidíš všechny objednávky.
2. GitHub Pages → záložka Register: dropdown hráčů se naplní (potřebuje, aby master posílal seznam hráčů; jinak bude prázdný).
3. Dva účty A a B: A vytvoří objednávku → A ji vidí, B ne, admin panel ano. Po změně stavu v adminu se A seznam obnoví.
4. `/api/logs` při pokusu o jiný typ příkazu ukáže `[MQTT] cmd: rejected type`.

## Když build selže
Pošli mi celý výpis chyby. Nemohl jsem to zkompilovat, takže nejpravděpodobnější místa jsou `bodyHandler` (typ `ArBodyHandlerFunction`) a `mqtt.write` ve `mqtt_bridge.cpp`.
