#include "counties.h"
#include "data_source.h"
#include "refresh_policy.h"
#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

/* Synthetic fixture: deterministic test input, never shown by the application. */
static const char *validJson =
    "{\"current\":{\"time\":\"2026-10-24T12:15\",\"temperature_2m\":12.5,"
    "\"relative_humidity_2m\":70,\"apparent_temperature\":10.2,\"wind_speed_10m\":15,\"weather_code\":3},"
    "\"daily\":{\"time\":[\"2026-10-24\",\"2026-10-25\",\"2026-10-26\",\"2026-10-27\",\"2026-10-28\",\"2026-10-29\",\"2026-10-30\"],"
    "\"temperature_2m_min\":[5,6,7,8,9,10,11],\"temperature_2m_max\":[12,13,14,15,16,17,18],"
    "\"precipitation_probability_max\":[0,10,20,30,40,50,100],\"precipitation_sum\":[0,1,2,3,4,5,6],"
    "\"wind_speed_10m_max\":[10,11,12,13,14,15,16],\"weather_code\":[0,1,2,3,61,71,95]}}";

static void Catalogue(void)
{
    bool seen[LOCATION_COUNT] = {0};
    for (int i = 0; i < COUNTY_COUNT; i++) {
        const County *county = &COUNTIES[i];
        CHECK(county->townCount >= 1 && county->townCount <= 2);
        CHECK(FindLocation(county->name) == county->firstLocation);
        for (int j = 0; j < county->townCount; j++) {
            int id = county->firstLocation + j;
            CHECK(id >= 0 && id < LOCATION_COUNT);
            if (id < 0 || id >= LOCATION_COUNT) continue;
            CHECK(!seen[id]); seen[id] = true;
            CHECK(LOCATIONS[id].countyId == i);
            CHECK(FindLocation(LOCATIONS[id].town) == id);
            CHECK(LOCATIONS[id].latitude > 51 && LOCATIONS[id].latitude < 56);
            CHECK(LOCATIONS[id].longitude > -11 && LOCATIONS[id].longitude < -5);
        }
    }
    for (int i = 0; i < LOCATION_COUNT; i++) CHECK(seen[i]);
    CHECK(FindLocation("DUBLIN") == 5);
    CHECK(CountyMatches(21, "nena"));
    CHECK(FindCountyTown(21, "nena") == 22);
    CHECK(FindCountyTown(21, "Tipperary") == 21);
    CHECK(CountyMatches(11, "shannon"));
    CHECK(!CountyMatches(0, "Dublin"));
    CHECK(!CountyMatches(-1, ""));
    CHECK(FindLocation("Belfast") == -1);
}

static EnvironmentData Parse(const cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    EnvironmentData result = {0};
    ParseWeatherJson(text, &result);
    cJSON_free(text);
    return result;
}

