#include "sensors.h"
#include <DHT.h>
#include "config.h"
#include "state.h"
#include "log_util.h"

static DHT dhtIndoor(PIN_DHT_INDOOR,  DHT11);
static DHT dhtOutdoor(PIN_DHT_OUTDOOR, DHT22);

void initSensors() {
  dhtIndoor.begin();
  dhtOutdoor.begin();
  logLine(F("[SENSORS] DHT11 (indoor) + DHT22 (outdoor) init"));
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
  return a || b;
}
