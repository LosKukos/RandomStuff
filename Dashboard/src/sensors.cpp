#include "sensors.h"
#include "config.h"
#include "state.h"
#include "log_util.h"
#include "breadcrumbs.h"

// =====================================================
// VLASTNI CTENI DHT11 / DHT22
// =====================================================
// Knihovna Adafruit DHT vypina preruseni a pri odmlcenem senzoru ceka na kazdy
// ze 80 pulsu az do dlouheho timeoutu (na ESP32 desitky ms az sekundy), coz
// vede na interrupt watchdog restart. Tady ma kazda hrana vlastni kratky
// timeout a pri prvni chybe se cteni hned ukonci, takze preruseni jsou
// vypnuta nejvyse ~5 ms i kdyz je linka mrtva.

enum DhtErr : uint8_t { DHT_OK = 0, DHT_ERR_NORESP, DHT_ERR_TIMEOUT, DHT_ERR_CRC, DHT_ERR_RANGE };

static const char *dhtErrText(DhtErr e) {
  switch (e) {
    case DHT_OK:          return "ok";
    case DHT_ERR_NORESP:  return "senzor neodpovida";
    case DHT_ERR_TIMEOUT: return "timeout v ramci / linka drzi LOW";
    case DHT_ERR_CRC:     return "chybny checksum";
    case DHT_ERR_RANGE:   return "hodnota mimo rozsah";
  }
  return "?";
}

struct DhtSensor {
  uint8_t  pin;
  bool     isDht22;
  BcStep   step;         // breadcrumb pro toto cteni
  uint32_t lastReadMs;   // 0 = jeste necteno
};

static DhtSensor dhtIndoor  = { PIN_DHT_INDOOR,  false, BS_DHT_IN,  0 };   // DHT11
static DhtSensor dhtOutdoor = { PIN_DHT_OUTDOOR, true,  BS_DHT_OUT, 0 };   // DHT22

static const uint32_t DHT_MIN_INTERVAL_MS = 2000;   // senzory potrebuji pauzu mezi cteni
static portMUX_TYPE dhtMux = portMUX_INITIALIZER_UNLOCKED;

// Ceka, dokud ma linka uroven `level`. Vraci delku trvani v us, nebo -1 pri timeoutu.
static inline int32_t dhtPulse(uint8_t pin, int level, uint32_t timeoutUs) {
  const uint32_t start = micros();
  while (digitalRead(pin) == level) {
    if ((uint32_t)(micros() - start) > timeoutUs) return -1;
  }
  return (int32_t)(micros() - start);
}

static DhtErr dhtReadRaw(uint8_t pin, bool isDht22, uint8_t data[5]) {
  // start signal: linka na chvili HIGH, pak LOW (DHT22 >= 1 ms, DHT11 >= 18 ms)
  pinMode(pin, INPUT_PULLUP);
  delay(1);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(isDht22 ? 2 : 20);

  int16_t hi[40];   // delky HIGH pulsu jednotlivych bitu (us)
  DhtErr err = DHT_OK;

  portENTER_CRITICAL(&dhtMux);
  pinMode(pin, INPUT_PULLUP);
  if      (dhtPulse(pin, HIGH, 200) < 0) err = DHT_ERR_NORESP;    // cekani na odpoved senzoru
  else if (dhtPulse(pin, LOW,  150) < 0) err = DHT_ERR_TIMEOUT;   // ~80 us LOW
  else if (dhtPulse(pin, HIGH, 150) < 0) err = DHT_ERR_TIMEOUT;   // ~80 us HIGH
  else {
    for (int i = 0; i < 40; i++) {
      if (dhtPulse(pin, LOW, 100) < 0) { err = DHT_ERR_TIMEOUT; break; }   // ~50 us LOW
      const int32_t h = dhtPulse(pin, HIGH, 150);                           // ~27 us = 0, ~70 us = 1
      if (h < 0) { err = DHT_ERR_TIMEOUT; break; }
      hi[i] = (int16_t)h;
    }
  }
  portEXIT_CRITICAL(&dhtMux);

  if (err != DHT_OK) return err;

  memset(data, 0, 5);
  for (int i = 0; i < 40; i++) {
    data[i / 8] <<= 1;
    if (hi[i] > 40) data[i / 8] |= 1;
  }
  if (((data[0] + data[1] + data[2] + data[3]) & 0xFF) != data[4]) return DHT_ERR_CRC;
  return DHT_OK;
}

static DhtErr dhtRead(DhtSensor &s, float &t, float &h) {
  if (s.lastReadMs != 0) {
    const uint32_t since = millis() - s.lastReadMs;
    if (since < DHT_MIN_INTERVAL_MS) vTaskDelay(pdMS_TO_TICKS(DHT_MIN_INTERVAL_MS - since));
  }

  uint8_t d[5];
  const DhtErr e = dhtReadRaw(s.pin, s.isDht22, d);
  s.lastReadMs = millis();
  if (e != DHT_OK) return e;

  if (s.isDht22) {
    h = (float)((d[0] << 8) | d[1]) * 0.1f;
    t = (float)(((d[2] & 0x7F) << 8) | d[3]) * 0.1f;
    if (d[2] & 0x80) t = -t;
  } else {
    h = d[0] + d[1] * 0.1f;
    t = d[2];
    if (d[3] & 0x80) t = -1 - t;
    t += (d[3] & 0x0F) * 0.1f;
  }
  if (h < 0 || h > 100 || t < -40 || t > 80) return DHT_ERR_RANGE;
  return DHT_OK;
}

static bool hasOutdoorPwrPin() { return PIN_DHT_OUTDOOR_PWR >= 0; }

void initSensors() {
  if (hasOutdoorPwrPin()) {
    pinMode(PIN_DHT_OUTDOOR_PWR, OUTPUT);
    digitalWrite(PIN_DHT_OUTDOOR_PWR, HIGH);
    delay(DHT_PWR_SETTLE_MS);   // nechat senzor nabehnout, setup() jeste nebezi v tasku
  }
  pinMode(dhtIndoor.pin,  INPUT_PULLUP);
  pinMode(dhtOutdoor.pin, INPUT_PULLUP);
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
  pinMode(dhtOutdoor.pin, INPUT_PULLUP);
  dhtOutdoor.lastReadMs = 0;   // po zapnuti uz je pauza dodrzena
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

static bool readInto(DhtSensor &dht, DhtReading &r, TempHistory &hist, const char *label) {
  // cteni DHT je blokujici (~5-30 ms) - delat mimo mutex
  bcMark(BT_CLIMATE, dht.step);
  float t = NAN, h = NAN;
  const DhtErr err = dhtRead(dht, t, h);
  bcMark(BT_CLIMATE, BS_CLIMATE_READ);
  bool ok = (err == DHT_OK);

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

  if (!ok) logLine(String("[SENSORS] read failed: ") + label + " (" + dhtErrText(err) + ")");
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
