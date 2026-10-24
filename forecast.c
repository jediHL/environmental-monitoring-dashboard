#include "data_source.h"
#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool Number(const cJSON *object, const char *key, double *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    *value = item->valuedouble;
    return true;
}

static bool ArrayNumber(const cJSON *object, const char *key, int index, double *value)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, key);
    const cJSON *item = cJSON_GetArrayItem(array, index);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    *value = item->valuedouble;
    return true;
}

static bool Code(double value)
{
    return value >= 0.0 && value <= 99.0 && value == (double)(int)value;
}

static bool Date(const char *value)
{
    if (!value) return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) { if (value[i] != '-') return false; }
        else if (value[i] < '0' || value[i] > '9') return false;
    }
    int year = 0, month = 0, day = 0;
    if (sscanf(value, "%d-%d-%d", &year, &month, &day) != 3 || month < 1 || month > 12) return false;
    const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maximum = lengths[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) maximum++;
    return day >= 1 && day <= maximum;
}

static bool Timestamp(const char *value)
{
    if (!value || strlen(value) != 16 || !Date(value) || value[10] != 'T' || value[13] != ':') return false;
    for (int i = 11; i < 16; i++) {
        if (i != 13 && (value[i] < '0' || value[i] > '9')) return false;
    }
    int hour = (value[11] - '0') * 10 + value[12] - '0';
    int minute = (value[14] - '0') * 10 + value[15] - '0';
    return hour < 24 && minute < 60;
}

bool ParseWeatherJson(const char *text, EnvironmentData *result)
{
    result->weatherValid = false;
    result->forecastValid = false;
    result->dayCount = 0;
    cJSON *root = cJSON_Parse(text);
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *time = cJSON_GetObjectItemCaseSensitive(current, "time");
    double code = 0.0;
    result->weatherValid =
        Number(current, "temperature_2m", &result->temperature) &&
        Number(current, "relative_humidity_2m", &result->humidity) &&
        Number(current, "apparent_temperature", &result->apparentTemperature) &&
        Number(current, "wind_speed_10m", &result->wind) &&
        Number(current, "weather_code", &code) && Code(code) &&
        result->humidity >= 0 && result->humidity <= 100 && result->wind >= 0 &&
        cJSON_IsString(time) && Timestamp(time->valuestring);
    if (result->weatherValid) {
        result->weatherCode = (int)code;
        snprintf(result->weatherTime, sizeof(result->weatherTime), "%s", time->valuestring);
        result->weatherTime[10] = ' ';
        result->weatherError[0] = '\0';
    } else {
        snprintf(result->weatherError, sizeof(result->weatherError),
                 "Current weather is missing or invalid");
    }

    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    const cJSON *dates = cJSON_GetObjectItemCaseSensitive(daily, "time");
    int count = cJSON_IsArray(dates) ? cJSON_GetArraySize(dates) : 0;
    bool valid = count >= FORECAST_DAYS;
    if (count > FORECAST_DAYS) count = FORECAST_DAYS;
    for (int i = 0; i < count && valid; i++) {
        const cJSON *date = cJSON_GetArrayItem(dates, i);
        DailyForecast *day = &result->days[i];
        double dayCode = 0.0;
        valid = cJSON_IsString(date) && date->valuestring &&
            strlen(date->valuestring) == 10 && Date(date->valuestring) &&
            (i == 0 || strcmp(date->valuestring, result->days[i - 1].date) > 0) &&
            ArrayNumber(daily, "temperature_2m_min", i, &day->low) &&
            ArrayNumber(daily, "temperature_2m_max", i, &day->high) &&
            ArrayNumber(daily, "precipitation_probability_max", i, &day->rainProbability) &&
            ArrayNumber(daily, "precipitation_sum", i, &day->rainMm) &&
            ArrayNumber(daily, "wind_speed_10m_max", i, &day->windMax) &&
            ArrayNumber(daily, "weather_code", i, &dayCode) && Code(dayCode) &&
            day->low <= day->high && day->rainProbability >= 0 &&
            day->rainProbability <= 100 && day->rainMm >= 0 && day->windMax >= 0;
        if (valid) {
            snprintf(day->date, sizeof(day->date), "%s", date->valuestring);
            day->weatherCode = (int)dayCode;
        }
    }
    result->forecastValid = valid;
    if (valid) {
        result->dayCount = count;
        result->forecastError[0] = '\0';
    } else {
        snprintf(result->forecastError, sizeof(result->forecastError),
                 "Forecast is missing or invalid");
    }
    cJSON_Delete(root);
    return result->weatherValid && result->forecastValid;
}

bool MergeEnvironmentData(EnvironmentData *cached, const EnvironmentData *incoming)
{
    if (cached->locationId != incoming->locationId) return false;
    if (incoming->weatherValid) {
        cached->temperature = incoming->temperature;
        cached->humidity = incoming->humidity;
        cached->apparentTemperature = incoming->apparentTemperature;
        cached->wind = incoming->wind;
        cached->weatherCode = incoming->weatherCode;
        cached->weatherValid = true;
        snprintf(cached->weatherTime, sizeof(cached->weatherTime), "%s", incoming->weatherTime);
    }
    if (incoming->forecastValid) {
        cached->forecastValid = true;
        cached->dayCount = incoming->dayCount;
        memcpy(cached->days, incoming->days, sizeof(cached->days));
    }
    if (incoming->airValid) {
        cached->airValid = true;
        cached->pm25 = incoming->pm25;
        snprintf(cached->airTime, sizeof(cached->airTime), "%s", incoming->airTime);
    }
    snprintf(cached->weatherError, sizeof(cached->weatherError), "%s", incoming->weatherError);
    snprintf(cached->forecastError, sizeof(cached->forecastError), "%s", incoming->forecastError);
    snprintf(cached->airError, sizeof(cached->airError), "%s", incoming->airError);
    return true;
}

const char *WeatherDescription(int code)
{
    switch (code) {
        case 0: return "Clear sky";
        case 1: return "Mainly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: case 67: return "Freezing rain";
        case 71: case 73: case 75: case 77: return "Snow";
        case 80: case 81: case 82: return "Rain showers";
        case 85: case 86: return "Snow showers";
        case 95: case 96: case 99: return "Thunderstorm";
        default: return "Unknown conditions";
    }
}
