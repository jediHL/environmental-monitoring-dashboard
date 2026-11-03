#include "weather_storage.h"
#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

/* Test-only values: include negative and zero temperatures. */
static WeatherSnapshot Fixture(void)
{
    WeatherSnapshot snapshot = {0};
    snapshot.weatherFetchedAt = 1793620800;
    snapshot.forecastFetchedAt = 1793620860;
    snapshot.airFetchedAt = 1793620920;
    EnvironmentData *data = &snapshot.data;
    data->locationId = 5;
    data->weatherValid = data->airValid = data->forecastValid = true;
    data->temperature = 0; data->apparentTemperature = -2;
    data->humidity = 76; data->wind = 10.5; data->weatherCode = 3; data->pm25 = 2.3;
    data->daylightValid = true; data->isDay = false;
    snprintf(data->weatherTime, sizeof(data->weatherTime), "2026-11-02 12:00");
    snprintf(data->airTime, sizeof(data->airTime), "2026-11-02 11:00");
    data->dayCount = FORECAST_DAYS;
    for (int i = 0; i < FORECAST_DAYS; i++) {
        DailyForecast *day = &data->days[i];
        snprintf(day->date, sizeof(day->date), "2026-11-%02d", i + 2);
        day->low = i - 3; day->high = i + .5;
        day->rainProbability = i * 10; day->rainMm = i * .5;
        day->windMax = 12 + i; day->weatherCode = 3;
    }
    return snapshot;
}

static char *Read(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *text = calloc(70000, 1);
    if (text) { size_t length = fread(text, 1, 69999, file); text[length] = '\0'; }
    fclose(file);
    return text;
}

static void Write(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb"); CHECK(file != NULL);
    if (file) { CHECK(fwrite(text, 1, strlen(text), file) == strlen(text)); CHECK(fclose(file) == 0); }
}

static void WriteJson(const char *path, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root); CHECK(text != NULL);
    if (text) Write(path, text);
    cJSON_free(text);
}

