# Dashboard – To-Do

Stav k 2026-11-12. Odškrtnuté položky mazat, nové přidávat nahoru podle priority.

## Stabilita
- [ ] Opravit blokující handlery v `async_tcp` (`/api/panic`, `/api/send`, `/api/update-time`, `/api/wled/*`) přes `xTaskNotifyGive` / `ulTaskNotifyTake` (pravděpodobně uděláme).

## UI a funkce
- [ ] Projít vzhled a přehlednost dashboardu: pořadí karet, jednotný styl, mobil, sekce Systém s diagnostikou (někdy se na to podíváme).
- [ ] Grafy teploty a vlhkosti (moc hezké mít).
- [ ] TFT sloučení (dash → odjezdy → Discord bot, přepínání tlačítkem, statické obrazovky, bez animací). Do budoucna, čeká na Lyriq bundle a starý kód odjezdovky.
- [ ] Vymyslet mobilní zrcadlení / bridge přes ntfy.

## Poznámky
- Venkovní DHT22 stačí napájet z GPIO 6 (power-cycle), další úpravy zapojení nejsou potřeba.
- Watchdog restarty vyřešené změnou DHT čtení.
- Sledované svátky i `loskukos_cmd` fungují (Discord, příkazy jako WOL).
- Bezpečnost: změna WiFi hesla hotová; přejmenování ntfy topiců a čištění historie repa nepotřebné.
