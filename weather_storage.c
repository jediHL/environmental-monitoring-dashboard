#include "weather_storage.h"
#include "counties.h"
#include "cJSON.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define MAX_CACHE_BYTES 65536
#define PATH_CAPACITY 4096

static bool Error(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return false;
}

void FormatUtcTime(time_t value, char *text, size_t capacity)
{
    if (!capacity) return;
    struct tm *utc = value > 0 ? gmtime(&value) : NULL;
    if (!utc || !strftime(text, capacity, "%Y-%m-%d %H:%M UTC", utc))
        snprintf(text, capacity, "unknown time");
}

bool MergeNewerSnapshot(WeatherSnapshot *cached, const WeatherSnapshot *candidate)
{
    if (!cached || !candidate || cached->data.locationId != candidate->data.locationId) return false;
    EnvironmentData incoming = candidate->data;
    incoming.weatherValid = incoming.weatherValid && (!cached->data.weatherValid ||
        candidate->weatherFetchedAt > cached->weatherFetchedAt ||
        (candidate->weatherFetchedAt == cached->weatherFetchedAt && incoming.daylightValid && !cached->data.daylightValid));
    incoming.forecastValid = incoming.forecastValid && (!cached->data.forecastValid || candidate->forecastFetchedAt > cached->forecastFetchedAt);
    incoming.airValid = incoming.airValid && (!cached->data.airValid || candidate->airFetchedAt > cached->airFetchedAt);
    if (!incoming.weatherValid && !incoming.forecastValid && !incoming.airValid) return false;
    MergeEnvironmentData(&cached->data, &incoming);
    if (incoming.weatherValid) cached->weatherFetchedAt = candidate->weatherFetchedAt;
    if (incoming.forecastValid) cached->forecastFetchedAt = candidate->forecastFetchedAt;
    if (incoming.airValid) cached->airFetchedAt = candidate->airFetchedAt;
    return true;
}

static bool IdValid(int id) { return id >= 0 && id < LOCATION_COUNT; }

static time_t ReadFetchTime(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble <= 0 || item->valuedouble > 4102444800.0 ||
        floor(item->valuedouble) != item->valuedouble) return 0;
    time_t value = (time_t)item->valuedouble;
    return (double)value == item->valuedouble ? value : 0;
}

/* Store current/daily fields in the API format so one validator serves both
   downloaded data and untrusted local cache files. */
static cJSON *EncodeWeather(const EnvironmentData *data)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    if (data->weatherValid) {
        cJSON *current = cJSON_AddObjectToObject(root, "current");
        char time[32]; snprintf(time, sizeof(time), "%s", data->weatherTime);
        if (strlen(time) == 16) time[10] = 'T';
        cJSON_AddStringToObject(current, "time", time);
        cJSON_AddNumberToObject(current, "temperature_2m", data->temperature);
        cJSON_AddNumberToObject(current, "relative_humidity_2m", data->humidity);
        cJSON_AddNumberToObject(current, "apparent_temperature", data->apparentTemperature);
        cJSON_AddNumberToObject(current, "wind_speed_10m", data->wind);
        cJSON_AddNumberToObject(current, "weather_code", data->weatherCode);
        if (data->daylightValid) cJSON_AddNumberToObject(current, "is_day", data->isDay ? 1 : 0);
    }
    if (data->forecastValid && data->dayCount == FORECAST_DAYS) {
        cJSON *daily = cJSON_AddObjectToObject(root, "daily");
        const char *keys[] = {"time", "temperature_2m_min", "temperature_2m_max",
            "precipitation_probability_max", "precipitation_sum", "wind_speed_10m_max", "weather_code"};
        for (int k = 0; k < 7; k++) {
            cJSON *array = cJSON_AddArrayToObject(daily, keys[k]);
            for (int i = 0; i < data->dayCount; i++) {
                const DailyForecast *day = &data->days[i];
                const double values[] = {0, day->low, day->high, day->rainProbability,
                                          day->rainMm, day->windMax, day->weatherCode};
                cJSON_AddItemToArray(array, k ? cJSON_CreateNumber(values[k]) : cJSON_CreateString(day->date));
            }
        }
    }
    return root;
}

