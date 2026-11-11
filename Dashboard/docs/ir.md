# IR ovladač přímotopu – kódy a klonování

Naskenované kódy původního ovladače elektrického přímotopu. Cíl: ovládat topení z dashboardu (tlačítka v UI, případně příkaz přes ntfy) pomocí ESP32 + IR LED.

## Ovladač

Ovladač má 7 tlačítek. Protokol zjištěný knihovnou **IRremoteESP8266** je **SYMPHONY**, 12 bitů, nosná 38 kHz.

| # | Tlačítko | Hex (12 bit) |
|---|----------|--------------|
| 1 | Power | `0xDC3` |
| 2 | Mode  | `0xD90` |
| 3 | Eco   | `0xD84` |
| 4 | Spin  | `0xD88` |
| 5 | Clock | `0xD82` |
| 6 | Minus | `0xDA0` |
| 7 | Plus  | `0xDC6` |

Kódy byly získány skenováním ovladače přijímačem (modul HW-477 / KY-022 s VS1838B). Odesílání přes IR LED zatím **není otestované na topení**, viz sekce Stav níže.

## Hardware

- **Přijímač** (jen pro skenování): TSOP4838 nebo modul HW-477 / KY-022, výstup na GPIO. Modul HW-477 má piny `−` (GND), střední (VCC, 3V3) a `S` (signál).
- **Vysílač:** IR LED 940 nm (např. Vishay TSAL6100) přes rezistor 100 Ω přímo z GPIO. Pro větší dosah přes tranzistor NPN (BC337 apod.): GPIO → 1 kΩ → báze, emitor na GND, kolektor na katodu LED, anoda přes 100 Ω na 5 V.
- **Pin:** vysílač na **GPIO 7** (`PIN_IR_TX` v `config.h`), ne GPIO 26–37 (viz [To-Do.md](To-Do.md)). Při testech čtení byl přijímač na GPIO 15.
- **Zapojení vysílače:** pin 5V na desce napětí nedával, proto se LED napájí ze **3V3**.
  - Test bez tranzistoru: GPIO 7 → 100 Ω → anoda IR LED, katoda → GND (proud ~20 mA, dosah malý; pod 68 Ω nejít, limit pinu).
  - Cílové zapojení: 3V3 → 47 Ω (případně 33 Ω) → anoda IR LED, katoda → kolektor BC547; GPIO 7 → 1 kΩ → báze; emitor → GND (BC547 zepředu zleva C-B-E; proud ~40–58 mA, vysílá se jen krátce). Při restartech desky kondenzátor 100 µF mezi 3V3 a GND u LED.
  - Barvy: 1 kΩ = hnědá-černá-černá-hnědá-hnědá, 100 Ω = hnědá-černá-černá-černá-hnědá, 47 Ω = žlutá-fialová-černá-zlatá-hnědá.

## Knihovna

`platformio.ini`:

```ini
lib_deps =
    IRremoteESP8266
```

Přidáno do `platformio.ini`.

(Zápis `crankyoz/IRremoteESP8266` v PlatformIO registru nefunguje. Pokud by nestačil ani název, jde použít `https://github.com/crankyoz/IRremoteESP8266.git`.)

## Kód pro odeslání (klon ovladače)

```cpp
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

const uint16_t PIN_IR_TX = 4;  // změnit podle zapojení, ne GPIO 26-37

IRsend irsend(PIN_IR_TX);

enum HeaterBtn : uint16_t {
  BTN_POWER = 0xDC3,
  BTN_MODE  = 0xD90,
  BTN_ECO   = 0xD84,
  BTN_SPIN  = 0xD88,
  BTN_CLOCK = 0xD82,
  BTN_MINUS = 0xDA0,
  BTN_PLUS  = 0xDC6,
};

void heaterSend(HeaterBtn btn) {
  irsend.sendSymphony(btn, 12);
}

void setup() {
  irsend.begin();
}

void loop() {
  heaterSend(BTN_POWER);
  delay(5000);
}
```

