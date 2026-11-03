#include "weather.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include "config.h"
#include "state.h"
#include "log_util.h"

// =====================================================
// WMO weather code -> icon-key / CZ popis
// =====================================================

String wmoToIcon(int code) {
  if (code == 0)                      return "clear";
  if (code == 1 || code == 2)         return "partly-cloudy";
  if (code == 3)                      return "cloudy";
  if (code == 45 || code == 48)       return "fog";
  if (code >= 51 && code <= 57)       return "drizzle";
  if (code >= 61 && code <= 67)       return "rain";
  if (code >= 71 && code <= 77)       return "snow";
  if (code >= 80 && code <= 82)       return "rain";
  if (code == 85 || code == 86)       return "snow";
  if (code >= 95)                     return "thunder";
  return "unknown";
}

String wmoToCondition(int code) {
  switch (code) {
    case 0:  return "Jasno";
    case 1:  return "Skoro jasno";
    case 2:  return "Castecne oblacno";
    case 3:  return "Oblacno";
    case 45: case 48: return "Mlha";
    case 51: case 53: case 55: return "Slabe srazky";
    case 56: case 57: return "Mrznouci mrholeni";
    case 61: case 63: case 65: return "Dest";
    case 66: case 67: return "Mrznouci dest";
    case 71: case 73: case 75: return "Snezeni";
    case 77: return "Snehove zrno";
    case 80: case 81: case 82: return "Prehanky";
    case 85: case 86: return "Snehove prehanky";
    case 95: return "Bourka";
    case 96: case 99: return "Bourka s kroupami";
    default: return "Neznamo";
  }
}

// =====================================================
// MOON PHASE (Julian Date calculation, no API)
// =====================================================

// Vraci lunarni den 0-29 pro dane datum
static uint8_t calcMoonPhase(int year, int month, int day) {
  // Zeller/Conway algoritmus pro Julian Date
  if (month < 3) { year--; month += 12; }
  long A = year / 100;
  long B = 2 - A + A / 4;
  long JD = (long)(365.25 * (year + 4716))
           + (long)(30.6001 * (month + 1))
           + day + B - 1524;
  // Referencni nov: JD 2451550.1 (6.1.2000), synodicky mesic 29.530588 dne
  // (puvodni "% 30" driftovalo ~0.47 dne za cyklus -> fazi to casem posouvalo)
  double age = fmod((double)JD - 2451550.1, 29.530588853);
  if (age < 0) age += 29.530588853;
  int phase = (int)age;
  if (phase > 29) phase = 29;
  return (uint8_t)phase;
}

static String moonEmoji(uint8_t phase) {
  if (phase == 0)                    return "🌑";
  if (phase >= 1  && phase <= 6)     return "🌒";
  if (phase >= 7  && phase <= 8)     return "🌓";
  if (phase >= 9  && phase <= 13)    return "🌔";
  if (phase >= 14 && phase <= 15)    return "🌕";
  if (phase >= 16 && phase <= 20)    return "🌖";
  if (phase >= 21 && phase <= 22)    return "🌗";
  return "🌘";
}

static String moonName(uint8_t phase) {
  if (phase == 0)                    return "Nov";
  if (phase >= 1  && phase <= 6)     return "Dorost";
  if (phase >= 7  && phase <= 8)     return "Prvni ctvrt";
  if (phase >= 9  && phase <= 13)    return "Pribyvajici";
  if (phase >= 14 && phase <= 15)    return "Uplnek";
  if (phase >= 16 && phase <= 20)    return "Ubyvajici";
  if (phase >= 21 && phase <= 22)    return "Posledni ctvrt";
  return "Ubyvajici srpek";
}

// =====================================================
// POLL
// =====================================================