static bool DecodeSnapshot(const cJSON *root, int id, WeatherSnapshot *out)
{
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *location = cJSON_GetObjectItemCaseSensitive(root, "locationId");
    const cJSON *town = cJSON_GetObjectItemCaseSensitive(root, "town");
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(root, "latitude");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(root, "longitude");
    if (!IdValid(id) || !cJSON_IsNumber(version) || version->valuedouble != 1 ||
        !cJSON_IsNumber(location) || location->valuedouble != id ||
        !cJSON_IsString(town) || strcmp(town->valuestring, LOCATIONS[id].town) != 0 ||
        !cJSON_IsNumber(lat) || !isfinite(lat->valuedouble) ||
        !cJSON_IsNumber(lon) || !isfinite(lon->valuedouble) ||
        fabs(lat->valuedouble - LOCATIONS[id].latitude) > .000001 ||
        fabs(lon->valuedouble - LOCATIONS[id].longitude) > .000001) return false;

    WeatherSnapshot result = {0};
    result.data.locationId = id;
    const cJSON *weather = cJSON_GetObjectItemCaseSensitive(root, "weatherData");
    char *text = cJSON_PrintUnformatted(weather);
    if (text) { ParseWeatherJson(text, &result.data); cJSON_free(text); }
    result.weatherFetchedAt = ReadFetchTime(root, "weatherFetchedAt");
    result.forecastFetchedAt = ReadFetchTime(root, "forecastFetchedAt");
    result.airFetchedAt = ReadFetchTime(root, "airFetchedAt");
    result.data.weatherValid = result.data.weatherValid && result.weatherFetchedAt > 0;
    result.data.forecastValid = result.data.forecastValid && result.forecastFetchedAt > 0;
    if (!result.data.forecastValid) result.data.dayCount = 0;
    const cJSON *air = cJSON_GetObjectItemCaseSensitive(root, "airData");
    const cJSON *pm = cJSON_GetObjectItemCaseSensitive(air, "pm25");
    const cJSON *time = cJSON_GetObjectItemCaseSensitive(air, "time");
    if (cJSON_IsNumber(pm) && isfinite(pm->valuedouble) && pm->valuedouble >= 0 &&
        cJSON_IsString(time) && strlen(time->valuestring) == 16 &&
        (time->valuestring[10] == ' ' || time->valuestring[10] == 'T') && result.airFetchedAt > 0) {
        char airTime[32]; snprintf(airTime, sizeof(airTime), "%s", time->valuestring); airTime[10] = 'T';
        if (IsDataTimestamp(airTime)) {
            result.data.airValid = true; result.data.pm25 = pm->valuedouble;
            airTime[10] = ' ';
            snprintf(result.data.airTime, sizeof(result.data.airTime), "%s", airTime);
        }
    }
    if (!result.data.weatherValid && !result.data.forecastValid && !result.data.airValid) return false;
    result.data.weatherError[0] = result.data.forecastError[0] = result.data.airError[0] = '\0';
    *out = result;
    return true;
}

static cJSON *EncodeSnapshot(const WeatherSnapshot *snapshot)
{
    const EnvironmentData *data = &snapshot->data;
    if (!IdValid(data->locationId)) return NULL;
    const CountyTown *town = &LOCATIONS[data->locationId];
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "locationId", data->locationId);
    cJSON_AddStringToObject(root, "town", town->town);
    cJSON_AddNumberToObject(root, "latitude", town->latitude);
    cJSON_AddNumberToObject(root, "longitude", town->longitude);
    cJSON_AddNumberToObject(root, "weatherFetchedAt", (double)snapshot->weatherFetchedAt);
    cJSON_AddNumberToObject(root, "forecastFetchedAt", (double)snapshot->forecastFetchedAt);
    cJSON_AddNumberToObject(root, "airFetchedAt", (double)snapshot->airFetchedAt);
    cJSON_AddItemToObject(root, "weatherData", EncodeWeather(data));
    if (data->airValid) {
        cJSON *air = cJSON_AddObjectToObject(root, "airData");
        cJSON_AddNumberToObject(air, "pm25", data->pm25);
        cJSON_AddStringToObject(air, "time", data->airTime);
    }
    return root;
}

static bool ValidSnapshot(const WeatherSnapshot *snapshot)
{
    /* Reject non-finite input before cJSON_CreateNumber: some cJSON versions
       cast NaN to int internally, which is undefined behaviour. */
    const EnvironmentData *data = &snapshot->data;
    if (!IdValid(data->locationId)) return false;
    if (data->weatherValid && (!isfinite(data->temperature) || !isfinite(data->humidity) ||
        !isfinite(data->apparentTemperature) || !isfinite(data->wind) ||
        !memchr(data->weatherTime, '\0', sizeof(data->weatherTime)))) return false;
    if (data->airValid && (!isfinite(data->pm25) || !memchr(data->airTime, '\0', sizeof(data->airTime)))) return false;
    if (data->forecastValid) {
        if (data->dayCount != FORECAST_DAYS) return false;
        for (int i = 0; i < data->dayCount; i++) {
            const DailyForecast *day = &data->days[i];
            if (!isfinite(day->low) || !isfinite(day->high) || !isfinite(day->rainProbability) ||
                !isfinite(day->rainMm) || !isfinite(day->windMax) ||
                !memchr(day->date, '\0', sizeof(day->date))) return false;
        }
    }
    cJSON *root = EncodeSnapshot(snapshot);
    WeatherSnapshot check;
    bool valid = root && DecodeSnapshot(root, snapshot->data.locationId, &check) &&
        check.data.weatherValid == snapshot->data.weatherValid &&
        check.data.forecastValid == snapshot->data.forecastValid &&
        check.data.airValid == snapshot->data.airValid;
    cJSON_Delete(root);
    return valid;
}