Pokud topení po jednom odeslání nereaguje, zvýšit počet opakování: `irsend.sendSymphony(btn, 12, 3)` (třetí parametr je `repeat`).

## Kód pro opětovné skenování

Jednoduchá čtečka, která při stisku vypíše jeden řádek ve formátu `IR|PROTOKOL|0xHEX|bity` (pro neznámé protokoly `IR|RAW|počet|časy_us`):

```cpp
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

const uint16_t PIN_RX = 15;  // pin přijímače

IRrecv irrecv(PIN_RX, 1024, 50, true);
decode_results results;

void setup() {
  Serial.begin(115200);
  irrecv.enableIRIn();
  Serial.println("READY");
}

void loop() {
  if (!irrecv.decode(&results)) return;

  if (!results.repeat) {
    if (results.decode_type == UNKNOWN) {
      Serial.print("IR|RAW|");
      Serial.print(results.rawlen - 1);
      Serial.print("|");
      for (uint16_t i = 1; i < results.rawlen; i++) {
        Serial.print((uint32_t)results.rawbuf[i] * kRawTick);
        if (i < results.rawlen - 1) Serial.print(",");
      }
      Serial.println();
    } else {
      Serial.print("IR|");
      Serial.print(typeToString(results.decode_type));
      Serial.print("|0x");
      Serial.print(uint64ToString(results.value, 16));
      Serial.print("|");
      Serial.println(results.bits);
    }
  }
  irrecv.resume();
}
```

## Stav

- [x] Kódy všech 7 tlačítek naskenovány
- [ ] Odeslání IR LED ověřit na skutečném topení (dosah, počet opakování)
- [x] Pin zvolen (GPIO 7, BC547) – fyzicky zatím nezapojeno
- [x] Firmware modul `heater.cpp` + API (etapa 1)
- [x] UI sekce Topení, ruční režim (etapa 2)
- [x] AUTO režim (etapa 3)

## API (etapa 1)

```
GET  /api/heater                         stav (odhad)
POST /api/heater/btn?btn=power|mode|spin|clock|eco|minus|plus
POST /api/heater/cfg?repeat=0..5                   počet dalších IR rámců za stisk (default 3, celkem 1 + repeat; ukládá se)
POST /api/heater/sync?on=1&mode=0|1|2    oprava odhadu, nic neposílá (0 ventilátor, 1 = I, 2 = II)
```

Po výpadku napájení se topení bere jako vypnuté; po crashi/WDT/flashi se použije uložený odhad (`/heater.json`). Ruční režim vypíná po 4 h. Oznámení (`loskukos_cmd`) chodí jen při chybách: chyba čidla, přehřátí (pokusy o vypnutí i selhání) a limit ručního režimu. Běžné akce (tlačítka, Sync, start/stop/běh AUTO) ntfy neposílají.

```
POST /api/heater/auto/start?target=21&minutes=60   (409 = čidlo nemá platnou teplotu)
POST /api/heater/auto/update?target=..&minutes=..  (změna času restartuje odpočet)
POST /api/heater/auto/stop                         (vypne topení)
```

AUTO: zapne (Power + Mode×2 = režim II) pod cílem, vypne od cíl+2 °C. Při cíl+4 °C (a topení má běžet nebo teplota roste) pošle Power; pokud 3 měření po sobě neklesají ani nestagnují, zkusí znovu, max 3×, pak AUTO ukončí. Chyba čidla nebo restart AUTO ruší.

## Známý problém: Mode vs. Spin

Kódy Mode (`0xD90`) a Spin (`0xD88`) se liší jen ve dvou bitech; při slabém signálu přímotop občas vyhodnotí Mode jako Spin. Pomáhá silnější signál (tranzistor, zamíření) a méně opakování: `curl -X POST "http://dash.local/api/heater/cfg?repeat=0"` (1 rámec) a porovnat spolehlivost. Originální ovladač na přijímači dává jeden výpis; zda jde o jeden rámec, ukáže délka `rawData`.
