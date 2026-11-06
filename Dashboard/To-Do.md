# Dashboard – To-Do

Stav k 2026-11-07. Odškrtnuté položky mazat, nové přidávat nahoru podle priority.

## Nové
- [ ] **IR ovladač k elektrickému přímotopu:** naskenovat kódy původního ovladače a ovládat topení z dashboardu (tlačítka v UI, případně i příkaz přes ntfy `loskukos_cmd`). Detaily doladíme: které funkce topení (zap/vyp, výkon, termostat, časovač), kam umístit IR LED a přijímač, který pin (ne GPIO 26–37), knihovna (např. IRremoteESP8266).

## Teď na řadě (čeká na desku)
- [ ] Stáhnout `main`, zkompilovat a nahrát (breadcrumbs, vlastní DHT čtení `c4230f2`, DHT power-cycle, `secrets.h` – nic z toho zatím nebylo kompilované).
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
