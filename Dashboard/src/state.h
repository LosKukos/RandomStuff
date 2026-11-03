#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <time.h>
#include "config.h"

// =====================================================
// MUTEXES
// =====================================================

extern SemaphoreHandle_t serialMutex;
extern SemaphoreHandle_t ledMutex;
extern SemaphoreHandle_t stateMutex;
extern SemaphoreHandle_t deviceMutex;
extern SemaphoreHandle_t alarmMutex;
extern SemaphoreHandle_t weatherMutex;
extern SemaphoreHandle_t climateMutex;

// vytvori vsechny mutexy - volat jednou v setup() pred vsim ostatnim
void initMutexes();

// =====================================================
// LOCK HELPERS
// =====================================================

bool lockMutex(SemaphoreHandle_t mtx, TickType_t timeout = pdMS_TO_TICKS(1000));
void unlockMutex(SemaphoreHandle_t mtx);

// =====================================================
// BOOT COUNTER (persists across deep sleep / soft reset)
// =====================================================

extern RTC_DATA_ATTR uint32_t bootCounter;

// =====================================================
// GLOBAL FLAGS
// =====================================================

void setNetReady(bool v);
bool getNetReady();

void setTimeReady(bool v);
bool getTimeReady();

void setLogicDone(bool v);
bool getLogicDone();

void setFatalError(bool v);
bool getFatalError();

void setNeedsUserAttention(bool v);
bool getNeedsUserAttention();

void setAutoSendDone(bool v);
bool getAutoSendDone();

void setLastSendOk(bool v);
bool getLastSendOk();

// =====================================================
// TIME / SVATKY STATE
// =====================================================

// timeinfo je chranena stateMutex - pristup pouze pres
// funkce v time_svatky.h (updateLocalTime/printTimeInfo)
extern struct tm timeinfo;

void setCurrentDateKey(const String &v);
String getCurrentDateKey();

void setCurrentFullDate(const String &v);
String getCurrentFullDate();

void setCurrentNames(const String &v);
String getCurrentNames();

void setLastSent(const String &v);
String getLastSent();

// =====================================================
// DEVICE STATE
// =====================================================

struct DeviceState {
  String   id;
  String   name;
  String   type;
  String   ip;
  String   host;
  bool     online;
  int      rssi;
  uint32_t uptime;
  uint32_t lastSeenMs;
  bool     power;
  int      brightness;
  uint32_t heap;
  String   mac;
  String   role;
  String   status;
  String   chip;
  String   version;
  String   error;
};

extern DeviceState wledPokoj;
extern DeviceState mcEsp;
extern DeviceState panicNode;

// =====================================================
// ALARM STATE
// =====================================================

struct AlarmState {
  bool     active;
  String   eventId;
  String   deviceId;
  String   deviceName;
  uint32_t receivedAtMs;
  bool     acked;
  String   type;
  uint32_t lastNtfyAt;
  int      ntfyCount;
};

extern AlarmState activeAlarm;

// =====================================================
// WEATHER STATE (Open-Meteo)
// =====================================================

struct ForecastEntry {
  int   hour;
  float temp;
  int   code;
  char  date[11];   // "YYYY-MM-DD" – pro detekci prelomu dne
};

struct WeatherState {
  bool     valid;
  float    temp;
  float    apparentTemp;
  float    windSpeed;
  int      code;
  String   condition;
  String   icon;
  String   sunrise;      // "HH:MM"
  String   sunset;       // "HH:MM"
  int      sunriseMin;   // minuty od pulnoci (pro countdown vypocet)
  int      sunsetMin;
  uint8_t  moonPhase;    // 0-29 (lunarni den)
  String   moonEmoji;
  String   moonName;
  uint32_t updatedAtMs;
  ForecastEntry forecast[WEATHER_FORECAST_HOURS];
  int      forecastCount;
};

extern WeatherState weather;

// =====================================================
// CLIMATE STATE (DHT11 vnitrni / DHT22 venkovni)
// =====================================================

struct DhtReading {
  bool     valid;        // posledni cteni OK (nebo < CLIMATE_MAX_FAILS selhani po sobe)
  float    temp;
  float    hum;
  int      fails;        // pocet selhani po sobe
  uint32_t updatedAtMs;  // millis() posledniho uspesneho cteni
  float    trend;        // zmena teploty za trendMin minut (°C)
  int      trendMin;     // delka okna trendu v minutach; 0 = zatim malo dat
};

struct ClimateState {
  DhtReading indoor;
  DhtReading outdoor;
};

extern ClimateState climate;

// =====================================================
// DESKTOP STATE (health check + WOL), chraneno deviceMutex
// =====================================================

struct DesktopState {
  bool     known;         // uz proběhla aspon jedna kontrola
  bool     online;
  uint32_t lastCheckMs;   // millis() posledni kontroly (0 = nikdy)
  uint32_t wolSentAtMs;   // millis() posledniho odeslani WOL (0 = nikdy)
};

extern DesktopState desktop;
