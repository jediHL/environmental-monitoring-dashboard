#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

#include <stdbool.h>

typedef struct {
    double temperature;
    double humidity;
    double pm25;
    bool weatherValid;
    bool airValid;
    char weatherTime[32];
    char airTime[32];
    char weatherError[160];
    char airError[160];
} EnvironmentData;

bool DataSourceInit(void);
void FetchEnvironmentData(EnvironmentData *result);
bool StartDataRefresh(void);
bool PollDataRefresh(EnvironmentData *result);
void DataSourceClose(void);

#endif
