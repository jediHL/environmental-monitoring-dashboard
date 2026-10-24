#ifndef COUNTIES_H
#define COUNTIES_H

#include <stdbool.h>

#define COUNTY_COUNT 26
#define LOCATION_COUNT 27

typedef struct {
    const char *name;
    const char *province;
    int firstLocation;
    int townCount;
} County;

typedef struct {
    int countyId;
    const char *town;
    double latitude;
    double longitude;
} CountyTown;

extern const County COUNTIES[COUNTY_COUNT];
extern const CountyTown LOCATIONS[LOCATION_COUNT];
bool CountyMatches(int countyId, const char *search);
int FindCountyTown(int countyId, const char *search);
int FindLocation(const char *name);

#endif