int main(void)
{
    char path[160], csv[160], secondCsv[160], badPath[200], error[160];
    unsigned long serial = (unsigned long)clock();
    snprintf(path, sizeof(path), "storage_test_%lld_%lu.json", (long long)time(NULL), serial);
    snprintf(csv, sizeof(csv), "storage_test_%lld_%lu.csv", (long long)time(NULL), serial);
    snprintf(secondCsv, sizeof(secondCsv), "storage_test_%lld_%lu_session.csv", (long long)time(NULL), serial);
    WeatherSnapshot original = Fixture(), loaded = {0};
    CHECK(SaveWeatherSnapshot(path, &original, error, sizeof(error)));
    CHECK(LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
    CHECK(loaded.data.weatherValid && loaded.data.forecastValid && loaded.data.airValid);
    CHECK(loaded.data.locationId == 5 && loaded.data.temperature == 0);
    CHECK(loaded.data.daylightValid && !loaded.data.isDay);
    CHECK(loaded.data.dayCount == FORECAST_DAYS && loaded.data.days[0].low == -3);
    CHECK(loaded.data.days[6].high == 6.5 && loaded.data.pm25 == 2.3);
    CHECK(loaded.weatherFetchedAt == original.weatherFetchedAt);
    CHECK(loaded.forecastFetchedAt == original.forecastFetchedAt);
    CHECK(loaded.airFetchedAt == original.airFetchedAt);
    {
        WeatherSnapshot cached = Fixture(), candidate = Fixture();
        candidate.weatherFetchedAt += 60; candidate.data.temperature = 7;
        candidate.forecastFetchedAt -= 60; candidate.data.days[0].low = -9;
        CHECK(MergeNewerSnapshot(&cached, &candidate));
        CHECK(cached.data.temperature == 7 && cached.data.days[0].low == -3);
        CHECK(cached.forecastFetchedAt == original.forecastFetchedAt);
        CHECK(!MergeNewerSnapshot(&cached, &candidate));
        candidate.data.locationId = 0;
        CHECK(!MergeNewerSnapshot(&cached, &candidate));
    }
    loaded.data.locationId = 18;
    CHECK(!LoadWeatherSnapshot(path, 0, &loaded, error, sizeof(error)));
    CHECK(loaded.data.locationId == 18); /* A failed read does not modify output. */

    original.data.temperature = 2;
    CHECK(SaveWeatherSnapshot(path, &original, error, sizeof(error)));
    CHECK(LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)) && loaded.data.temperature == 2);
    original.data.temperature = NAN;
    CHECK(!SaveWeatherSnapshot(path, &original, error, sizeof(error)));
    CHECK(LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)) && loaded.data.temperature == 2);
    original = Fixture(); original.data.days[0].high = INFINITY;
    CHECK(!SaveWeatherSnapshot(path, &original, error, sizeof(error)));
    original = Fixture(); original.data.pm25 = NAN;
    CHECK(!SaveWeatherSnapshot(path, &original, error, sizeof(error)));
    original = Fixture();
    snprintf(badPath, sizeof(badPath), "%s/not-a-directory.json", path);
    CHECK(!SaveWeatherSnapshot(badPath, &original, error, sizeof(error)));
    CHECK(LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));

    CHECK(ExportForecastCsv(csv, &loaded, true, error, sizeof(error)));
    char *text = Read(csv); CHECK(text != NULL);
    if (text) {
        CHECK(strstr(text, "min_temp_c,max_temp_c") != NULL);
        CHECK(strstr(text, "\"Dublin\",\"Dublin\",53.33306,-6.24889,2026-11-02,-3.0,0.5") != NULL);
        CHECK(strstr(text, ",saved_snapshot,Europe/Dublin,Open-Meteo (CC BY 4.0)") != NULL);
        CHECK(strstr(text, "2026-11-08") != NULL);
        int rows = 0; for (const char *p = text; *p; p++) if (*p == '\n') rows++;
        CHECK(rows == 8);
        CHECK(!ExportForecastCsv(csv, &loaded, true, error, sizeof(error)));
        char *after = Read(csv); CHECK(after && strcmp(after, text) == 0); free(after);
        free(text);
    }
    CHECK(ExportForecastCsv(secondCsv, &loaded, false, error, sizeof(error)));
    text = Read(secondCsv); CHECK(text && strstr(text, "downloaded_this_session")); free(text);

    text = Read(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    CHECK(root != NULL); free(text);
    if (root) {
        cJSON_ReplaceItemInObjectCaseSensitive(root, "version", cJSON_CreateNumber(2));
        WriteJson(path, root); CHECK(!LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
        cJSON_ReplaceItemInObjectCaseSensitive(root, "version", cJSON_CreateNumber(1));
        cJSON_ReplaceItemInObjectCaseSensitive(root, "town", cJSON_CreateString("Carlow"));
        WriteJson(path, root); CHECK(!LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
        cJSON_ReplaceItemInObjectCaseSensitive(root, "town", cJSON_CreateString("Dublin"));
        cJSON *weather = cJSON_GetObjectItemCaseSensitive(root, "weatherData");
        cJSON *current = cJSON_GetObjectItemCaseSensitive(weather, "current");
        cJSON_DeleteItemFromObjectCaseSensitive(current, "is_day");
        WriteJson(path, root); CHECK(LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
        CHECK(loaded.data.weatherValid && !loaded.data.daylightValid); /* Older snapshots remain usable. */
        cJSON_AddNumberToObject(current, "is_day", 1);
        WriteJson(path, root); CHECK(LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
        CHECK(loaded.data.daylightValid && loaded.data.isDay);
        cJSON *daily = cJSON_GetObjectItemCaseSensitive(weather, "daily");
        cJSON_ReplaceItemInArray(cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min"), 2, cJSON_CreateNull());
        WriteJson(path, root); CHECK(LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
        CHECK(loaded.data.weatherValid && loaded.data.airValid && !loaded.data.forecastValid && loaded.data.dayCount == 0);
        CHECK(!ExportForecastCsv(badPath, &loaded, true, error, sizeof(error)));
        cJSON_Delete(root);
    }
    Write(path, "{\"version\":1,");
    CHECK(!LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
    Write(path, "{\"version\":1} trailing garbage");
    CHECK(!LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
    FILE *large = fopen(path, "wb"); CHECK(large != NULL);
    if (large) { for (int i = 0; i < 66000; i++) fputc('x', large); fclose(large); }
    CHECK(!LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));

    WeatherSnapshot partial = Fixture();
    partial.data.weatherValid = partial.data.forecastValid = false; partial.data.dayCount = 0;
    CHECK(SaveWeatherSnapshot(path, &partial, error, sizeof(error)));
    CHECK(LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
    CHECK(!loaded.data.weatherValid && !loaded.data.forecastValid && loaded.data.airValid);
    partial = Fixture(); partial.data.airValid = false;
    CHECK(SaveWeatherSnapshot(path, &partial, error, sizeof(error)));
    CHECK(LoadWeatherSnapshot(path, 5, &loaded, error, sizeof(error)));
    CHECK(loaded.data.weatherValid && loaded.data.forecastValid && !loaded.data.airValid);
    partial = Fixture(); partial.forecastFetchedAt = 0;
    CHECK(!SaveWeatherSnapshot(path, &partial, error, sizeof(error)));
    partial = Fixture(); partial.data.dayCount = 8;
    CHECK(!SaveWeatherSnapshot(path, &partial, error, sizeof(error)));
    CHECK(!ExportForecastCsv(badPath, &partial, false, error, sizeof(error)));

    char stamp[40]; FormatUtcTime(0, stamp, sizeof(stamp)); CHECK(strcmp(stamp, "unknown time") == 0);
    FormatUtcTime(original.forecastFetchedAt, stamp, sizeof(stamp)); CHECK(strstr(stamp, "UTC") != NULL);
    CHECK(remove(path) == 0); CHECK(remove(csv) == 0); CHECK(remove(secondCsv) == 0);
    printf("Storage/CSV tests: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