bool SaveWeatherSnapshot(const char *path, const WeatherSnapshot *snapshot, char *error, size_t capacity)
{
    if (error && capacity) error[0] = '\0';
    if (!path || !snapshot || !ValidSnapshot(snapshot)) return Error(error, capacity, "Invalid snapshot; nothing saved");
    char temporary[PATH_CAPACITY];
    int count = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (count < 0 || count >= (int)sizeof(temporary)) return Error(error, capacity, "Cache path is too long");
    cJSON *root = EncodeSnapshot(snapshot);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return Error(error, capacity, "Not enough memory to save data");
    FILE *file = fopen(temporary, "wb");
    if (!file) { cJSON_free(text); return Error(error, capacity, "Cannot write cache file"); }
    size_t length = strlen(text);
    bool written = fwrite(text, 1, length, file) == length;
    if (fclose(file) != 0) written = false;
    cJSON_free(text);
    if (!written) { remove(temporary); return Error(error, capacity, "Cache write failed; previous snapshot kept"); }
    /* Replace only after the complete new file is closed. Never remove the old one first. */
#ifdef _WIN32
    bool replaced = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    bool replaced = rename(temporary, path) == 0;
#endif
    if (!replaced) { remove(temporary); return Error(error, capacity, "Cannot replace cache; previous snapshot kept"); }
    return true;
}

bool LoadWeatherSnapshot(const char *path, int locationId, WeatherSnapshot *snapshot, char *error, size_t capacity)
{
    if (error && capacity) error[0] = '\0';
    if (!path || !snapshot || !IdValid(locationId)) return Error(error, capacity, "Invalid cache request");
    FILE *file = fopen(path, "rb");
    if (!file) return Error(error, capacity, "No readable saved snapshot");
    char *text = malloc(MAX_CACHE_BYTES + 1);
    if (!text) { fclose(file); return Error(error, capacity, "Not enough memory to read cache"); }
    size_t length = fread(text, 1, MAX_CACHE_BYTES, file);
    bool read = !ferror(file) && length > 0 && length < MAX_CACHE_BYTES;
    fclose(file); text[length] = '\0';
    cJSON *root = read && strlen(text) == length ? cJSON_ParseWithOpts(text, NULL, true) : NULL;
    WeatherSnapshot candidate;
    bool valid = root && DecodeSnapshot(root, locationId, &candidate);
    cJSON_Delete(root); free(text);
    if (!valid) return Error(error, capacity, "Saved snapshot is invalid or belongs to another town");
    *snapshot = candidate;
    return true;
}

static bool CsvText(FILE *file, const char *text)
{
    if (fputc('"', file) == EOF) return false;
    for (const char *p = text; *p; p++) {
        if (*p == '"' && fputc('"', file) == EOF) return false;
        if (fputc(*p, file) == EOF) return false;
    }
    return fputc('"', file) != EOF;
}

bool ExportForecastCsv(const char *path, const WeatherSnapshot *snapshot, bool savedForecast,
                       char *error, size_t capacity)
{
    if (error && capacity) error[0] = '\0';
    if (!path || !snapshot || !snapshot->data.forecastValid || !ValidSnapshot(snapshot))
        return Error(error, capacity, "No valid forecast to export");
    /* C11 exclusive creation prevents overwriting a previous export. */
    FILE *file = fopen(path, "wbx");
    if (!file) return Error(error, capacity, errno == EEXIST ? "This export already exists" : "Cannot create CSV file");
    const EnvironmentData *data = &snapshot->data;
    const CountyTown *town = &LOCATIONS[data->locationId];
    char fetched[40]; FormatUtcTime(snapshot->forecastFetchedAt, fetched, sizeof(fetched));
    bool ok = fprintf(file, "county,town,latitude,longitude,forecast_date,min_temp_c,max_temp_c,"
        "rain_probability_pct,precipitation_mm,max_wind_kmh,weather_code,conditions,"
        "forecast_fetched_utc,data_status,timezone,source\r\n") > 0;
    for (int i = 0; i < data->dayCount && ok; i++) {
        const DailyForecast *day = &data->days[i];
        ok = CsvText(file, COUNTIES[town->countyId].name) && fputc(',', file) != EOF && CsvText(file, town->town) &&
            fprintf(file, ",%.5f,%.5f,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%d,", town->latitude, town->longitude,
                day->date, day->low, day->high, day->rainProbability, day->rainMm, day->windMax, day->weatherCode) > 0 &&
            CsvText(file, WeatherDescription(day->weatherCode)) && fputc(',', file) != EOF && CsvText(file, fetched) &&
            fprintf(file, ",%s,Europe/Dublin,Open-Meteo (CC BY 4.0)\r\n",
                    savedForecast ? "saved_snapshot" : "downloaded_this_session") > 0;
    }
    if (fclose(file) != 0) ok = false;
    if (!ok) { remove(path); return Error(error, capacity, "CSV write failed; incomplete export removed"); }
    return true;
}
