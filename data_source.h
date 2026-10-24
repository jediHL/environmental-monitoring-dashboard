#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

#include <stdbool.h>

#define FORECAST_DAYS 7

typedef struct {
    char date[11];
    double low;
    double high;
    double rainProbability;
    double rainMm;
    double windMax;
    int weatherCode;
} DailyForecast;

typedef struct {
    int locationId;
    double temperature;
    double humidity;
    double apparentTemperature;
    double wind;
    int weatherCode;
    double pm25;
    bool weatherValid;
    bool airValid;
    bool forecastValid;
    int dayCount;
    DailyForecast days[FORECAST_DAYS];
    char weatherTime[32];
    char airTime[32];
    char weatherError[160];
    char airError[160];
    char forecastError[160];
} EnvironmentData;

bool DataSourceInit(void);
void FetchEnvironmentData(int locationId, double latitude, double longitude,
                          EnvironmentData *result);
bool StartDataRefresh(int locationId, double latitude, double longitude);
bool PollDataRefresh(EnvironmentData *result);
void DataSourceClose(void);

/* Shared parsing and merging also allow deterministic offline tests. */
bool ParseWeatherJson(const char *text, EnvironmentData *result);
bool MergeEnvironmentData(EnvironmentData *cached, const EnvironmentData *incoming);
const char *WeatherDescription(int code);

#endif
