#include "sensors.h"
#include <DHT.h>
#include "config.h"
#include "state.h"
#include "log_util.h"

static DHT dhtIndoor(PIN_DHT_INDOOR,  DHT11);
static DHT dhtOutdoor(PIN_DHT_OUTDOOR, DHT22);

static bool hasOutdoorPwrPin() { return PIN_DHT_OUTDOOR_PWR >= 0; }

void initSensors() {
  if (hasOutdoorPwrPin()) {
    pinMode(PIN_DHT_OUTDOOR_PWR, OUTPUT);
    digitalWrite(PIN_DHT_OUTDOOR_PWR, HIGH);
    delay(DHT_PWR_SETTLE_MS);   // nechat senzor nabehnout, setup() jeste nebezi v tasku
  }
  dhtIndoor.begin();
  dhtOutdoor.begin();
  logLine(F("[SENSORS] DHT11 (indoor) + DHT22 (outdoor) init"));
  if (hasOutdoorPwrPin()) logValue(F("[SENSORS] outdoor DHT22 power pin: GPIO "), PIN_DHT_OUTDOOR_PWR);
}

// Vypne a zapne napajeni venkovniho DHT22 (blokuje ~4 s, volat jen z ClimateTask).
// Pred vypnutim zaloguje stav datoveho pinu: LOW = zaseknuty senzor nebo zkrat,
// HIGH = linka volna (pull-up), senzor jen neodpovida.
static void powerCycleOutdoor() {
  const int level = digitalRead(PIN_DHT_OUTDOOR);
  logLine(String("[SENSORS] outdoor power-cycle, data pin = ") + (level ? "HIGH" : "LOW"));

  digitalWrite(PIN_DHT_OUTDOOR_PWR, LOW);
  vTaskDelay(pdMS_TO_TICKS(DHT_PWR_OFF_MS));
  digitalWrite(PIN_DHT_OUTDOOR_PWR, HIGH);
  vTaskDelay(pdMS_TO_TICKS(DHT_PWR_SETTLE_MS));
  dhtOutdoor.begin();   // znovu nastavi pin a vynuluje interval cteni knihovny
}

// kruhovy buffer minutovych vzorku teploty pro vypocet trendu
struct TempHistory {
  float vals[CLIMATE_TREND_SAMPLES];
  int   count = 0;
  int   head  = 0;   // kam se zapise dalsi vzorek
};
static TempHistory histIndoor, histOutdoor;

static void pushHistory(TempHistory &h, float t, float &trendOut, int &trendMinOut) {
  // nejstarsi vzorek v okne (pred zapisem noveho)
  if (h.count >= CLIMATE_TREND_MIN_SAMPLES) {
    int oldest = (h.count < CLIMATE_TREND_SAMPLES) ? 0 : h.head;
    trendOut    = t - h.vals[oldest];
    trendMinOut = h.count;
  } else {
    trendOut = 0;
    trendMinOut = 0;
  }
  h.vals[h.head] = t;
  h.head = (h.head + 1) % CLIMATE_TREND_SAMPLES;
  if (h.count < CLIMATE_TREND_SAMPLES) h.count++;
}

static bool readInto(DHT &dht, DhtReading &r, TempHistory &hist, const char *label) {
  // cteni DHT je blokujici (~20-250 ms) - delat mimo mutex
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  bool ok = !isnan(t) && !isnan(h);

  if (lockMutex(climateMutex)) {
    if (ok) {
      r.temp = t;
      r.hum  = h;
      r.fails = 0;
      r.valid = true;
      r.updatedAtMs = millis();
      pushHistory(hist, t, r.trend, r.trendMin);
    } else {
      r.fails++;
      if (r.fails >= CLIMATE_MAX_FAILS) r.valid = false;
    }
    unlockMutex(climateMutex);
  }

  if (!ok) logValue(F("[SENSORS] read failed: "), label);
  return ok;
}

bool pollClimate() {
  bool a = readInto(dhtIndoor,  climate.indoor,  histIndoor,  "indoor");
  bool b = readInto(dhtOutdoor, climate.outdoor, histOutdoor, "outdoor");

  // Venkovni senzor se zasekl? Zkusit automaticky "odpojit/pripojit" (s cooldownem).
  if (!b && hasOutdoorPwrPin()) {
    int fails = 0;
    uint32_t lastCycle = 0;
    if (lockMutex(climateMutex)) {
      fails = climate.outdoor.fails;
      lastCycle = climate.outdoor.lastCycleMs;
      unlockMutex(climateMutex);
    }
    const bool cooldownOk = (lastCycle == 0) || (millis() - lastCycle >= DHT_POWER_CYCLE_COOLDOWN_MS);
    if (fails >= DHT_POWER_CYCLE_FAILS && cooldownOk) {
      powerCycleOutdoor();
      if (lockMutex(climateMutex)) {
        climate.outdoor.pwrCycles++;
        climate.outdoor.lastCycleMs = millis();
        unlockMutex(climateMutex);
      }
      b = readInto(dhtOutdoor, climate.outdoor, histOutdoor, "outdoor (po power-cyclu)");
      logLine(b ? F("[SENSORS] outdoor DHT22 po power-cyclu OK")
                : F("[SENSORS] outdoor DHT22 po power-cyclu porad nic"));
    }
  }
  return a || b;
}
