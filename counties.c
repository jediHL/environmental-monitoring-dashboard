#include "counties.h"

#include <ctype.h>
#include <string.h>

/* Traditional counties, not the separate modern local-authority areas. */
const County COUNTIES[COUNTY_COUNT] = {
    {"Carlow", "Leinster", 0, 1},
    {"Cavan", "Ulster", 1, 1},
    {"Clare", "Munster", 2, 1},
    {"Cork", "Munster", 3, 1},
    {"Donegal", "Ulster", 4, 1},
    {"Dublin", "Leinster", 5, 1},
    {"Galway", "Connacht", 6, 1},
    {"Kerry", "Munster", 7, 1},
    {"Kildare", "Leinster", 8, 1},
    {"Kilkenny", "Leinster", 9, 1},
    {"Laois", "Leinster", 10, 1},
    {"Leitrim", "Connacht", 11, 1},
    {"Limerick", "Munster", 12, 1},
    {"Longford", "Leinster", 13, 1},
    {"Louth", "Leinster", 14, 1},
    {"Mayo", "Connacht", 15, 1},
    {"Meath", "Leinster", 16, 1},
    {"Monaghan", "Ulster", 17, 1},
    {"Offaly", "Leinster", 18, 1},
    {"Roscommon", "Connacht", 19, 1},
    {"Sligo", "Connacht", 20, 1},
    {"Tipperary", "Munster", 21, 2},
    {"Waterford", "Munster", 23, 1},
    {"Westmeath", "Leinster", 24, 1},
    {"Wexford", "Leinster", 25, 1},
    {"Wicklow", "Leinster", 26, 1}
};

/* Town coordinates: Open-Meteo Geocoding / GeoNames, retrieved 24 Oct 2026. */
const CountyTown LOCATIONS[LOCATION_COUNT] = {
    {0, "Carlow", 52.84083, -6.92611},
    {1, "Cavan", 53.99083, -7.36056},
    {2, "Ennis", 52.84361, -8.98639},
    {3, "Cork", 51.89797, -8.47061},
    {4, "Lifford", 54.83194, -7.48361},
    {5, "Dublin", 53.33306, -6.24889},
    {6, "Galway", 53.27245, -9.05095},
    {7, "Tralee", 52.27042, -9.70264},
    {8, "Naas", 53.21583, -6.66694},
    {9, "Kilkenny", 52.65417, -7.25222},
    {10, "Portlaoise", 53.03441, -7.29979},
    {11, "Carrick-on-Shannon", 53.94694, -8.09000},
    {12, "Limerick", 52.66472, -8.62306},
    {13, "Longford", 53.72536, -7.79823},
    {14, "Dundalk", 54.00000, -6.41667},
    {15, "Castlebar", 53.85000, -9.30000},
    {16, "Navan", 53.65278, -6.68139},
    {17, "Monaghan", 54.24589, -6.97098},
    {18, "Tullamore", 53.27389, -7.48889},
    {19, "Roscommon", 53.63333, -8.18333},
    {20, "Sligo", 54.26969, -8.46943},
    {21, "Clonmel", 52.35500, -7.70389},
    {21, "Nenagh", 52.86284, -8.19953},
    {22, "Waterford", 52.25833, -7.11194},
    {23, "Mullingar", 53.52466, -7.33850},
    {24, "Wexford", 52.33417, -6.45750},
    {25, "Wicklow", 52.97500, -6.04944}
};

static bool Contains(const char *text, const char *search)
{
    if (!search[0]) return true;
    for (const char *start = text; *start; start++) {
        const char *a = start;
        const char *b = search;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) return true;
    }
    return false;
}

bool CountyMatches(int countyId, const char *search)
{
    if (countyId < 0 || countyId >= COUNTY_COUNT) return false;
    const County *county = &COUNTIES[countyId];
    if (Contains(county->name, search)) return true;
    for (int i = 0; i < county->townCount; i++) {
        if (Contains(LOCATIONS[county->firstLocation + i].town, search)) return true;
    }
    return false;
}

int FindLocation(const char *name)
{
    for (int i = 0; i < LOCATION_COUNT; i++) {
        if (strlen(LOCATIONS[i].town) == strlen(name) && Contains(LOCATIONS[i].town, name)) return i;
    }
    for (int i = 0; i < COUNTY_COUNT; i++) {
        if (strlen(COUNTIES[i].name) == strlen(name) && Contains(COUNTIES[i].name, name)) {
            return COUNTIES[i].firstLocation;
        }
    }
    return -1;
}

int FindCountyTown(int countyId, const char *search)
{
    if (countyId < 0 || countyId >= COUNTY_COUNT) return -1;
    const County *county = &COUNTIES[countyId];
    for (int i = 0; i < county->townCount; i++) {
        int id = county->firstLocation + i;
        if (Contains(LOCATIONS[id].town, search)) return id;
    }
    return county->firstLocation;
}
