# RFID čtečka RC522 – tagy a zapojení

Čtečka RC522 (13,56 MHz) na ESP32, zatím jako základ pro "smart switch": přiložení karty přepne výstup (relé / LED). Otestováno na klasickém ESP32 DevKitu (30pin), ne na ESP32-S3 z dashboardu.

## Tagy

Dvě vlastněné karty, obě MIFARE Classic 1K (4bajtové UID), přiřazené jednomu uživateli:

| # | UID | Typ | Uživatel |
|---|-----|-----|----------|
| 1 | `B4 00 2E 07` | MIFARE Classic 1K | Kuba |
| 2 | `C1 E4 17 07` | MIFARE Classic 1K | Kuba |

UID není tajné a u Classic jde zkopírovat, takže je to vhodné na pohodlí, ne na zámek.

## Pinout (RC522 → ESP32 DevKit, VSPI)

| RC522 | ESP32 | Poznámka |
|-------|-------|----------|
| 3.3V  | 3V3   | **jen 3,3 V**, 5 V čtečku zničí |
| GND   | GND   | |
| SDA (SS) | GPIO 5  | |
| SCK   | GPIO 18 | |
| MOSI  | GPIO 23 | |
| MISO  | GPIO 19 | |
| RST   | GPIO 22 | |
| IRQ   | nezapojeno | |

Relé / LED pro smart switch: GPIO 4 (není strapping pin). Moduly relé bývají active-low.

### Pozor při přesunu na dashboard (ESP32-S3)

Tento pinout na S3 z dashboardu nejde použít 1:1:

- GPIO 5 a 4 už jsou obsazené DHT senzory (`src/config.h`).
- GPIO 19 / 20 jsou na S3 USB, GPIO 22–25 neexistují, GPIO 26–37 jsou flash/PSRAM.
- Je potřeba zvolit nové SPI piny a přepsat `SPI.begin(SCK, MISO, MOSI, SS)`.

## Knihovna

```ini
lib_deps =
    miguelbalboa/MFRC522@^1.4.11
```

## Kód pro čtení UID

```cpp
#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN   5
#define RST_PIN  22
#define SCK_PIN  18
#define MISO_PIN 19
#define MOSI_PIN 23

MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  rfid.PCD_DumpVersionToSerial();  // 0x91 / 0x92 / 0x82 = OK, 0x00 / 0xFF = chyba zapojení
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  Serial.print("UID:");
  for (byte i = 0; i < rfid.uid.size; i++) Serial.printf(" %02X", rfid.uid.uidByte[i]);
  Serial.println();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
```

## Tabulka UID → uživatel

Víc karet může patřit jednomu uživateli, ztracená karta se smaže jedním řádkem:

```cpp
struct Tag { byte uid[4]; const char* user; };

Tag tags[] = {
  {{0xB4,0x00,0x2E,0x07}, "Kuba"},
  {{0xC1,0xE4,0x17,0x07}, "Kuba"},
};

const char* findUser(MFRC522::Uid& u) {
  if (u.size != 4) return nullptr;
  for (auto& t : tags)
    if (memcmp(t.uid, u.uidByte, 4) == 0) return t.user;
  return nullptr;
}
```

## Poznámky

- Verze čipu `0x82` je klon, funguje stejně jako originál (`0x91` / `0x92`).
- Reálný dosah je 2–3 cm. Přes desku stolu čte jen na hraně dosahu, což způsobuje opakované čtení stejné karty. Řešení: ignorovat stejné UID 1 s (`millis()` debounce).
- Výpadky / smetí v sériovém monitoru: kondenzátor 100 µF + 100 nF mezi 3V3 a GND u modulu, krátké dupont kabely, kvalitní USB kabel.
- Kapacita Classic 1K: 752 B použitelných dat (47 datových bloků po 16 B). Blok 0 a každý 4. blok (sector trailer, 3, 7, 11 …) se nezapisují.
- Seznam tagů jde uložit do LittleFS (`board_build.filesystem = littlefs`), do tisíců záznamů.

## Stav

- [x] Čtečka zapojená a čte UID obou karet
- [ ] Debounce stejného UID
- [ ] Smart switch (přepnutí relé při přiložení)
- [ ] Tabulka tagů v LittleFS, přidávání karet přes sériový monitor
- [ ] Přesunout na ESP32-S3 dashboardu (nové piny)