bool pollWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    logLine(F("[WEATHER] WiFi not connected"));
    return false;
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  String url = String("https://api.open-meteo.com/v1/forecast")
              + "?latitude="  WEATHER_LAT
              + "&longitude=" WEATHER_LON
              + "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
              + "&hourly=temperature_2m,weather_code"
              + "&daily=sunrise,sunset"
              + "&forecast_days=2&timezone=Europe%2FPrague";

  if (!http.begin(client, url)) {
    logLine(F("[WEATHER] http.begin failed"));
    return false;
  }
  http.setTimeout(4000);

  int httpCode = http.GET();
  if (httpCode != 200) {
    logValue(F("[WEATHER] HTTP code: "), httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    logValue(F("[WEATHER] JSON parse fail: "), String(err.c_str()));
    return false;
  }

  float  curTemp     = doc["current"]["temperature_2m"]    | NAN;
  float  appTemp     = doc["current"]["apparent_temperature"] | NAN;
  float  curWind     = doc["current"]["wind_speed_10m"]    | NAN;
  int    curCode     = doc["current"]["weather_code"]      | -1;
  String curTime     = doc["current"]["time"]              | "";

  if (isnan(curTemp) || curCode < 0 || curTime.isEmpty()) {
    logLine(F("[WEATHER] missing current fields"));
    return false;
  }

  // sunrise/sunset z daily[0] - format "YYYY-MM-DDTHH:MM"
  String sunriseRaw = doc["daily"]["sunrise"][0] | "";
  String sunsetRaw  = doc["daily"]["sunset"][0]  | "";

  auto extractHHMM = [](const String &s) -> String {
    int ti = s.indexOf('T');
    if (ti < 0 || s.length() < ti + 6) return "--:--";
    return s.substring(ti + 1, ti + 6);
  };
  auto toMinutes = [](const String &hhmm) -> int {
    if (hhmm.length() < 5) return 0;
    return hhmm.substring(0, 2).toInt() * 60 + hhmm.substring(3, 5).toInt();
  };

  String sunriseHHMM = extractHHMM(sunriseRaw);
  String sunsetHHMM  = extractHHMM(sunsetRaw);
  int    sunriseMin  = toMinutes(sunriseHHMM);
  int    sunsetMin   = toMinutes(sunsetHHMM);

  // moon phase z aktualniho datumu (NTP)
  int tIdx = curTime.indexOf('T');
  uint8_t phase = 0;
  if (tIdx >= 10) {
    int yr  = curTime.substring(0, 4).toInt();
    int mo  = curTime.substring(5, 7).toInt();
    int dy  = curTime.substring(8, 10).toInt();
    phase = calcMoonPhase(yr, mo, dy);
  }

  // hodinova predpoved
  int tIdxC = curTime.indexOf('T');
  String curDate = (tIdxC >= 0) ? curTime.substring(0, tIdxC) : "";
  int curHour    = (tIdxC >= 0) ? curTime.substring(tIdxC + 1, tIdxC + 3).toInt() : -1;

  JsonArray hTimes = doc["hourly"]["time"].as<JsonArray>();
  JsonArray hTemps = doc["hourly"]["temperature_2m"].as<JsonArray>();
  JsonArray hCodes = doc["hourly"]["weather_code"].as<JsonArray>();

  ForecastEntry tmpForecast[WEATHER_FORECAST_HOURS];
  int forecastCount = 0;

  if (curHour >= 0 && !hTimes.isNull()) {
    int startIdx = -1;
    for (size_t i = 0; i < hTimes.size(); i++) {
      String t = hTimes[i].as<String>();
      int ti = t.indexOf('T');
      if (ti < 0) continue;
      if (t.substring(0, ti) == curDate &&
          t.substring(ti + 1, ti + 3).toInt() == curHour) {
        startIdx = (int)i; break;
      }
    }
    for (int k = 1; k <= WEATHER_FORECAST_HOURS && startIdx >= 0
                     && (startIdx + k) < (int)hTimes.size(); k++) {
      int idx = startIdx + k;
      String t = hTimes[idx].as<String>();
      int ti = t.indexOf('T');
      tmpForecast[forecastCount].hour = (ti >= 0) ? t.substring(ti+1, ti+3).toInt() : 0;
      tmpForecast[forecastCount].temp = hTemps[idx] | curTemp;
      tmpForecast[forecastCount].code = hCodes[idx] | curCode;
      String datePart = (ti >= 0) ? t.substring(0, ti) : "";
      strncpy(tmpForecast[forecastCount].date, datePart.c_str(), 10);
      tmpForecast[forecastCount].date[10] = '\0';
      forecastCount++;
    }
  }

  if (lockMutex(weatherMutex)) {
    weather.valid         = true;
    weather.temp          = curTemp;
    weather.apparentTemp  = isnan(appTemp) ? curTemp : appTemp;
    weather.windSpeed     = curWind;
    weather.code          = curCode;
    weather.condition     = wmoToCondition(curCode);
    weather.icon          = wmoToIcon(curCode);
    weather.sunrise       = sunriseHHMM;
    weather.sunset        = sunsetHHMM;
    weather.sunriseMin    = sunriseMin;
    weather.sunsetMin     = sunsetMin;
    weather.moonPhase     = phase;
    weather.moonEmoji     = moonEmoji(phase);
    weather.moonName      = moonName(phase);
    weather.updatedAtMs   = millis();
    weather.forecastCount = forecastCount;
    for (int i = 0; i < forecastCount; i++) weather.forecast[i] = tmpForecast[i];
    unlockMutex(weatherMutex);
  }

  logValue(F("[WEATHER] temp: "), String(curTemp, 1));
  logValue(F("[WEATHER] apparent: "), String(appTemp, 1));
  logValue(F("[WEATHER] sunrise: "), sunriseHHMM);
  logValue(F("[WEATHER] sunset: "), sunsetHHMM);
  logValue(F("[WEATHER] moon phase: "), (int)phase);
  return true;
}