static void Parsing(void)
{
    EnvironmentData data = {0};
    CHECK(ParseWeatherJson(validJson, &data));
    CHECK(data.weatherValid && data.forecastValid && data.dayCount == 7);
    CHECK(fabs(data.temperature - 12.5) < .001);
    CHECK(strcmp(data.weatherTime, "2026-10-24 12:15") == 0);
    CHECK(data.days[6].rainProbability == 100 && data.days[0].rainProbability == 0);
    CHECK(data.days[6].weatherCode == 95);
    CHECK(!ParseWeatherJson("not json", &data));
    CHECK(!data.weatherValid && !data.forecastValid && data.dayCount == 0);
    CHECK(!ParseWeatherJson("{\"current\":null,\"daily\":null}", &data));

    cJSON *root = cJSON_Parse(validJson);
    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON_ReplaceItemInObjectCaseSensitive(current, "temperature_2m", cJSON_CreateNull());
    data = Parse(root);
    CHECK(!data.weatherValid && data.forecastValid);
    cJSON_ReplaceItemInObjectCaseSensitive(current, "temperature_2m", cJSON_CreateNumber(0));
    data = Parse(root); CHECK(data.weatherValid && data.temperature == 0);
    cJSON_ReplaceItemInObjectCaseSensitive(current, "relative_humidity_2m", cJSON_CreateNumber(101));
    data = Parse(root); CHECK(!data.weatherValid && data.forecastValid);
    cJSON_ReplaceItemInObjectCaseSensitive(current, "relative_humidity_2m", cJSON_CreateNumber(70));
    cJSON_ReplaceItemInObjectCaseSensitive(current, "time", cJSON_CreateString("2026-13-24T25:99"));
    data = Parse(root); CHECK(!data.weatherValid);
    cJSON_Delete(root);

    const char *keys[] = {"temperature_2m_min", "temperature_2m_max", "precipitation_probability_max", "precipitation_sum", "wind_speed_10m_max", "weather_code"};
    for (int i = 0; i < 6; i++) {
        root = cJSON_Parse(validJson);
        cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
        cJSON *values = cJSON_GetObjectItemCaseSensitive(daily, keys[i]);
        cJSON_ReplaceItemInArray(values, 3, cJSON_CreateNull());
        data = Parse(root); CHECK(data.weatherValid && !data.forecastValid);
        cJSON_Delete(root);
    }
    root = cJSON_Parse(validJson);
    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON *minimums = cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min");
    cJSON_ReplaceItemInArray(minimums, 1, cJSON_CreateNumber(80));
    data = Parse(root); CHECK(!data.forecastValid);
    cJSON_Delete(root);
    root = cJSON_Parse(validJson);
    daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(daily, "time"), 6);
    data = Parse(root); CHECK(!data.forecastValid);
    cJSON_Delete(root);
}

static void Cache(void)
{
    EnvironmentData cached = {.locationId = 5};
    EnvironmentData incoming = {.locationId = 0};
    ParseWeatherJson(validJson, &incoming);
    CHECK(!MergeEnvironmentData(&cached, &incoming));
    CHECK(!cached.weatherValid && !cached.forecastValid);
    incoming.locationId = 5;
    CHECK(MergeEnvironmentData(&cached, &incoming));
    CHECK(cached.weatherValid && cached.forecastValid && cached.temperature == 12.5);
    incoming = (EnvironmentData){.locationId = 5};
    snprintf(incoming.weatherError, sizeof(incoming.weatherError), "HTTP error 503");
    CHECK(MergeEnvironmentData(&cached, &incoming));
    CHECK(cached.temperature == 12.5 && cached.dayCount == 7 && cached.days[6].high == 18);
    CHECK(strcmp(cached.weatherError, "HTTP error 503") == 0);
    CHECK(!cached.airValid);
}

static void Refresh(void)
{
    RefreshPolicy policy = RefreshPolicyInit(0);
    CHECK(RefreshPolicyDue(&policy, 0, false, false));
    RefreshPolicyStarted(&policy, 0);
    CHECK(!RefreshPolicyDue(&policy, 20, true, true));
    RefreshPolicyFinished(&policy, 1, true);
    CHECK(!RefreshPolicyDue(&policy, 5, true, false));
    CHECK(RefreshPolicyDue(&policy, 6, true, false));
    RefreshPolicyStarted(&policy, 6); RefreshPolicyFinished(&policy, 7, true);
    CHECK(!RefreshPolicyDue(&policy, 36, true, false));
    CHECK(RefreshPolicyDue(&policy, 37, true, false));
    RefreshPolicyFinished(&policy, 40, false);
    CHECK(!RefreshPolicyDue(&policy, 99, true, false));
    CHECK(RefreshPolicyDue(&policy, 100, false, false));
    RefreshPolicyFinished(&policy, 100, false);
    CHECK(policy.nextRefresh == 220);
    CHECK(RefreshSecondsLeft(1.1, 1) == 1);
}

int main(void)
{
    Catalogue(); Parsing(); Cache(); Refresh();
    printf("Core tests: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
