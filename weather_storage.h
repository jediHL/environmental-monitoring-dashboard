#ifndef WEATHER_STORAGE_H
#define WEATHER_STORAGE_H

#include "data_source.h"
#include <stddef.h>
#include <time.h>

/* Fetch times are UTC, independent for each service/section. */
typedef struct {
    EnvironmentData data;
    time_t weatherFetchedAt;
    time_t forecastFetchedAt;
    time_t airFetchedAt;
} WeatherSnapshot;

bool SaveWeatherSnapshot(const char *path, const WeatherSnapshot *snapshot,
                         char *error, size_t capacity);
bool LoadWeatherSnapshot(const char *path, int locationId, WeatherSnapshot *snapshot,
                         char *error, size_t capacity);
bool ExportForecastCsv(const char *path, const WeatherSnapshot *snapshot,
                       bool savedForecast, char *error, size_t capacity);
void FormatUtcTime(time_t value, char *text, size_t capacity);
bool MergeNewerSnapshot(WeatherSnapshot *cached, const WeatherSnapshot *candidate);

#endif
