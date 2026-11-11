# Dashboard – To-Do

Stav k 2026-11-07. Odškrtnuté položky mazat, nové přidávat nahoru podle priority.

## Nové
- [x] IR topení, etapa 1: modul `heater.cpp`, API `/api/heater*`, dokumentace v `ir.md` (nezkompilováno, kód zatím neviděl překladač).
- [x] IR topení, etapa 2 (hotovo, Auto tlačítko zatím neaktivní): sekce „Topení“ v UI (pod měsícem, nad „Zařízení“): sbalená ON/OFF + AUTO/MANUAL, rozbalená manuál s 7 tlačítky, Sync pop-up. Vzhled podle schváleného náhledu.
- [ ] IR topení, etapa 3: AUTO (cílová teplota 15–35 °C, časovač po 15 min do 8 h, hystereze +2 °C, přehřátí +4 °C s opakováním vypnutí, selhání čidla → vypnout, oznámení na `loskukos_cmd`).
- [ ] Zapojit IR LED: GPIO 7 → 1k → BC547, LED + 100R na 5 V, ověřit dosah (~1 m) a počet opakování.

## Teď na řadě (čeká na desku)
- [ ] Stáhnout `main`, zkompilovat a nahrát (IR topení `heater.cpp` + knihovna IRremoteESP8266, breadcrumbs, vlastní DHT čtení `c4230f2`, DHT power-cycle, `secrets.h` – nic z toho zatím nebylo kompilované).
- [ ] Ověřit nové čtení DHT: hodnoty indoor/outdoor vs. dashboard před flashem (±0,5 °C), vytáhnout datový vodič venkovního DHT22 za běhu (deska nesmí spadnout, jen `read failed`).
- [ ] Zapojit venkovní DHT22: VCC z GPIO 6, LED + 1k na GND, kondenzátor 104, pull-up 10k data↔VCC.
- [ ] Sledovat watchdog: po dalším pádu `GET /api/crash` / ntfy report. Poslední krok `dht_in` / `dht_out` potvrdí příčinu, cokoli jiného znamená hledat dál.

## Bezpečnost
- [ ] Změnit WiFi heslo na routeru a přepsat ho v `src/secrets.h` (staré je ve veřejné historii repa).
- [ ] Rozhodnout, jestli přejmenovat ntfy topicy (`loskukos_dash`, `loskukos_cmd`), jsou veřejně známé.
- [ ] Volitelně vyčistit historii repa (`git filter-repo`).

## Stabilita
- [ ] Opravit blokující handlery v `async_tcp` (`/api/panic`, `/api/send`, `/api/update-time`, `/api/wled/*`) přes `xTaskNotifyGive` / `ulTaskNotifyTake`.

## UI a funkce
- [ ] Projít vzhled a přehlednost dashboardu: pořadí karet, jednotný styl, mobil, sekce Systém s diagnostikou, grafy teploty a vlhkosti.
- [ ] TFT sloučení (dash → odjezdy → Discord bot, přepínání tlačítkem, statické obrazovky, bez animací). Čeká na Lyriq bundle a starý kód odjezdovky.
- [ ] Rozhodnout přístup k mobilnímu zrcadlení / bridge přes ntfy.
- [ ] Otestovat na desce: sledované svátky, příkaz přes `loskukos_cmd` (přihlásit odběr v ntfy appce).

## Dokumentace
- [ ] Aktualizovat `ARCHITECTURE.md` a `HANDOFF.md` (vlastní DHT čtení místo Adafruit knihovny, nové breadcrumby `dht_in` / `dht_out`, větší stacky `wled` a `esp`, odkaz na tento soubor).
